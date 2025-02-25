#include "MergeTreeDataPartChecksum.h"

#include <Common/SipHash.h>
#include <IO/CompressedReadBuffer.h>
#include <IO/CompressedWriteBuffer.h>
#include <IO/ReadBufferFromString.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteBufferFromString.h>
#include <IO/WriteHelpers.h>
#include <Poco/File.h>


namespace DB
{
namespace ErrorCodes
{
extern const int CHECKSUM_DOESNT_MATCH;
extern const int BAD_SIZE_OF_FILE_IN_DATA_PART;
extern const int FORMAT_VERSION_TOO_OLD;
extern const int FILE_DOESNT_EXIST;
extern const int UNEXPECTED_FILE_IN_DATA_PART;
extern const int UNKNOWN_FORMAT;
extern const int NO_FILE_IN_DATA_PART;
} // namespace ErrorCodes


void MergeTreeDataPartChecksum::checkEqual(const MergeTreeDataPartChecksum & rhs, bool have_uncompressed, const String & name) const
{
    if (is_compressed && have_uncompressed)
    {
        if (!rhs.is_compressed)
            throw Exception("No uncompressed checksum for file " + name, ErrorCodes::CHECKSUM_DOESNT_MATCH);
        if (rhs.uncompressed_size != uncompressed_size)
            throw Exception("Unexpected uncompressed size of file " + name + " in data part", ErrorCodes::BAD_SIZE_OF_FILE_IN_DATA_PART);
        if (rhs.uncompressed_hash != uncompressed_hash)
            throw Exception("Checksum mismatch for uncompressed file " + name + " in data part", ErrorCodes::CHECKSUM_DOESNT_MATCH);
        return;
    }
    if (rhs.file_size != file_size)
        throw Exception("Unexpected size of file " + name + " in data part", ErrorCodes::BAD_SIZE_OF_FILE_IN_DATA_PART);
    if (rhs.file_hash != file_hash)
        throw Exception("Checksum mismatch for file " + name + " in data part", ErrorCodes::CHECKSUM_DOESNT_MATCH);
}

void MergeTreeDataPartChecksum::checkSize(const String & path) const
{
    Poco::File file(path);
    if (!file.exists())
        throw Exception(path + " doesn't exist", ErrorCodes::FILE_DOESNT_EXIST);
    UInt64 size = file.getSize();
    if (size != file_size)
        throw Exception(path + " has unexpected size: " + toString(size) + " instead of " + toString(file_size),
                        ErrorCodes::BAD_SIZE_OF_FILE_IN_DATA_PART);
}


void MergeTreeDataPartChecksums::checkEqual(const MergeTreeDataPartChecksums & rhs, bool have_uncompressed) const
{
    for (const auto & it : rhs.files)
    {
        const String & name = it.first;

        if (!files.count(name))
            throw Exception("Unexpected file " + name + " in data part", ErrorCodes::UNEXPECTED_FILE_IN_DATA_PART);
    }

    for (const auto & it : files)
    {
        const String & name = it.first;

        auto jt = rhs.files.find(name);
        if (jt == rhs.files.end())
            throw Exception("No file " + name + " in data part", ErrorCodes::NO_FILE_IN_DATA_PART);

        it.second.checkEqual(jt->second, have_uncompressed, name);
    }
}

void MergeTreeDataPartChecksums::checkSizes(const String & path) const
{
    for (const auto & it : files)
    {
        const String & name = it.first;
        it.second.checkSize(path + name);
    }
}

bool MergeTreeDataPartChecksums::read(ReadBuffer & in, size_t format_version)
{
    switch (format_version)
    {
    case 1:
        return false;
    case 2:
        return read_v2(in);
    case 3:
        return read_v3(in);
    case 4:
        return read_v4(in);
    default:
        throw Exception("Bad checksums format version: " + DB::toString(format_version), ErrorCodes::UNKNOWN_FORMAT);
    }
}

bool MergeTreeDataPartChecksums::read(ReadBuffer & in)
{
    files.clear();

    assertString("checksums format version: ", in);
    size_t format_version;
    readText(format_version, in);
    assertChar('\n', in);

    read(in, format_version);
    return true;
}

bool MergeTreeDataPartChecksums::read_v2(ReadBuffer & in)
{
    size_t count;

    readText(count, in);
    assertString(" files:\n", in);

    for (size_t i = 0; i < count; ++i)
    {
        String name;
        Checksum sum;

        readString(name, in);
        assertString("\n\tsize: ", in);
        readText(sum.file_size, in);
        assertString("\n\thash: ", in);
        readText(sum.file_hash.first, in);
        assertString(" ", in);
        readText(sum.file_hash.second, in);
        assertString("\n\tcompressed: ", in);
        readText(sum.is_compressed, in);
        if (sum.is_compressed)
        {
            assertString("\n\tuncompressed size: ", in);
            readText(sum.uncompressed_size, in);
            assertString("\n\tuncompressed hash: ", in);
            readText(sum.uncompressed_hash.first, in);
            assertString(" ", in);
            readText(sum.uncompressed_hash.second, in);
        }
        assertChar('\n', in);

        files.insert(std::make_pair(name, sum));
    }

    return true;
}

bool MergeTreeDataPartChecksums::read_v3(ReadBuffer & in)
{
    size_t count;

    readVarUInt(count, in);

    for (size_t i = 0; i < count; ++i)
    {
        String name;
        Checksum sum;

        readBinary(name, in);
        readVarUInt(sum.file_size, in);
        readPODBinary(sum.file_hash, in);
        readBinary(sum.is_compressed, in);

        if (sum.is_compressed)
        {
            readVarUInt(sum.uncompressed_size, in);
            readPODBinary(sum.uncompressed_hash, in);
        }

        files.emplace(std::move(name), sum);
    }

    return true;
}

bool MergeTreeDataPartChecksums::read_v4(ReadBuffer & from)
{
    CompressedReadBuffer in{from};
    return read_v3(in);
}

void MergeTreeDataPartChecksums::write(WriteBuffer & to) const
{
    writeString("checksums format version: 4\n", to);

    CompressedWriteBuffer out{to, CompressionSettings(CompressionMethod::LZ4), 1 << 16};

    writeVarUInt(files.size(), out);

    for (const auto & it : files)
    {
        const String & name = it.first;
        const Checksum & sum = it.second;

        writeBinary(name, out);
        writeVarUInt(sum.file_size, out);
        writePODBinary(sum.file_hash, out);
        writeBinary(sum.is_compressed, out);

        if (sum.is_compressed)
        {
            writeVarUInt(sum.uncompressed_size, out);
            writePODBinary(sum.uncompressed_hash, out);
        }
    }
}

void MergeTreeDataPartChecksums::addFile(const String & file_name, UInt64 file_size, MergeTreeDataPartChecksum::uint128 file_hash)
{
    files[file_name] = Checksum(file_size, file_hash);
}

void MergeTreeDataPartChecksums::add(MergeTreeDataPartChecksums && rhs_checksums)
{
    for (auto & checksum : rhs_checksums.files)
        files[std::move(checksum.first)] = std::move(checksum.second);

    rhs_checksums.files.clear();
}

/// Checksum computed from the set of control sums of .bin files.
void MergeTreeDataPartChecksums::computeTotalChecksumDataOnly(SipHash & hash) const
{
    /// We use fact that iteration is in deterministic (lexicographical) order.
    for (const auto & it : files)
    {
        const String & name = it.first;
        const Checksum & sum = it.second;

        if (!endsWith(name, ".bin"))
            continue;

        UInt64 len = name.size();
        hash.update(len);
        hash.update(name.data(), len);
        hash.update(sum.uncompressed_size);
        hash.update(sum.uncompressed_hash);
    }
}

String MergeTreeDataPartChecksums::getSerializedString() const
{
    WriteBufferFromOwnString out;
    write(out);
    return out.str();
}

MergeTreeDataPartChecksums MergeTreeDataPartChecksums::deserializeFrom(const String & s)
{
    ReadBufferFromString in(s);
    MergeTreeDataPartChecksums res;
    if (!res.read(in))
        throw Exception("Checksums format is too old", ErrorCodes::FORMAT_VERSION_TOO_OLD);
    assertEOF(in);
    return res;
}

bool MergeTreeDataPartChecksums::isBadChecksumsErrorCode(int code)
{
    return code == ErrorCodes::CHECKSUM_DOESNT_MATCH
        || code == ErrorCodes::BAD_SIZE_OF_FILE_IN_DATA_PART
        || code == ErrorCodes::NO_FILE_IN_DATA_PART
        || code == ErrorCodes::UNEXPECTED_FILE_IN_DATA_PART;
}

void MinimalisticDataPartChecksums::serialize(WriteBuffer & to) const
{
    writeString("checksums format version: 5\n", to);

    writeVarUInt(num_compressed_files, to);
    writeVarUInt(num_uncompressed_files, to);

    writePODBinary(hash_of_all_files, to);
    writePODBinary(hash_of_uncompressed_files, to);
    writePODBinary(uncompressed_hash_of_compressed_files, to);
}

String MinimalisticDataPartChecksums::getSerializedString()
{
    WriteBufferFromOwnString wb;
    serialize(wb);
    return wb.str();
}

bool MinimalisticDataPartChecksums::deserialize(ReadBuffer & in)
{
    assertString("checksums format version: ", in);
    size_t format_version;
    readText(format_version, in);
    assertChar('\n', in);

    if (format_version < MINIMAL_VERSION_WITH_MINIMALISTIC_CHECKSUMS)
    {
        auto full_checksums_ptr = std::make_unique<MergeTreeDataPartChecksums>();
        if (!full_checksums_ptr->read(in, format_version))
            return false;

        computeTotalChecksums(*full_checksums_ptr);
        full_checksums = std::move(full_checksums_ptr);
        return true;
    }

    if (format_version > MINIMAL_VERSION_WITH_MINIMALISTIC_CHECKSUMS)
        throw Exception("Unknown checksums format version: " + DB::toString(format_version), ErrorCodes::UNKNOWN_FORMAT);

    readVarUInt(num_compressed_files, in);
    readVarUInt(num_uncompressed_files, in);

    readPODBinary(hash_of_all_files, in);
    readPODBinary(hash_of_uncompressed_files, in);
    readPODBinary(uncompressed_hash_of_compressed_files, in);

    return true;
}

void MinimalisticDataPartChecksums::computeTotalChecksums(const MergeTreeDataPartChecksums & full_checksums)
{
    num_compressed_files = 0;
    num_uncompressed_files = 0;

    SipHash hash_of_all_files_;
    SipHash hash_of_uncompressed_files_;
    SipHash uncompressed_hash_of_compressed_files_;

    auto update_hash = [](SipHash & hash, const std::string & data) {
        UInt64 len = data.size();
        hash.update(len);
        hash.update(data.data(), len);
    };

    for (const auto & elem : full_checksums.files)
    {
        const String & name = elem.first;
        const auto & checksum = elem.second;

        update_hash(hash_of_all_files_, name);
        hash_of_all_files_.update(checksum.file_hash);

        if (!checksum.is_compressed)
        {
            ++num_uncompressed_files;
            update_hash(hash_of_uncompressed_files_, name);
            hash_of_uncompressed_files_.update(checksum.file_hash);
        }
        else
        {
            ++num_compressed_files;
            update_hash(uncompressed_hash_of_compressed_files_, name);
            uncompressed_hash_of_compressed_files_.update(checksum.uncompressed_hash);
        }
    }

    auto get_hash = [](SipHash & hash, uint128 & data) {
        hash.get128(data.first, data.second);
    };

    get_hash(hash_of_all_files_, hash_of_all_files);
    get_hash(hash_of_uncompressed_files_, hash_of_uncompressed_files);
    get_hash(uncompressed_hash_of_compressed_files_, uncompressed_hash_of_compressed_files);
}

String MinimalisticDataPartChecksums::getSerializedString(const MergeTreeDataPartChecksums & full_checksums, bool minimalistic)
{
    if (!minimalistic)
        return full_checksums.getSerializedString();

    MinimalisticDataPartChecksums checksums;
    checksums.computeTotalChecksums(full_checksums);
    return checksums.getSerializedString();
}

void MinimalisticDataPartChecksums::checkEqual(const MinimalisticDataPartChecksums & rhs, bool check_uncompressed_hash_in_compressed_files)
{
    if (full_checksums && rhs.full_checksums)
        full_checksums->checkEqual(*rhs.full_checksums, check_uncompressed_hash_in_compressed_files);

    // If full checksums were checked, check total checksums just in case
    checkEqualImpl(rhs, check_uncompressed_hash_in_compressed_files);
}

void MinimalisticDataPartChecksums::checkEqual(const MergeTreeDataPartChecksums & rhs, bool check_uncompressed_hash_in_compressed_files)
{
    if (full_checksums)
        full_checksums->checkEqual(rhs, check_uncompressed_hash_in_compressed_files);

    // If full checksums were checked, check total checksums just in case
    MinimalisticDataPartChecksums rhs_minimalistic;
    rhs_minimalistic.computeTotalChecksums(rhs);
    checkEqualImpl(rhs_minimalistic, check_uncompressed_hash_in_compressed_files);
}

void MinimalisticDataPartChecksums::checkEqualImpl(const MinimalisticDataPartChecksums & rhs, bool check_uncompressed_hash_in_compressed_files)
{
    if (num_compressed_files != rhs.num_compressed_files || num_uncompressed_files != rhs.num_uncompressed_files)
    {
        std::stringstream error_msg;
        error_msg << "Different number of files: " << rhs.num_compressed_files << " compressed (expected " << num_compressed_files << ")"
                  << " and " << rhs.num_uncompressed_files << " uncompressed ones (expected " << num_uncompressed_files << ")";

        throw Exception(error_msg.str(), ErrorCodes::CHECKSUM_DOESNT_MATCH);
    }

    Strings errors;

    if (hash_of_uncompressed_files != rhs.hash_of_uncompressed_files)
        errors.emplace_back("hash of uncompressed files doesn't match");

    if (check_uncompressed_hash_in_compressed_files)
    {
        if (uncompressed_hash_of_compressed_files != rhs.uncompressed_hash_of_compressed_files)
            errors.emplace_back("uncompressed hash of compressed files doesn't match");
    }
    else
    {
        if (hash_of_all_files != rhs.hash_of_all_files)
            errors.emplace_back("total hash of all files doesn't match");
    }

    if (!errors.empty())
    {
        String error_msg = "Checksums of parts don't match: " + errors.front();
        for (size_t i = 1; i < errors.size(); ++i)
            error_msg += ", " + errors[i];

        throw Exception(error_msg, ErrorCodes::CHECKSUM_DOESNT_MATCH);
    }
}

MinimalisticDataPartChecksums MinimalisticDataPartChecksums::deserializeFrom(const String & s)
{
    MinimalisticDataPartChecksums res;
    ReadBufferFromString rb(s);
    res.deserialize(rb);
    return res;
}

} // namespace DB
