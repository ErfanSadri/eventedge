#pragma once

#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <memory>

namespace eventedge {

namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(net::io_context& io_context, const tcp::endpoint& endpoint);

    void run();
    void stop();

    [[nodiscard]] std::uint16_t port() const;

private:
    void do_accept();

    tcp::acceptor acceptor_;
};

}  // namespace eventedge
