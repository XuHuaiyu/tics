#pragma once
#include <Common/PODArray.h>
#include <common/types.h>

namespace DB
{
/// It's a class which represents the result of weak and fast hash function per row in column.
/// It's usually hardware accelerated CRC32-C.
/// Has function result may be combined to calculate hash for tuples.
///
/// The main purpose why this class needed is to support data initialization. Initially, every bit is 1.
class WeakHash32
{
public:
    using Container = PaddedPODArray<UInt32>;

    static constexpr UInt32 initial_hash = ~UInt32(0);

    explicit WeakHash32(size_t size)
        : data(size, initial_hash)
    {}
    WeakHash32(const WeakHash32 & other) { data.assign(other.data); }

    void reset(size_t size) { data.assign(size, initial_hash); }

    const Container & getData() const { return data; }
    Container & getData() { return data; }

private:
    PaddedPODArray<UInt32> data;
};

} // namespace DB
