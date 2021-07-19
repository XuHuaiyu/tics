#include <Flash/Mpp/ExchangeReceiver.h>

namespace pingcap
{
namespace kv
{

template <>
struct RpcTypeTraits<::mpp::EstablishMPPConnectionRequest>
{
    using RequestType = ::mpp::EstablishMPPConnectionRequest;
    using ResultType = ::mpp::MPPDataPacket;
    static std::unique_ptr<::grpc::ClientReader<::mpp::MPPDataPacket>> doRPCCall(
        grpc::ClientContext * context, std::shared_ptr<KvConnClient> client, const RequestType & req)
    {
        return client->stub->EstablishMPPConnection(context, req);
    }
    static std::unique_ptr<::grpc::ClientAsyncReader<::mpp::MPPDataPacket>> doAsyncRPCCall(grpc::ClientContext * context,
        std::shared_ptr<KvConnClient> client, const RequestType & req, grpc::CompletionQueue & cq, void * call)
    {
        return client->stub->AsyncEstablishMPPConnection(context, req, &cq, call);
    }
};

} // namespace kv
} // namespace pingcap

namespace DB
{

void ExchangeReceiver::setUpConnection()
{
    for (int index = 0; index < pb_exchange_receiver.encoded_task_meta_size(); index++)
    {
        auto & meta = pb_exchange_receiver.encoded_task_meta(index);
        std::thread t(&ExchangeReceiver::ReadLoop, this, std::ref(meta), index);
        live_connections++;
        workers.push_back(std::move(t));
    }
}

void ExchangeReceiver::ReadLoop(const String & meta_raw, size_t source_index)
{
    bool meet_error = false;
    String local_err_msg;
    try
    {
        auto sender_task = new mpp::TaskMeta();
        sender_task->ParseFromString(meta_raw);
        auto req = std::make_shared<mpp::EstablishMPPConnectionRequest>();
        req->set_allocated_receiver_meta(new mpp::TaskMeta(task_meta));
        req->set_allocated_sender_meta(sender_task);
        LOG_DEBUG(log, "begin start and read : " << req->DebugString());
        pingcap::kv::RpcCall<mpp::EstablishMPPConnectionRequest> call(req);
        grpc::ClientContext client_context;
        auto reader = cluster->rpc_client->sendStreamRequest(req->sender_meta().address(), &client_context, call);
        reader->WaitForInitialMetadata();
        // Block until the next result is available in the completion queue "cq".
        mpp::MPPDataPacket packet;
        String req_info = "tunnel" + std::to_string(sender_task->task_id()) + "+" + std::to_string(task_meta.task_id());
        for (;;)
        {
            LOG_TRACE(log, "begin next ");
            bool success = reader->Read(&packet);
            if (!success)
                break;
            if (packet.has_error())
            {
                throw Exception("Exchange receiver meet error : " + packet.error().msg());
            }
            if (!decodePacket(packet, source_index, req_info))
            {
                meet_error = true;
                local_err_msg = "Decode packet meet error";
                LOG_WARNING(log, "Decode packet meet error, exit from ReadLoop");
                break;
            }
        }
        LOG_DEBUG(log, "finish read : " << req->DebugString());
    }
    catch (Exception & e)
    {
        meet_error = true;
        local_err_msg = e.message();
    }
    catch (std::exception & e)
    {
        meet_error = true;
        local_err_msg = e.what();
    }
    catch (...)
    {
        meet_error = true;
        local_err_msg = "fatal error";
    }
    std::lock_guard<std::mutex> lock(mu);
    live_connections--;
    if (meet_error && state == NORMAL)
        state = ERROR;
    if (meet_error && err_msg.empty())
        err_msg = local_err_msg;
    cv.notify_all();
    LOG_DEBUG(log, "read thread end!!! live connections: " << std::to_string(live_connections));
}

} // namespace DB
