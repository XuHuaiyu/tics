#include <Common/LogWithPrefix.h>
#include <Common/TiFlashException.h>
#include <DataStreams/IProfilingBlockInputStream.h>
#include <Flash/Coprocessor/ArrowChunkCodec.h>
#include <Flash/Coprocessor/CHBlockChunkCodec.h>
#include <Flash/Coprocessor/DefaultChunkCodec.h>
#include <Flash/Coprocessor/StreamingDAGResponseWriter.h>
#include <Flash/Mpp/MPPTunnelSet.h>
#include <Interpreters/AggregationCommon.h>

namespace DB
{
namespace ErrorCodes
{
extern const int UNSUPPORTED_PARAMETER;
extern const int LOGICAL_ERROR;
} // namespace ErrorCodes

inline void serializeToPacket(mpp::MPPDataPacket & packet, const tipb::SelectResponse & response)
{
    if (!response.SerializeToString(packet.mutable_data()))
        throw Exception(fmt::format("Fail to serialize response, response size: {}", response.ByteSizeLong()));
}

template <class StreamWriterPtr>
StreamingDAGResponseWriter<StreamWriterPtr>::StreamingDAGResponseWriter(
    StreamWriterPtr writer_,
    std::vector<Int64> partition_col_ids_,
    TiDB::TiDBCollators collators_,
    tipb::ExchangeType exchange_type_,
    Int64 records_per_chunk_,
    Int64 batch_send_min_limit_,
    tipb::EncodeType encode_type_,
    std::vector<tipb::FieldType> result_field_types_,
    DAGContext & dag_context_,
    const LogWithPrefixPtr & log_)
    : DAGResponseWriter(records_per_chunk_, encode_type_, result_field_types_, dag_context_)
    , batch_send_min_limit(batch_send_min_limit_)
    , exchange_type(exchange_type_)
    , writer(writer_)
    , partition_col_ids(std::move(partition_col_ids_))
    , collators(std::move(collators_))
{
    log = log_ != nullptr ? log_ : std::make_shared<LogWithPrefix>(&Poco::Logger::get("StreamingDAGResponseWriter"), "");

    rows_in_blocks = 0;
    partition_num = writer_->getPartitionNum();
}

template <class StreamWriterPtr>
void StreamingDAGResponseWriter<StreamWriterPtr>::finishWrite()
{
    batchWrite<true>();
}

template <class StreamWriterPtr>
void StreamingDAGResponseWriter<StreamWriterPtr>::write(const Block & block)
{
    if (block.columns() != result_field_types.size())
        throw TiFlashException("Output column size mismatch with field type size", Errors::Coprocessor::Internal);
    size_t rows = block.rows();
    rows_in_blocks += rows;
    if (rows > 0)
    {
        blocks.push_back(block);
    }
    if ((Int64)rows_in_blocks > (encode_type == tipb::EncodeType::TypeCHBlock ? batch_send_min_limit : records_per_chunk - 1))
    {
        batchWrite<false>();
    }
}

template <class StreamWriterPtr>
template <bool for_last_response>
void StreamingDAGResponseWriter<StreamWriterPtr>::encodeThenWriteBlocks(
    const std::vector<Block> & input_blocks,
    tipb::SelectResponse & response) const
{
    std::unique_ptr<ChunkCodecStream> chunk_codec_stream = nullptr;
    if (encode_type == tipb::EncodeType::TypeDefault)
    {
        chunk_codec_stream = std::make_unique<DefaultChunkCodec>()->newCodecStream(result_field_types);
    }
    else if (encode_type == tipb::EncodeType::TypeChunk)
    {
        chunk_codec_stream = std::make_unique<ArrowChunkCodec>()->newCodecStream(result_field_types);
    }
    else if (encode_type == tipb::EncodeType::TypeCHBlock)
    {
        chunk_codec_stream = std::make_unique<CHBlockChunkCodec>()->newCodecStream(result_field_types);
    }

    if (encode_type == tipb::EncodeType::TypeCHBlock)
    {
        if (dag_context.isMPPTask()) /// broadcast data among TiFlash nodes in MPP
        {
            mpp::MPPDataPacket packet;
            if constexpr (for_last_response)
            {
                serializeToPacket(packet, response);
            }
            if (input_blocks.empty())
            {
                if constexpr (for_last_response)
                {
                    writer->write(packet);
                }
                return;
            }
            for (auto & block : input_blocks)
            {
                chunk_codec_stream->encode(block, 0, block.rows());
                packet.add_chunks(chunk_codec_stream->getString());
                chunk_codec_stream->clear();
            }
            writer->write(packet);
        }
        else /// passthrough data to a non-TiFlash node, like sending data to TiSpark
        {
            response.set_encode_type(encode_type);
            if (input_blocks.empty())
            {
                if constexpr (for_last_response)
                {
                    writer->write(response);
                }
                return;
            }
            for (auto & block : input_blocks)
            {
                chunk_codec_stream->encode(block, 0, block.rows());
                auto dag_chunk = response.add_chunks();
                dag_chunk->set_rows_data(chunk_codec_stream->getString());
                chunk_codec_stream->clear();
            }
            writer->write(response);
        }
    }
    else /// passthrough data to a TiDB node
    {
        response.set_encode_type(encode_type);
        if (input_blocks.empty())
        {
            if constexpr (for_last_response)
            {
                writer->write(response);
            }
            return;
        }

        Int64 current_records_num = 0;
        for (auto & block : input_blocks)
        {
            size_t rows = block.rows();
            for (size_t row_index = 0; row_index < rows;)
            {
                if (current_records_num >= records_per_chunk)
                {
                    auto dag_chunk = response.add_chunks();
                    dag_chunk->set_rows_data(chunk_codec_stream->getString());
                    chunk_codec_stream->clear();
                    current_records_num = 0;
                }
                const size_t upper = std::min(row_index + (records_per_chunk - current_records_num), rows);
                chunk_codec_stream->encode(block, row_index, upper);
                current_records_num += (upper - row_index);
                row_index = upper;
            }
        }

        if (current_records_num > 0)
        {
            auto dag_chunk = response.add_chunks();
            dag_chunk->set_rows_data(chunk_codec_stream->getString());
            chunk_codec_stream->clear();
        }
        writer->write(response);
    }
}

/// hash exchanging data among only TiFlash nodes.
template <class StreamWriterPtr>
template <bool for_last_response>
void StreamingDAGResponseWriter<StreamWriterPtr>::partitionAndEncodeThenWriteBlocks(
    std::vector<Block> & input_blocks,
    tipb::SelectResponse & response) const
{
    std::vector<std::unique_ptr<ChunkCodecStream>> chunk_codec_stream(partition_num);
    std::vector<mpp::MPPDataPacket> packet(partition_num);

    std::vector<size_t> responses_row_count(partition_num);
    for (auto i = 0; i < partition_num; ++i)
    {
        if (encode_type == tipb::EncodeType::TypeDefault)
        {
            chunk_codec_stream[i] = DefaultChunkCodec().newCodecStream(result_field_types);
        }
        else if (encode_type == tipb::EncodeType::TypeChunk)
        {
            chunk_codec_stream[i] = ArrowChunkCodec().newCodecStream(result_field_types);
        }
        else if (encode_type == tipb::EncodeType::TypeCHBlock)
        {
            chunk_codec_stream[i] = CHBlockChunkCodec().newCodecStream(result_field_types);
        }
        if constexpr (for_last_response)
        {
            /// Sending the response to only one node, default the first one.
            if (i == 0)
                serializeToPacket(packet[i], response);
        }
    }
    if (input_blocks.empty())
    {
        if constexpr (for_last_response)
        {
            for (auto part_id = 0; part_id < partition_num; ++part_id)
            {
                writer->write(packet[part_id], part_id);
            }
        }
        return;
    }

    // partition tuples in blocks
    // 1) compute partition id
    // 2) partition each row
    // 3) encode each chunk and send it
    std::vector<String> partition_key_containers(collators.size());
    for (auto & block : input_blocks)
    {
        std::vector<Block> dest_blocks(partition_num);
        std::vector<MutableColumns> dest_tbl_cols(partition_num);

        for (size_t i = 0; i < block.columns(); ++i)
        {
            if (ColumnPtr converted = block.getByPosition(i).column->convertToFullColumnIfConst())
            {
                block.getByPosition(i).column = converted;
            }
        }

        for (auto i = 0; i < partition_num; ++i)
        {
            dest_tbl_cols[i] = block.cloneEmptyColumns();
            dest_blocks[i] = block.cloneEmpty();
        }

        size_t rows = block.rows();
        WeakHash32 hash(rows);

        // get hash values by all partition key columns
        for (size_t i = 0; i < partition_col_ids.size(); i++)
        {
            block.getByPosition(partition_col_ids[i]).column->updateWeakHash32(hash, collators[i], partition_key_containers[i]);
        }
        const auto & hash_data = hash.getData();

        // partition each row
        IColumn::Selector selector(rows);
        for (size_t row = 0; row < rows; ++row)
        {
            /// Row from interval [(2^32 / partition_num) * i, (2^32 / partition_num) * (i + 1)) goes to bucket with number i.
            selector[row] = hash_data[row]; /// [0, 2^32)
            selector[row] *= partition_num; /// [0, partition_num * 2^32), selector stores 64 bit values.
            selector[row] >>= 32u; /// [0, partition_num)
        }

        for (size_t col_id = 0; col_id < block.columns(); ++col_id)
        {
            // Scatter columns to different partitions
            auto scattered_columns = block.getByPosition(col_id).column->scatter(partition_num, selector);
            for (size_t part_id = 0; part_id < partition_num; ++part_id)
            {
                dest_tbl_cols[part_id][col_id] = std::move(scattered_columns[part_id]);
            }
        }

        // serialize each partitioned block and write it to its destination
        for (auto part_id = 0; part_id < partition_num; ++part_id)
        {
            dest_blocks[part_id].setColumns(std::move(dest_tbl_cols[part_id]));
            responses_row_count[part_id] += dest_blocks[part_id].rows();
            chunk_codec_stream[part_id]->encode(dest_blocks[part_id], 0, dest_blocks[part_id].rows());
            packet[part_id].add_chunks(chunk_codec_stream[part_id]->getString());
            chunk_codec_stream[part_id]->clear();
        }
    }

    for (auto part_id = 0; part_id < partition_num; ++part_id)
    {
        if constexpr (for_last_response)
        {
            writer->write(packet[part_id], part_id);
        }
        else
        {
            if (responses_row_count[part_id] > 0)
                writer->write(packet[part_id], part_id);
        }
    }
}

template <class StreamWriterPtr>
template <bool for_last_response>
void StreamingDAGResponseWriter<StreamWriterPtr>::batchWrite()
{
    tipb::SelectResponse response;
    if constexpr (for_last_response)
        addExecuteSummaries(response, !dag_context.isMPPTask() || dag_context.isRootMPPTask());
    if (exchange_type == tipb::ExchangeType::Hash)
    {
        partitionAndEncodeThenWriteBlocks<for_last_response>(blocks, response);
    }
    else
    {
        encodeThenWriteBlocks<for_last_response>(blocks, response);
    }
    blocks.clear();
    rows_in_blocks = 0;
}

template class StreamingDAGResponseWriter<StreamWriterPtr>;
template class StreamingDAGResponseWriter<MPPTunnelSetPtr>;

} // namespace DB
