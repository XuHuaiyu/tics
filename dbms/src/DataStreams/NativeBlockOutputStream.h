#pragma once

#include <Core/Types.h>
#include <DataStreams/IBlockOutputStream.h>
#include <DataTypes/IDataType.h>
#include <IO/CompressedWriteBuffer.h>

namespace DB
{
class WriteBuffer;


/** Serializes the stream of blocks in their native binary format (with names and column types).
  * Designed for communication between servers.
  *
  * A stream can be specified to write the index. The index contains offsets to each part of each column.
  * If an `append` is made to an existing file, and you need to write the index, then specify `initial_size_of_file`.
  */
class NativeBlockOutputStream : public IBlockOutputStream
{
public:
    /** If non-zero client_revision is specified, additional block information can be written.
      */
    NativeBlockOutputStream(
        WriteBuffer & ostr_,
        UInt64 client_revision_,
        const Block & header_,
        WriteBuffer * index_ostr_ = nullptr,
        size_t initial_size_of_file_ = 0);

    Block getHeader() const override { return header; }
    void write(const Block & block) override;
    void flush() override;

    static void writeData(const IDataType & type, const ColumnPtr & column, WriteBuffer & ostr, size_t offset, size_t limit);

    String getContentType() const override { return "application/octet-stream"; }

private:
    WriteBuffer & ostr;
    UInt64 client_revision;
    Block header;
    WriteBuffer * index_ostr;
    size_t initial_size_of_file; /// The initial size of the data file, if `append` done. Used for the index.
    /// If you need to write index, then `ostr` must be a CompressedWriteBuffer.
    CompressedWriteBuffer<> * ostr_concrete = nullptr;
};

} // namespace DB
