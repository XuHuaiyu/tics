#pragma once

#include <Common/Exception.h>
#include <IO/BufferWithOwnMemory.h>
#include <IO/WriteBuffer.h>

#include <iostream>


namespace DB
{
namespace ErrorCodes
{
extern const int CANNOT_WRITE_TO_OSTREAM;
}

class WriteBufferFromOStream : public BufferWithOwnMemory<WriteBuffer>
{
private:
    std::ostream & ostr;

    void nextImpl() override
    {
        if (!offset())
            return;

        ostr.write(working_buffer.begin(), offset());
        ostr.flush();

        if (!ostr.good())
            throw Exception("Cannot write to ostream", ErrorCodes::CANNOT_WRITE_TO_OSTREAM);
    }

public:
    WriteBufferFromOStream(
        std::ostream & ostr_,
        size_t size = DBMS_DEFAULT_BUFFER_SIZE,
        char * existing_memory = nullptr,
        size_t alignment = 0)
        : BufferWithOwnMemory<WriteBuffer>(size, existing_memory, alignment)
        , ostr(ostr_)
    {}

    ~WriteBufferFromOStream() override
    {
        try
        {
            next();
        }
        catch (...)
        {
            tryLogCurrentException(__PRETTY_FUNCTION__);
        }
    }
};

} // namespace DB
