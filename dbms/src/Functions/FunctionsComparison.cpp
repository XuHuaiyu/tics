#include <Common/typeid_cast.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionsComparison.h>

namespace DB
{
void registerFunctionsComparison(FunctionFactory & factory)
{
    factory.registerFunction<FunctionEquals>();
    factory.registerFunction<FunctionNotEquals>();
    factory.registerFunction<FunctionLess>();
    factory.registerFunction<FunctionGreater>();
    factory.registerFunction<FunctionLessOrEquals>();
    factory.registerFunction<FunctionGreaterOrEquals>();
}

template <>
void FunctionComparison<EqualsOp, NameEquals>::executeTupleImpl(
    Block & block,
    size_t result,
    const ColumnsWithTypeAndName & x,
    const ColumnsWithTypeAndName & y,
    size_t tuple_size) const
{
    return executeTupleEqualityImpl<FunctionComparison<EqualsOp, NameEquals>, FunctionAnd>(block, result, x, y, tuple_size);
}

template <>
void FunctionComparison<NotEqualsOp, NameNotEquals>::executeTupleImpl(
    Block & block,
    size_t result,
    const ColumnsWithTypeAndName & x,
    const ColumnsWithTypeAndName & y,
    size_t tuple_size) const
{
    return executeTupleEqualityImpl<FunctionComparison<NotEqualsOp, NameNotEquals>, FunctionOr>(block, result, x, y, tuple_size);
}

template <>
void FunctionComparison<LessOp, NameLess>::executeTupleImpl(
    Block & block,
    size_t result,
    const ColumnsWithTypeAndName & x,
    const ColumnsWithTypeAndName & y,
    size_t tuple_size) const
{
    return executeTupleLessGreaterImpl<
        FunctionComparison<LessOp, NameLess>,
        FunctionComparison<LessOp, NameLess>>(block, result, x, y, tuple_size);
}

template <>
void FunctionComparison<GreaterOp, NameGreater>::executeTupleImpl(
    Block & block,
    size_t result,
    const ColumnsWithTypeAndName & x,
    const ColumnsWithTypeAndName & y,
    size_t tuple_size) const
{
    return executeTupleLessGreaterImpl<
        FunctionComparison<GreaterOp, NameGreater>,
        FunctionComparison<GreaterOp, NameGreater>>(block, result, x, y, tuple_size);
}

template <>
void FunctionComparison<LessOrEqualsOp, NameLessOrEquals>::executeTupleImpl(
    Block & block,
    size_t result,
    const ColumnsWithTypeAndName & x,
    const ColumnsWithTypeAndName & y,
    size_t tuple_size) const
{
    return executeTupleLessGreaterImpl<
        FunctionComparison<LessOp, NameLess>,
        FunctionComparison<LessOrEqualsOp, NameLessOrEquals>>(block, result, x, y, tuple_size);
}

template <>
void FunctionComparison<GreaterOrEqualsOp, NameGreaterOrEquals>::executeTupleImpl(
    Block & block,
    size_t result,
    const ColumnsWithTypeAndName & x,
    const ColumnsWithTypeAndName & y,
    size_t tuple_size) const
{
    return executeTupleLessGreaterImpl<
        FunctionComparison<GreaterOp, NameGreater>,
        FunctionComparison<GreaterOrEqualsOp, NameGreaterOrEquals>>(block, result, x, y, tuple_size);
}

} // namespace DB
