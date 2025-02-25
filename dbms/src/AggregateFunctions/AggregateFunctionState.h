
#pragma once

#include <DataTypes/DataTypeAggregateFunction.h>
#include <AggregateFunctions/IAggregateFunction.h>
#include <Columns/ColumnAggregateFunction.h>


namespace DB
{


/** Not an aggregate function, but an adapter of aggregate functions,
  * Aggregate functions with the `State` suffix differ from the corresponding ones in that their states are not finalized.
  * Return type - DataTypeAggregateFunction.
  */

class AggregateFunctionState final : public IAggregateFunctionHelper<AggregateFunctionState>
{
private:
    AggregateFunctionPtr nested_func;
    DataTypes arguments;
    Array params;

public:
    AggregateFunctionState(AggregateFunctionPtr nested, const DataTypes & arguments, const Array & params)
        : nested_func(nested), arguments(arguments), params(params) {}

    String getName() const override
    {
        return nested_func->getName() + "State";
    }

    DataTypePtr getReturnType() const override;

    void create(AggregateDataPtr __restrict place) const override
    {
        nested_func->create(place);
    }

    void destroy(AggregateDataPtr __restrict place) const noexcept override
    {
        nested_func->destroy(place);
    }

    bool hasTrivialDestructor() const override
    {
        return nested_func->hasTrivialDestructor();
    }

    size_t sizeOfData() const override
    {
        return nested_func->sizeOfData();
    }

    size_t alignOfData() const override
    {
        return nested_func->alignOfData();
    }

    void add(AggregateDataPtr __restrict place, const IColumn ** columns, size_t row_num, Arena * arena) const override
    {
        nested_func->add(place, columns, row_num, arena);
    }

    void merge(AggregateDataPtr __restrict place, ConstAggregateDataPtr rhs, Arena * arena) const override
    {
        nested_func->merge(place, rhs, arena);
    }

    void serialize(ConstAggregateDataPtr __restrict place, WriteBuffer & buf) const override
    {
        nested_func->serialize(place, buf);
    }

    void deserialize(AggregateDataPtr __restrict place, ReadBuffer & buf, Arena * arena) const override
    {
        nested_func->deserialize(place, buf, arena);
    }

    void insertResultInto(ConstAggregateDataPtr __restrict place, IColumn & to, Arena *) const override
    {
        static_cast<ColumnAggregateFunction &>(to).getData().push_back(const_cast<AggregateDataPtr>(place));
    }

    /// Aggregate function or aggregate function state.
    bool isState() const override { return true; }

    bool allocatesMemoryInArena() const override
    {
        return nested_func->allocatesMemoryInArena();
    }

    AggregateFunctionPtr getNestedFunction() const { return nested_func; }

    const char * getHeaderFilePath() const override { return __FILE__; }
};

}
