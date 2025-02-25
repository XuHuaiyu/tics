#pragma once

#include <Core/Row.h>
#include <Core/ColumnNumbers.h>
#include <DataStreams/MergingSortedBlockInputStream.h>
#include <AggregateFunctions/IAggregateFunction.h>
#include <AggregateFunctions/AggregateFunctionFactory.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}


/** Merges several sorted streams into one.
  * For each group of consecutive identical values of the primary key (the columns by which the data is sorted),
  *  collapses them into one row, summing all the numeric columns except the primary key.
  * If in all numeric columns, except for the primary key, the result is zero, it deletes the row.
  */
class SummingSortedBlockInputStream : public MergingSortedBlockInputStream
{
public:
    SummingSortedBlockInputStream(
        const BlockInputStreams & inputs_,
        const SortDescription & description_,
        /// List of columns to be summed. If empty, all numeric columns that are not in the description are taken.
        const Names & column_names_to_sum_,
        size_t max_block_size_);

    String getName() const override { return "SummingSorted"; }

protected:
    /// Can return 1 more records than max_block_size.
    Block readImpl() override;

private:
    Poco::Logger * log = &Poco::Logger::get("SummingSortedBlockInputStream");

    /// Read up to the end.
    bool finished = false;

    /// Columns with which values should be summed.
    ColumnNumbers column_numbers_not_to_aggregate;

    /** A table can have nested tables that are treated in a special way.
     *    If the name of the nested table ends in `Map` and it contains at least two columns,
     *    satisfying the following criteria:
     *        - the first column, as well as all columns whose names end with `ID`, `Key` or `Type` - numeric ((U)IntN, Date, DateTime);
     *        (a tuple of such columns will be called `keys`)
     *        - the remaining columns are arithmetic ((U)IntN, Float32/64), called (`values`...).
     *    This nested table is treated as a mapping (keys...) => (values...) and when merge
     *    its rows, the merge of the elements of two sets by (keys...) with summing of corresponding (values...).
     *
     *    Example:
     *    [(1, 100)] + [(2, 150)] -> [(1, 100), (2, 150)]
     *    [(1, 100)] + [(1, 150)] -> [(1, 250)]
     *    [(1, 100)] + [(1, 150), (2, 150)] -> [(1, 250), (2, 150)]
     *    [(1, 100), (2, 150)] + [(1, -100)] -> [(2, 150)]
     *
     *  This very unusual functionality is made exclusively for the banner system,
     *   is not supposed for use by anyone else,
     *   and can be deleted at any time.
     */

    /// Stores aggregation function, state, and columns to be used as function arguments
    struct AggregateDescription
    {
        AggregateFunctionPtr function;
        IAggregateFunction::AddFunc add_function = nullptr;
        std::vector<size_t> column_numbers;
        MutableColumnPtr merged_column;
        std::vector<char> state;
        bool created = false;

        void init(const char * function_name, const DataTypes & argument_types)
        {
            function = AggregateFunctionFactory::instance().get(function_name, argument_types);
            add_function = function->getAddressOfAddFunction();
            state.resize(function->sizeOfData());
        }

        void createState()
        {
            if (created)
                return;
            function->create(state.data());
            created = true;
        }

        void destroyState()
        {
            if (!created)
                return;
            function->destroy(state.data());
            created = false;
        }

        /// Explicitly destroy aggregation state if the stream is terminated
        ~AggregateDescription()
        {
            destroyState();
        }

        AggregateDescription() = default;
        AggregateDescription(AggregateDescription &&) = default;
        AggregateDescription(const AggregateDescription &) = delete;
    };

    /// Stores numbers of key-columns and value-columns.
    struct MapDescription
    {
        std::vector<size_t> key_col_nums;
        std::vector<size_t> val_col_nums;
    };

    std::vector<AggregateDescription> columns_to_aggregate;
    std::vector<MapDescription> maps_to_sum;

    RowRef current_key;        /// The current primary key.
    RowRef next_key;           /// The primary key of the next row.

    Row current_row;
    bool current_row_is_zero = true;    /// Are all summed columns zero (or empty)? It is updated incrementally.

    bool output_is_non_empty = false;   /// Have we given out at least one row as a result.
    size_t merged_rows = 0;             /// Number of rows merged into current result block

    /** We support two different cursors - with Collation and without.
     *  Templates are used instead of polymorphic SortCursor and calls to virtual functions.
     */
    void merge(MutableColumns & merged_columns, std::priority_queue<SortCursor> & queue);

    /// Insert the summed row for the current group into the result and updates some of per-block flags if the row is not "zero".
    /// If force_insertion=true, then the row will be inserted even if it is "zero"
    void insertCurrentRowIfNeeded(MutableColumns & merged_columns, bool force_insertion);

    /// Returns true if merge result is not empty
    bool mergeMap(const MapDescription & map, Row & row, SortCursor & cursor);

    // Add the row under the cursor to the `row`.
    void addRow(SortCursor & cursor);
};

}
