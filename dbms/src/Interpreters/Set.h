#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <tipb/expression.pb.h>
#pragma GCC diagnostic pop

#include <Core/Block.h>
#include <DataStreams/SizeLimits.h>
#include <DataTypes/IDataType.h>
#include <Interpreters/Context.h>
#include <Interpreters/SetVariants.h>
#include <Parsers/IAST.h>
#include <Storages/MergeTree/BoolMask.h>
#include <Storages/Transaction/Collator.h>
#include <common/logger_useful.h>

#include <shared_mutex>


namespace DB
{
struct Range;
class FieldWithInfinity;

using SetElements = std::vector<std::vector<Field>>;
using SetElementsPtr = std::unique_ptr<SetElements>;

class IFunctionBase;
using FunctionBasePtr = std::shared_ptr<IFunctionBase>;

/** Data structure for implementation of IN expression.
  */
class Set
{
public:
    Set(const SizeLimits & limits)
        : log(&Poco::Logger::get("Set"))
        , limits(limits)
        , set_elements(std::make_unique<SetElements>())
    {
    }

    bool empty() const { return data.empty(); }

    /** Set can be created either from AST or from a stream of data (subquery result).
      */

    /** Create a Set from expression (specified literally in the query).
      * 'types' - types of what are on the left hand side of IN.
      * 'node' - list of values: 1, 2, 3 or list of tuples: (1, 2), (3, 4), (5, 6).
      * 'fill_set_elements' - if true, fill vector of elements. For primary key to work.
      */
    void createFromAST(const DataTypes & types, ASTPtr node, const Context & context, bool fill_set_elements);

    /**
      * Create a Set from DAG Expr, used when processing DAG Request
      */
    std::vector<const tipb::Expr *> createFromDAGExpr(const DataTypes & types, const tipb::Expr & expr, bool fill_set_elements);

    /** Create a Set from stream.
      * Call setHeader, then call insertFromBlock for each block.
      */
    void setHeader(const Block & header);

    /// Returns false, if some limit was exceeded and no need to insert more data.
    bool insertFromBlock(const Block & block, bool fill_set_elements);

    /** For columns of 'block', check belonging of corresponding rows to the set.
      * Return UInt8 column with the result.
      */
    ColumnPtr execute(const Block & block, bool negative) const;

    size_t getTotalRowCount() const { return data.getTotalRowCount(); }
    size_t getTotalByteCount() const { return data.getTotalByteCount(); }

    const DataTypes & getDataTypes() const { return data_types; }

    SetElements & getSetElements() { return *set_elements.get(); }

    void setContainsNullValue(bool contains_null_value_) { contains_null_value = contains_null_value_; }
    bool containsNullValue() const { return contains_null_value; }

    void setCollators(TiDB::TiDBCollators & collators_) { collators = collators_; }

private:
    size_t keys_size;
    Sizes key_sizes;

    SetVariants data;

    /** How IN works with Nullable types.
      *
      * For simplicity reasons, all NULL values and any tuples with at least one NULL element are ignored in the Set.
      * And for left hand side values, that are NULLs or contain any NULLs, we return 0 (means that element is not in Set).
      *
      * If we want more standard compliant behaviour, we must return NULL
      *  if lhs is NULL and set is not empty or if lhs is not in set, but set contains at least one NULL.
      * It is more complicated with tuples.
      * For example,
      *      (1, NULL, 2) IN ((1, NULL, 3)) must return 0,
      *  but (1, NULL, 2) IN ((1, 1111, 2)) must return NULL.
      *
      * We have not implemented such sophisticated behaviour.
      */

    /** The data types from which the set was created.
      * When checking for belonging to a set, the types of columns to be checked must match with them.
      */
    DataTypes data_types;

    Poco::Logger * log;

    /// Limitations on the maximum size of the set
    SizeLimits limits;

    bool contains_null_value = false;

    /// If in the left part columns contains the same types as the elements of the set.
    void executeOrdinary(
        const ColumnRawPtrs & key_columns,
        ColumnUInt8::Container & vec_res,
        bool negative,
        const PaddedPODArray<UInt8> * null_map) const;

    /// Vector of elements of `Set`.
    /// It is necessary for the index to work on the primary key in the IN statement.
    SetElementsPtr set_elements;

    /** Protects work with the set in the functions `insertFromBlock` and `execute`.
      * These functions can be called simultaneously from different threads only when using StorageSet,
      *  and StorageSet calls only these two functions.
      * Therefore, the rest of the functions for working with set are not protected.
      */
    mutable std::shared_mutex rwlock;

    TiDB::TiDBCollators collators;

    template <typename Method>
    void insertFromBlockImpl(
        Method & method,
        const ColumnRawPtrs & key_columns,
        size_t rows,
        SetVariants & variants,
        ConstNullMapPtr null_map);

    template <typename Method, bool has_null_map>
    void insertFromBlockImplCase(
        Method & method,
        const ColumnRawPtrs & key_columns,
        size_t rows,
        SetVariants & variants,
        ConstNullMapPtr null_map);

    template <typename Method>
    void executeImpl(
        Method & method,
        const ColumnRawPtrs & key_columns,
        ColumnUInt8::Container & vec_res,
        bool negative,
        size_t rows,
        ConstNullMapPtr null_map) const;

    template <typename Method, bool has_null_map>
    void executeImplCase(
        Method & method,
        const ColumnRawPtrs & key_columns,
        ColumnUInt8::Container & vec_res,
        bool negative,
        size_t rows,
        ConstNullMapPtr null_map) const;
};

using SetPtr = std::shared_ptr<Set>;
using ConstSetPtr = std::shared_ptr<const Set>;
using Sets = std::vector<SetPtr>;

class IFunction;
using FunctionPtr = std::shared_ptr<IFunction>;

/// Class for mayBeTrueInRange function.
class MergeTreeSetIndex
{
public:
    /** Mapping for tuple positions from Set::set_elements to
      * position of pk index and data type of this pk column
      * and functions chain applied to this column.
      */
    struct KeyTuplePositionMapping
    {
        size_t tuple_index;
        size_t key_index;
        std::vector<FunctionBasePtr> functions;
    };

    MergeTreeSetIndex(const SetElements & set_elements, std::vector<KeyTuplePositionMapping> && indexes_mapping_);

    size_t size() const { return ordered_set.size(); }

    BoolMask mayBeTrueInRange(const std::vector<Range> & key_ranges, const DataTypes & data_types);

private:
    using OrderedTuples = std::vector<std::vector<FieldWithInfinity>>;
    OrderedTuples ordered_set;

    std::vector<KeyTuplePositionMapping> indexes_mapping;
};

} // namespace DB
