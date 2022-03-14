#include <Common/RedactHelpers.h>
#include <Common/StringUtils/StringUtils.h>
#include <Encryption/FileProvider.h>
#include <Encryption/createReadBufferFromFileBaseByFileProvider.h>
#include <IO/WriteHelpers.h>
#include <Poco/File.h>
#include <Poco/Logger.h>
#include <Storages/Page/V3/LogFile/LogFilename.h>
#include <Storages/Page/V3/LogFile/LogFormat.h>
#include <Storages/Page/V3/PageEntriesEdit.h>
#include <Storages/Page/V3/WAL/WALReader.h>
#include <Storages/Page/V3/WAL/serialize.h>
#include <Storages/PathPool.h>
#include <common/logger_useful.h>

namespace DB::PS::V3
{
LogFilenameSet WALStoreReader::listAllFiles(
    const PSDiskDelegatorPtr & delegator,
    Poco::Logger * logger)
{
    // [<parent_path_0, [file0, file1, ...]>, <parent_path_1, [...]>, ...]
    std::vector<std::pair<String, Strings>> all_filenames;
    Strings filenames;
    for (const auto & p : delegator->listPaths())
    {
        Poco::File directory(p);
        if (!directory.exists())
            directory.createDirectories();
        filenames.clear();
        directory.list(filenames);
        all_filenames.emplace_back(std::make_pair(p, std::move(filenames)));
        filenames.clear();
    }
    assert(all_filenames.size() == 1); // TODO: multi-path

    LogFilenameSet log_files;
    for (const auto & [parent_path, filenames] : all_filenames)
    {
        for (const auto & filename : filenames)
        {
            auto name = LogFilename::parseFrom(parent_path, filename, logger);
            switch (name.stage)
            {
            case LogFileStage::Normal:
            {
                log_files.insert(name);
                break;
            }
            case LogFileStage::Temporary:
                [[fallthrough]];
            case LogFileStage::Invalid:
            {
                // TODO: clean
                break;
            }
            }
        }
    }
    return log_files;
}

std::tuple<std::optional<LogFilename>, LogFilenameSet>
WALStoreReader::findCheckpoint(LogFilenameSet && all_files)
{
    LogFilenameSet::const_iterator latest_checkpoint_iter = all_files.cend();
    for (auto iter = all_files.cbegin(); iter != all_files.cend(); ++iter)
    {
        if (iter->level_num > 0)
        {
            latest_checkpoint_iter = iter;
        }
    }
    if (latest_checkpoint_iter == all_files.cend())
    {
        return {std::nullopt, std::move(all_files)};
    }

    LogFilename latest_checkpoint = *latest_checkpoint_iter;
    for (auto iter = all_files.cbegin(); iter != all_files.cend(); /*empty*/)
    {
        // We use <largest_num, 1> as the checkpoint, so all files less than or equal
        // to latest_checkpoint.log_num can be erase
        if (iter->log_num <= latest_checkpoint.log_num)
        {
            if (iter->log_num == latest_checkpoint.log_num && iter->level_num != 0)
            {
                // the checkpoint file, not remove
            }
            else
            {
                // TODO: clean useless file that is older than `checkpoint`
            }
            iter = all_files.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
    return {latest_checkpoint, std::move(all_files)};
}

WALStoreReaderPtr WALStoreReader::create(FileProviderPtr & provider, LogFilenameSet files, const ReadLimiterPtr & read_limiter)
{
    auto [checkpoint, files_to_read] = findCheckpoint(std::move(files));
    auto reader = std::make_shared<WALStoreReader>(provider, checkpoint, std::move(files_to_read), read_limiter);
    reader->openNextFile();
    return reader;
}

WALStoreReaderPtr WALStoreReader::create(FileProviderPtr & provider, PSDiskDelegatorPtr & delegator, const ReadLimiterPtr & read_limiter)
{
    Poco::Logger * logger = &Poco::Logger::get("WALStore");
    LogFilenameSet log_files = listAllFiles(delegator, logger);
    return create(provider, std::move(log_files), read_limiter);
}

WALStoreReader::WALStoreReader(FileProviderPtr & provider_, std::optional<LogFilename> checkpoint, LogFilenameSet && files_, const ReadLimiterPtr & read_limiter_)
    : provider(provider_)
    , read_limiter(read_limiter_)
    , checkpoint_read_done(!checkpoint.has_value())
    , checkpoint_file(checkpoint)
    , files_to_read(std::move(files_))
    , next_reading_file(files_to_read.begin())
    , logger(&Poco::Logger::get("LogReader"))
{}

bool WALStoreReader::remained() const
{
    if (reader == nullptr)
        return false;

    if (!reader->isEOF())
        return true;
    if (checkpoint_read_done && next_reading_file != files_to_read.end())
        return true;
    return false;
}

std::tuple<bool, PageEntriesEdit> WALStoreReader::next()
{
    bool ok = false;
    String record;
    do
    {
        std::tie(ok, record) = reader->readRecord();
        if (ok)
        {
            return {true, ser::deserializeFrom(record)};
        }

        // Roll to read the next file
        if (bool next_file = openNextFile(); !next_file)
        {
            // No more file to be read.
            return {false, PageEntriesEdit{}};
        }
    } while (true);
}

bool WALStoreReader::openNextFile()
{
    if (checkpoint_read_done && next_reading_file == files_to_read.end())
    {
        return false;
    }

    auto do_open = [this](const LogFilename & next_file) {
        const auto & parent_path = next_file.parent_path;
        const auto log_num = next_file.log_num;
        const auto level_num = next_file.level_num;
        const auto filename = fmt::format("log_{}_{}", log_num, level_num);
        const auto fullname = fmt::format("{}/{}", parent_path, filename);
        LOG_FMT_DEBUG(logger, "Open log file for reading [file={}]", fullname);

        auto read_buf = createReadBufferFromFileBaseByFileProvider(
            provider,
            fullname,
            EncryptionPath{parent_path, filename},
            /*estimated_size*/ Format::BLOCK_SIZE,
            /*aio_threshold*/ 0,
            /*read_limiter*/ read_limiter,
            /*buffer_size*/ Format::BLOCK_SIZE // Must be `Format::BLOCK_SIZE`
        );
        reader = std::make_unique<LogReader>(
            std::move(read_buf),
            &reporter,
            /*verify_checksum*/ true,
            log_num,
            WALRecoveryMode::TolerateCorruptedTailRecords,
            logger);
    };

    if (!checkpoint_read_done)
    {
        do_open(*checkpoint_file);
        checkpoint_read_done = true;
    }
    else
    {
        do_open(*next_reading_file);
        ++next_reading_file;
    }
    return true;
}

} // namespace DB::PS::V3
