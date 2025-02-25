#pragma once

#include <Core/SortDescription.h>
#include <DataStreams/IProfilingBlockInputStream.h>
#include <Flash/Mpp/getMPPTaskLog.h>


namespace DB
{
/** Sorts each block individually by the values of the specified columns.
  * At the moment, not very optimal algorithm is used.
  */
class PartialSortingBlockInputStream : public IProfilingBlockInputStream
{
public:
    /// limit - if not 0, then you can sort each block not completely, but only `limit` first rows by order.
    PartialSortingBlockInputStream(
        const BlockInputStreamPtr & input_,
        SortDescription & description_,
        const LogWithPrefixPtr & log_,
        size_t limit_ = 0)
        : description(description_)
        , limit(limit_)
        , log(getMPPTaskLog(log_, getName()))
    {
        children.push_back(input_);
    }

    String getName() const override { return "PartialSorting"; }

    bool isGroupedOutput() const override { return true; }
    bool isSortedOutput() const override { return true; }
    const SortDescription & getSortDescription() const override { return description; }

    Block getHeader() const override { return children.at(0)->getHeader(); }

protected:
    Block readImpl() override;

private:
    SortDescription description;
    size_t limit;
    LogWithPrefixPtr log;
};

} // namespace DB
