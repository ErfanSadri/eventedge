#pragma once

#include <eventedge/http_handler.hpp>
#include <eventedge/metrics.hpp>
#include <eventedge/request_coalescer.hpp>
#include <eventedge/upstream_config.hpp>
#include <eventedge/response_cache.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <cstddef>
#include <chrono>
#include <memory>

namespace eventedge {

namespace beast = boost::beast;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(boost::asio::ip::tcp::socket&& socket,
                std::shared_ptr<UpstreamPool> upstream_pool,
                std::shared_ptr<ResponseCache> response_cache,
                std::shared_ptr<RequestCoalescer> request_coalescer, std::shared_ptr<MetricsRegistry> metrics);

    void run();

private:
    void do_read();
    void on_read(beast::error_code error, std::size_t bytes_transferred);
    void on_write(bool close, beast::error_code error, std::size_t bytes_transferred);
    void write_response(HttpResponse response);
    void write_response_for_current_request(HttpResponse response);
    void complete_flight(const std::string& key, HttpResponse response, bool should_cache);
    [[nodiscard]] bool cacheable_request() const;
    [[nodiscard]] bool cacheable_response(const HttpResponse& response) const;
    void close();

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
    std::shared_ptr<UpstreamPool> upstream_pool_;
    std::shared_ptr<ResponseCache> response_cache_;
    std::weak_ptr<RequestCoalescer> request_coalescer_;
    std::shared_ptr<MetricsRegistry> metrics_;
    std::chrono::steady_clock::time_point request_started_;
};

}  // namespace eventedge
