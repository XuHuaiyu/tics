#pragma once

#include <DataStreams/IBlockInputStream.h>
#include <RaftStoreProxyFFI/ColumnFamily.h>
#include <Storages/DeltaMerge/DMVersionFilterBlockInputStream.h>

#include <memory>
#include <string_view>

namespace Poco
{
class Logger;
}

namespace DB
{

class TMTContext;
class Region;
using RegionPtr = std::shared_ptr<Region>;

struct SSTViewVec;
struct TiFlashRaftProxyHelper;
struct SSTReader;
class StorageDeltaMerge;

namespace DM
{

struct ColumnDefine;
using ColumnDefines    = std::vector<ColumnDefine>;
using ColumnDefinesPtr = std::shared_ptr<ColumnDefines>;

// forward declaration
class SSTFilesToBlockInputStream;
using SSTFilesToBlockInputStreamPtr = std::shared_ptr<SSTFilesToBlockInputStream>;
class BoundedSSTFilesToBlockInputStream;
using BoundedSSTFilesToBlockInputStreamPtr = std::shared_ptr<BoundedSSTFilesToBlockInputStream>;

class SSTFilesToBlockInputStream final : public IBlockInputStream
{
public:
    using StorageDeltaMergePtr = std::shared_ptr<StorageDeltaMerge>;
    SSTFilesToBlockInputStream(RegionPtr                      region_,
                               const SSTViewVec &             snaps_,
                               const TiFlashRaftProxyHelper * proxy_helper_,
                               StorageDeltaMergePtr           ingest_storage_,
                               DM::ColumnDefinesPtr           schema_snap_,
                               Timestamp                      gc_safepoint_,
                               bool                           force_decode_,
                               TMTContext &                   tmt_,
                               size_t                         expected_size_ = DEFAULT_MERGE_BLOCK_SIZE);
    ~SSTFilesToBlockInputStream();

    String getName() const override { return "SSTFilesToBlockInputStream"; }

    Block getHeader() const override { return toEmptyBlock(*schema_snap); }

    void  readPrefix() override;
    void  readSuffix() override;
    Block read() override;

public:
    struct ProcessKeys
    {
        size_t default_cf;
        size_t write_cf;
        size_t lock_cf;

        inline size_t total() const { return default_cf + write_cf + lock_cf; }
    };

private:
    void scanCF(ColumnFamilyType cf, const std::string_view until = std::string_view{});

    Block readCommitedBlock();

private:
    RegionPtr                      region;
    const SSTViewVec &             snaps;
    const TiFlashRaftProxyHelper * proxy_helper{nullptr};
    const StorageDeltaMergePtr     ingest_storage;
    const DM::ColumnDefinesPtr     schema_snap;
    TMTContext &                   tmt;
    const Timestamp                gc_safepoint;
    size_t                         expected_size;
    Poco::Logger *                 log;

    using SSTReaderPtr = std::unique_ptr<SSTReader>;
    SSTReaderPtr write_cf_reader;
    SSTReaderPtr default_cf_reader;
    SSTReaderPtr lock_cf_reader;

    friend class BoundedSSTFilesToBlockInputStream;

    const bool force_decode;
    bool       is_decode_cancelled = false;

    ProcessKeys process_keys;
};

// Bound the blocks read from SSTFilesToBlockInputStream by column `_tidb_rowid` and
// do some calculation for the `DMFileWriter::BlockProperty` of read blocks.
class BoundedSSTFilesToBlockInputStream final
{
public:
    BoundedSSTFilesToBlockInputStream(SSTFilesToBlockInputStreamPtr child, const ColId pk_column_id_, const bool is_common_handle_);

    String getName() const { return "BoundedSSTFilesToBlockInputStream"; }

    void readPrefix();

    void readSuffix();

    Block read();

    std::tuple<std::shared_ptr<StorageDeltaMerge>, DM::ColumnDefinesPtr> ingestingInfo() const;

    SSTFilesToBlockInputStream::ProcessKeys getProcessKeys() const;

    const RegionPtr getRegion() const;

    // Return values: not clean rows
    size_t getMvccStatistics() const;

private:
    const ColId pk_column_id;
    const bool  is_common_handle;

    // Note that we only keep _raw_child for getting ingest info / process key, etc. All block should be
    // read from `mvcc_compact_stream`
    const SSTFilesToBlockInputStreamPtr                                              _raw_child;
    std::unique_ptr<DMVersionFilterBlockInputStream<DM_VERSION_FILTER_MODE_COMPACT>> mvcc_compact_stream;
};

} // namespace DM
} // namespace DB
