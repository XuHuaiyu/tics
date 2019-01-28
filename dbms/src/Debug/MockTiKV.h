#pragma once

#include <Storages/Transaction/RegionMeta.h>

namespace DB
{

class MockTiKV : public ext::singleton<MockTiKV>
{
    friend class ext::singleton<MockTiKV>;

public:
    UInt64 getRaftIndex(RegionID region_id)
    {
        std::lock_guard lock(mutex);
        auto it = raft_index.find(region_id);
        if (it == raft_index.end())
            it = raft_index.emplace_hint(it, region_id, RAFT_INIT_LOG_INDEX);
        ++(it->second);
        return it->second;
    }

    UInt64 getRaftTerm(RegionID region_id)
    {
        std::lock_guard lock(mutex);
        auto it = raft_term.find(region_id);
        if (it == raft_term.end())
            it = raft_term.emplace_hint(it, region_id, RAFT_INIT_LOG_TERM);
        return it->second;
    }

private:
    std::mutex mutex;

    std::unordered_map<RegionID, UInt64> raft_index;
    std::unordered_map<RegionID, UInt64> raft_term;
};

} // namespace DB
