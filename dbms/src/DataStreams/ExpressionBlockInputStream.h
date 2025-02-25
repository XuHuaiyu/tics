#pragma once

#include <DataStreams/IProfilingBlockInputStream.h>


namespace DB
{
class ExpressionActions;

/** Executes a certain expression over the block.
  * The expression consists of column identifiers from the block, constants, common functions.
  * For example: hits * 2 + 3, url LIKE '%yandex%'
  * The expression processes each row independently of the others.
  */
class ExpressionBlockInputStream : public IProfilingBlockInputStream
{
private:
    using ExpressionActionsPtr = std::shared_ptr<ExpressionActions>;

public:
    ExpressionBlockInputStream(
        const BlockInputStreamPtr & input,
        const ExpressionActionsPtr & expression_,
        const LogWithPrefixPtr & log);

    String getName() const override;
    Block getTotals() override;
    Block getHeader() const override;

protected:
    Block readImpl() override;

private:
    ExpressionActionsPtr expression;
    const LogWithPrefixPtr log;
};

} // namespace DB
