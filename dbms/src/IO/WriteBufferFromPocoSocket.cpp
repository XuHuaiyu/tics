#include <Common/Exception.h>
#include <Common/NetException.h>
#include <IO/WriteBufferFromPocoSocket.h>
#include <Poco/Net/NetException.h>


namespace DB
{
namespace ErrorCodes
{
extern const int NETWORK_ERROR;
extern const int SOCKET_TIMEOUT;
extern const int CANNOT_WRITE_TO_SOCKET;
} // namespace ErrorCodes


void WriteBufferFromPocoSocket::nextImpl()
{
    if (!offset())
        return;

    size_t bytes_written = 0;
    while (bytes_written < offset())
    {
        ssize_t res = 0;

        /// Add more details to exceptions.
        try
        {
            res = socket.impl()->sendBytes(working_buffer.begin() + bytes_written, offset() - bytes_written);
        }
        catch (const Poco::Net::NetException & e)
        {
            throw NetException(e.displayText() + " while writing to socket (" + peer_address.toString() + ")", ErrorCodes::NETWORK_ERROR);
        }
        catch (const Poco::TimeoutException & e)
        {
            throw NetException("Timeout exceeded while writing to socket (" + peer_address.toString() + ")", ErrorCodes::SOCKET_TIMEOUT);
        }
        catch (const Poco::IOException & e)
        {
            throw NetException(e.displayText(), " while reading from socket (" + peer_address.toString() + ")", ErrorCodes::NETWORK_ERROR);
        }

        if (res < 0)
            throw NetException("Cannot write to socket (" + peer_address.toString() + ")", ErrorCodes::CANNOT_WRITE_TO_SOCKET);
        bytes_written += res;
    }
}

WriteBufferFromPocoSocket::WriteBufferFromPocoSocket(Poco::Net::Socket & socket_, size_t buf_size)
    : BufferWithOwnMemory<WriteBuffer>(buf_size)
    , socket(socket_)
    , peer_address(socket.peerAddress())
{
}

WriteBufferFromPocoSocket::~WriteBufferFromPocoSocket()
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

} // namespace DB
