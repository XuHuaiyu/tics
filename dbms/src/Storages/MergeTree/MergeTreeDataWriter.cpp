#include <Common/HashTable/HashMap.h>
#include <Common/escapeForFileName.h>
#include <IO/HashingWriteBuffer.h>
#include <Interpreters/AggregationCommon.h>
#include <Poco/File.h>
#include <Storages/MergeTree/MergeTreeDataWriter.h>
#include <Storages/MergeTree/MergedBlockOutputStream.h>
#include <Storages/MergeTree/TMTDataPartProperty.h>
#include <Storages/MutableSupport.h>


namespace ProfileEvents
{
extern const Event MergeTreeDataWriterBlocks;
extern const Event MergeTreeDataWriterBlocksAlreadySorted;
extern const Event MergeTreeDataWriterRows;
extern const Event MergeTreeDataWriterUncompressedBytes;
extern const Event MergeTreeDataWriterCompressedBytes;
} // namespace ProfileEvents

namespace DB
{
namespace
{
void buildScatterSelector(
    const ColumnRawPtrs & columns,
    const size_t partition_mod,
    PODArray<size_t> & partition_num_to_first_row,
    PODArray<UInt128> & partition_num_to_key,
    IColumn::Selector & selector)
{
    /// Use generic hashed variant since partitioning is unlikely to be a bottleneck.
    using Data = HashMap<UInt128, size_t, TrivialHash>;
    Data partitions_map;

    size_t num_rows = columns[0]->size();
    size_t partitions_count = 0;
    for (size_t i = 0; i < num_rows; ++i)
    {
        Data::key_type key = hash128(i, columns.size(), columns, TiDB::dummy_collators, TiDB::dummy_sort_key_contaners);
        // TODO: Better mod calculating.
        key = UInt128(key.low % partition_mod);

        typename Data::LookupResult it;
        bool inserted;
        partitions_map.emplace(key, it, inserted);

        if (inserted)
        {
            partition_num_to_first_row.push_back(i);
            partition_num_to_key.push_back(key);

            it->getMapped() = partitions_count;

            ++partitions_count;

            /// Optimization for common case when there is only one partition - defer selector initialization.
            if (partitions_count == 2)
            {
                selector = IColumn::Selector(num_rows);
                std::fill(selector.begin(), selector.begin() + i, 0);
            }
        }

        if (partitions_count > 1)
            selector[i] = it->getMapped();
    }
}

} // namespace

BlocksWithPartition MergeTreeDataWriter::splitBlockIntoParts(const Block & block)
{
    BlocksWithPartition result;
    if (!block || !block.rows())
        return result;

    data.check(block, true);
    block.checkNumberOfRows();

    if (!data.partition_expr && data.merging_params.mode != MergeTreeData::MergingParams::Mutable && data.merging_params.mode != MergeTreeData::MergingParams::Txn) /// Table is not partitioned.
    {
        result.emplace_back(Block(block), Row());
        return result;
    }

    Block block_copy = block;
    ColumnRawPtrs partition_columns;
    if (data.merging_params.mode != MergeTreeData::MergingParams::Mutable && data.merging_params.mode != MergeTreeData::MergingParams::Txn)
    {
        data.partition_expr->execute(block_copy);
        partition_columns.reserve(data.partition_key_sample.columns());
        for (const ColumnWithTypeAndName & element : data.partition_key_sample)
            partition_columns.emplace_back(block_copy.getByName(element.name).column.get());
    }
    else
    {
        std::vector<String> primary_columns = data.getPrimaryExpression()->getRequiredColumns();
        partition_columns.reserve(primary_columns.size());
        for (const String & name : primary_columns)
            partition_columns.emplace_back(block_copy.getByName(name).column.get());
    }

    PODArray<size_t> partition_num_to_first_row;
    PODArray<UInt128> partition_num_to_key;
    IColumn::Selector selector;

    size_t partition_mod = 1;
    if (data.merging_params.mode == MergeTreeData::MergingParams::Mutable)
        partition_mod = size_t(data.settings.mutable_mergetree_partition_number);
    buildScatterSelector(partition_columns, partition_mod, partition_num_to_first_row, partition_num_to_key, selector);

    size_t partitions_count = partition_num_to_first_row.size();
    result.reserve(partitions_count);

    auto get_partition = [&](size_t num) {
        if (data.merging_params.mode == MergeTreeData::MergingParams::Mutable || data.merging_params.mode == MergeTreeData::MergingParams::Txn)
        {
            Row partition(1);
            partition[0] = partition_num_to_key[num].low;
            return partition;
        }

        Row partition(partition_columns.size());
        for (size_t i = 0; i < partition_columns.size(); ++i)
            partition[i] = Field((*partition_columns[i])[partition_num_to_first_row[num]]);
        return partition;
    };

    if (partitions_count == 1)
    {
        /// A typical case is when there is one partition (you do not need to split anything).
        /// NOTE: returning a copy of the original block so that calculated partition key columns
        /// do not interfere with possible calculated primary key columns of the same name.
        result.emplace_back(Block(block), get_partition(0));
        return result;
    }

    for (size_t i = 0; i < partitions_count; ++i)
        result.emplace_back(block.cloneEmpty(), get_partition(i));

    for (size_t col = 0; col < block.columns(); ++col)
    {
        MutableColumns scattered = block.getByPosition(col).column->scatter(partitions_count, selector);
        for (size_t i = 0; i < partitions_count; ++i)
            result[i].block.getByPosition(col).column = std::move(scattered[i]);
    }

    return result;
}

MergeTreeData::MutableDataPartPtr MergeTreeDataWriter::writeTempPart(BlockWithPartition & block_with_partition)
{
    Block & block = block_with_partition.block;

    static const String TMP_PREFIX = "tmp_insert_";

    /// This will generate unique name in scope of current server process.
    Int64 temp_index = data.insert_increment.get();

    MergeTreeDataPart::MinMaxIndex minmax_idx;
    TMTDataPartProperty tmt_prop;

    minmax_idx.update(block, data.minmax_idx_columns);

    if (data.merging_params.mode == MergeTreeData::MergingParams::Txn)
        tmt_prop.update(block, data.getPrimarySortDescription()[0].column_name);

    MergeTreePartition partition(std::move(block_with_partition.partition));

    MergeTreePartInfo new_part_info(partition.getID(data), temp_index, temp_index, 0);
    String part_name;
    if (data.format_version < MERGE_TREE_DATA_MIN_FORMAT_VERSION_WITH_CUSTOM_PARTITIONING)
    {
        DayNum min_date(minmax_idx.min_values[data.minmax_idx_date_column_pos].get<UInt64>());
        DayNum max_date(minmax_idx.max_values[data.minmax_idx_date_column_pos].get<UInt64>());

        const auto & date_lut = DateLUT::instance();

        auto min_month = date_lut.toNumYYYYMM(min_date);
        auto max_month = date_lut.toNumYYYYMM(max_date);

        if (min_month != max_month)
            throw Exception("Logical error: part spans more than one month.");

        part_name = new_part_info.getPartNameV0(min_date, max_date);
    }
    else
        part_name = new_part_info.getPartName();

    MergeTreeData::MutableDataPartPtr new_data_part = std::make_shared<MergeTreeData::DataPart>(data, part_name, new_part_info);
    new_data_part->partition = std::move(partition);
    new_data_part->minmax_idx = std::move(minmax_idx);
    if (data.merging_params.mode == MergeTreeData::MergingParams::Txn)
        *new_data_part->tmt_property = std::move(tmt_prop);
    new_data_part->relative_path = TMP_PREFIX + part_name;
    new_data_part->is_temp = true;

    /// The name could be non-unique in case of stale files from previous runs.
    String full_path = new_data_part->getFullPath();
    LOG_TRACE(log, "Data part: " << new_data_part->relative_path << " will be in path " << full_path);
    Poco::File dir(full_path);

    if (dir.exists())
    {
        LOG_WARNING(log, "Removing old temporary directory " + full_path);
        dir.remove(true);
    }

    dir.createDirectories();

    /// If we need to calculate some columns to sort.
    if (data.hasPrimaryKey())
    {
        data.getPrimaryExpression()->execute(block);
        auto secondary_sort_expr = data.getSecondarySortExpression();
        if (secondary_sort_expr)
            secondary_sort_expr->execute(block);
    }

    SortDescription sort_descr = data.getSortDescription();

    ProfileEvents::increment(ProfileEvents::MergeTreeDataWriterBlocks);

    /// Sort.
    IColumn::Permutation * perm_ptr = nullptr;
    IColumn::Permutation perm;
    if (data.hasPrimaryKey())
    {
        if (!isAlreadySorted(block, sort_descr))
        {
            stableGetPermutation(block, sort_descr, perm);
            perm_ptr = &perm;
        }
        else
            ProfileEvents::increment(ProfileEvents::MergeTreeDataWriterBlocksAlreadySorted);
    }

    /// This effectively chooses minimal compression method:
    ///  either default lz4 or compression method with zero thresholds on absolute and relative part size.
    auto compression_settings = data.context.chooseCompressionSettings(0, 0);

    // TODO: pass use_l0_opt as arg from caller
    bool use_l0_opt = data.context.useL0Opt() && data.merging_params.mode == MergeTreeData::MergingParams::Mutable;

    NamesAndTypesList columns = data.getColumns().getAllPhysical().filter(block.getNames());
    MergedBlockOutputStream out(data, new_data_part->getFullPath(), columns, use_l0_opt, compression_settings);

    out.writePrefix();
    out.writeWithPermutation(block, perm_ptr);
    out.writeSuffixAndFinalizePart(new_data_part);

    ProfileEvents::increment(ProfileEvents::MergeTreeDataWriterRows, block.rows());
    ProfileEvents::increment(ProfileEvents::MergeTreeDataWriterUncompressedBytes, block.bytes());
    ProfileEvents::increment(ProfileEvents::MergeTreeDataWriterCompressedBytes, new_data_part->bytes_on_disk);

    return new_data_part;
}

} // namespace DB
