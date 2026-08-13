#pragma once

#include <eventedge/upstream_config.hpp>
#include <eventedge/request_coalescer.hpp>
#include <eventedge/metrics.hpp>
#include <eventedge/response_cache.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <cstdint>
#include <memory>

namespace eventedge {

namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(net::io_context& io_context,
               tcp::endpoint endpoint,
               std::shared_ptr<UpstreamPool> upstream_pool,
               std::shared_ptr<ResponseCache> response_cache,
               std::shared_ptr<RequestCoalescer> request_coalescer,
               std::shared_ptr<MetricsRegistry> metrics = nullptr);

    void run();
    void stop();

    [[nodiscard]] net::any_io_executor executor() const;
    [[nodiscard]] std::uint16_t port() const;

private:
    void do_accept();

    tcp::acceptor acceptor_;
    net::strand<net::io_context::executor_type> acceptor_strand_;
    std::shared_ptr<UpstreamPool> upstream_pool_;
    std::shared_ptr<ResponseCache> response_cache_;
    std::shared_ptr<RequestCoalescer> request_coalescer_;
    std::shared_ptr<MetricsRegistry> metrics_;
};

}  // namespace eventedge
