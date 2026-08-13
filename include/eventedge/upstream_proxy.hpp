#pragma once

#include <eventedge/http_handler.hpp>
#include <eventedge/upstream_config.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <functional>
#include <chrono>
#include <memory>

namespace eventedge {

struct ProxyTimeouts {
    std::chrono::milliseconds connect{std::chrono::seconds(2)};
    std::chrono::milliseconds write{std::chrono::seconds(2)};
    std::chrono::milliseconds read{std::chrono::seconds(5)};
};

class UpstreamProxy : public std::enable_shared_from_this<UpstreamProxy> {
public:
    using CompletionHandler = std::function<void(HttpResponse)>;

    UpstreamProxy(boost::asio::any_io_executor executor,
                  UpstreamEndpoint upstream,
                  HttpRequest request,
                  CompletionHandler completion_handler,
                  ProxyTimeouts timeouts = {});

    void run();

private:
    void on_resolve(boost::beast::error_code error,
                    boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(boost::beast::error_code error,
                    const boost::asio::ip::tcp::resolver::results_type::endpoint_type& endpoint);
    void on_write(boost::beast::error_code error, std::size_t bytes_transferred);
    void on_read(boost::beast::error_code error, std::size_t bytes_transferred);
    void fail(const boost::beast::error_code& error, const char* operation);
    void complete(HttpResponse response);

    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::tcp_stream stream_;
    boost::beast::flat_buffer buffer_;
    UpstreamEndpoint upstream_;
    HttpRequest request_;
    HttpResponse response_;
    CompletionHandler completion_handler_;
    ProxyTimeouts timeouts_;
    boost::asio::any_io_executor executor_;
    unsigned client_version_;
    bool client_keep_alive_;
    bool completed_{false};
};

}  // namespace eventedge
