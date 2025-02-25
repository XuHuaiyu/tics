#include <DataStreams/LimitByBlockInputStream.h>
#include <Common/SipHash.h>


namespace DB
{

LimitByBlockInputStream::LimitByBlockInputStream(const BlockInputStreamPtr & input, size_t group_size_, const Names & columns)
    : columns_names(columns)
    , group_size(group_size_)
{
    children.push_back(input);
}

Block LimitByBlockInputStream::readImpl()
{
    /// Execute until end of stream or until
    /// a block with some new records will be gotten.
    while (true)
    {
        Block block = children[0]->read();
        if (!block)
            return Block();

        const ColumnRawPtrs column_ptrs(getKeyColumns(block));
        const size_t rows = block.rows();
        IColumn::Filter filter(rows);
        size_t inserted_count = 0;

        for (size_t i = 0; i < rows; ++i)
        {
            UInt128 key;
            SipHash hash;

            for (auto & column : column_ptrs)
                column->updateHashWithValue(i, hash);

            hash.get128(key);

            if (keys_counts[key]++ < group_size)
            {
                inserted_count++;
                filter[i] = 1;
            }
            else
                filter[i] = 0;
        }

        /// Just go to the next block if there isn't any new records in the current one.
        if (!inserted_count)
            continue;

        size_t all_columns = block.columns();
        for (size_t i = 0; i < all_columns; ++i)
            block.safeGetByPosition(i).column = block.safeGetByPosition(i).column->filter(filter, inserted_count);

        return block;
    }
}

ColumnRawPtrs LimitByBlockInputStream::getKeyColumns(Block & block) const
{
    ColumnRawPtrs column_ptrs;
    column_ptrs.reserve(columns_names.size());

    for (const auto & name : columns_names)
    {
        auto & column = block.getByName(name).column;

        /// Ignore all constant columns.
        if (!column->isColumnConst())
            column_ptrs.emplace_back(column.get());
    }

    return column_ptrs;
}

}
