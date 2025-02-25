#pragma once
#include <Core/Types.h>
#include <IO/ReadBuffer.h>
#include <IO/WriteBuffer.h>
#include <city.h>

#include <map>
#include <memory>


class SipHash;


namespace DB
{
/// Checksum of one file.
struct MergeTreeDataPartChecksum
{
    using uint128 = CityHash_v1_0_2::uint128;

    UInt64 file_size{};
    uint128 file_hash{};

    bool is_compressed = false;
    UInt64 uncompressed_size{};
    uint128 uncompressed_hash{};

    MergeTreeDataPartChecksum() {}
    MergeTreeDataPartChecksum(UInt64 file_size_, uint128 file_hash_)
        : file_size(file_size_)
        , file_hash(file_hash_)
    {}
    MergeTreeDataPartChecksum(UInt64 file_size_, uint128 file_hash_, UInt64 uncompressed_size_, uint128 uncompressed_hash_)
        : file_size(file_size_)
        , file_hash(file_hash_)
        , is_compressed(true)
        , uncompressed_size(uncompressed_size_)
        , uncompressed_hash(uncompressed_hash_)
    {}

    void checkEqual(const MergeTreeDataPartChecksum & rhs, bool have_uncompressed, const String & name) const;
    void checkSize(const String & path) const;
};


/** Checksums of all non-temporary files.
  * For compressed files, the check sum and the size of the decompressed data are stored to not depend on the compression method.
  */
struct MergeTreeDataPartChecksums
{
    using Checksum = MergeTreeDataPartChecksum;

    /// The order is important.
    using FileChecksums = std::map<String, Checksum>;
    FileChecksums files;

    void addFile(const String & file_name, UInt64 file_size, Checksum::uint128 file_hash);

    void add(MergeTreeDataPartChecksums && rhs_checksums);

    bool empty() const
    {
        return files.empty();
    }

    /// Checks that the set of columns and their checksums are the same. If not, throws an exception.
    /// If have_uncompressed, for compressed files it compares the checksums of the decompressed data.
    /// Otherwise, it compares only the checksums of the files.
    void checkEqual(const MergeTreeDataPartChecksums & rhs, bool have_uncompressed) const;

    static bool isBadChecksumsErrorCode(int code);

    /// Checks that the directory contains all the needed files of the correct size. Does not check the checksum.
    void checkSizes(const String & path) const;

    /// Returns false if the checksum is too old.
    bool read(ReadBuffer & in);
    /// Assume that header with version (the first line) is read
    bool read(ReadBuffer & in, size_t format_version);
    bool read_v2(ReadBuffer & in);
    bool read_v3(ReadBuffer & in);
    bool read_v4(ReadBuffer & in);

    void write(WriteBuffer & out) const;

    /// Checksum from the set of checksums of .bin files (for deduplication).
    void computeTotalChecksumDataOnly(SipHash & hash) const;

    String getSerializedString() const;
    static MergeTreeDataPartChecksums deserializeFrom(const String & s);
};


/// A kind of MergeTreeDataPartChecksums intended to be stored in ZooKeeper (to save its RAM)
/// MinimalisticDataPartChecksums and MergeTreeDataPartChecksums hasve the same serialization format
///  for versions less than MINIMAL_VERSION_WITH_MINIMALISTIC_CHECKSUMS.
struct MinimalisticDataPartChecksums
{
    UInt64 num_compressed_files = 0;
    UInt64 num_uncompressed_files = 0;

    using uint128 = MergeTreeDataPartChecksum::uint128;
    uint128 hash_of_all_files{};
    uint128 hash_of_uncompressed_files{};
    uint128 uncompressed_hash_of_compressed_files{};

    /// Is set only for old formats
    std::unique_ptr<MergeTreeDataPartChecksums> full_checksums;

    static constexpr size_t MINIMAL_VERSION_WITH_MINIMALISTIC_CHECKSUMS = 5;

    MinimalisticDataPartChecksums() = default;
    void computeTotalChecksums(const MergeTreeDataPartChecksums & full_checksums);

    bool deserialize(ReadBuffer & in);
    static MinimalisticDataPartChecksums deserializeFrom(const String & s);

    void serialize(WriteBuffer & to) const;
    String getSerializedString();
    static String getSerializedString(const MergeTreeDataPartChecksums & full_checksums, bool minimalistic);

    void checkEqual(const MinimalisticDataPartChecksums & rhs, bool check_uncompressed_hash_in_compressed_files);
    void checkEqual(const MergeTreeDataPartChecksums & rhs, bool check_uncompressed_hash_in_compressed_files);
    void checkEqualImpl(const MinimalisticDataPartChecksums & rhs, bool check_uncompressed_hash_in_compressed_files);
};


} // namespace DB
