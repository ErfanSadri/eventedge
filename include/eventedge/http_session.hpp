#pragma once

#include <eventedge/http_handler.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <cstddef>
#include <memory>

namespace eventedge {

namespace beast = boost::beast;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(boost::asio::ip::tcp::socket&& socket);

    void run();

private:
    void do_read();
    void on_read(beast::error_code error, std::size_t bytes_transferred);
    void on_write(bool close, beast::error_code error, std::size_t bytes_transferred);
    void close();

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
};

}  // namespace eventedge
