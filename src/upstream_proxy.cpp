#include <eventedge/upstream_proxy.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/beast/http.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eventedge {
namespace {

void remove_hop_by_hop_headers(http::fields& headers) {
    std::vector<std::string> connection_tokens;
    for (const auto& header : headers) {
        if (header.name() != http::field::connection) {
            continue;
        }

        const std::string_view value{header.value().data(), header.value().size()};
        std::size_t start = 0;
        while (start < value.size()) {
            const auto end = value.find(',', start);
            const auto token_end = end == std::string_view::npos ? value.size() : end;
            const auto token = value.substr(start, token_end - start);
            const auto first = token.find_first_not_of(" \t");
            const auto last = token.find_last_not_of(" \t");
            if (first != std::string_view::npos) {
                connection_tokens.emplace_back(token.substr(first, last - first + 1));
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
    }

    for (const auto& token : connection_tokens) {
        headers.erase(boost::beast::string_view{token.data(), token.size()});
    }
    headers.erase(http::field::connection);
    headers.erase(http::field::keep_alive);
    headers.erase(http::field::proxy_authenticate);
    headers.erase(http::field::proxy_authorization);
    headers.erase(http::field::te);
    headers.erase(http::field::trailer);
    headers.erase(http::field::transfer_encoding);
    headers.erase(http::field::upgrade);
}

std::string upstream_host_header(const UpstreamEndpoint& upstream) {
    if (upstream.host.find(':') != std::string::npos) {
        return '[' + upstream.host + "]:" + std::to_string(upstream.port);
    }
    return upstream.host + ':' + std::to_string(upstream.port);
}

}  // namespace

UpstreamProxy::UpstreamProxy(boost::asio::any_io_executor executor,
                             UpstreamEndpoint upstream,
                             HttpRequest request,
                             CompletionHandler completion_handler,
                             ProxyTimeouts timeouts)
    : resolver_(executor),
      stream_(executor),
      upstream_(std::move(upstream)),
      request_(std::move(request)),
      completion_handler_(std::move(completion_handler)),
      timeouts_(timeouts),
      executor_(executor),
      client_version_(request_.version()),
      client_keep_alive_(request_.keep_alive()) {}

void UpstreamProxy::run() {
    remove_hop_by_hop_headers(request_.base());
    request_.set(http::field::host, upstream_host_header(upstream_));
    request_.version(11);
    request_.keep_alive(false);
    request_.set(http::field::connection, "close");
    request_.prepare_payload();

    resolver_.async_resolve(upstream_.host, std::to_string(upstream_.port),
                            boost::asio::bind_executor(executor_, [self = shared_from_this()](boost::beast::error_code error,
                                                        boost::asio::ip::tcp::resolver::results_type results) {
                                self->on_resolve(error, std::move(results));
                            }));
}

void UpstreamProxy::on_resolve(boost::beast::error_code error,
                               boost::asio::ip::tcp::resolver::results_type results) {
    if (error) {
        return fail(error, "resolve");
    }

    stream_.expires_after(timeouts_.connect);
    stream_.async_connect(results,
                          boost::asio::bind_executor(executor_, [self = shared_from_this()](boost::beast::error_code connect_error,
                                                      const boost::asio::ip::tcp::resolver::results_type::endpoint_type& endpoint) {
                              self->on_connect(connect_error, endpoint);
                          }));
}

void UpstreamProxy::on_connect(
    boost::beast::error_code error,
    const boost::asio::ip::tcp::resolver::results_type::endpoint_type&) {
    if (error) {
        return fail(error, "connect");
    }

    stream_.expires_after(timeouts_.write);
    http::async_write(stream_, request_,
                      boost::asio::bind_executor(executor_, [self = shared_from_this()](boost::beast::error_code write_error,
                                                  std::size_t bytes_transferred) {
                          self->on_write(write_error, bytes_transferred);
                      }));
}

void UpstreamProxy::on_write(boost::beast::error_code error, std::size_t) {
    if (error) {
        return fail(error, "write");
    }

    stream_.expires_after(timeouts_.read);
    http::async_read(stream_, buffer_, response_,
                     boost::asio::bind_executor(executor_, [self = shared_from_this()](boost::beast::error_code read_error,
                                                 std::size_t bytes_transferred) {
                         self->on_read(read_error, bytes_transferred);
                     }));
}

void UpstreamProxy::on_read(boost::beast::error_code error, std::size_t) {
    if (error) {
        return fail(error, "read");
    }

    remove_hop_by_hop_headers(response_.base());
    response_.version(client_version_);
    response_.keep_alive(client_keep_alive_);
    response_.prepare_payload();

    boost::beast::error_code shutdown_error;
    stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, shutdown_error);
    complete(std::move(response_));
}

void UpstreamProxy::fail(const boost::beast::error_code& error, const char* operation) {
    if (completed_) {
        return;
    }
    std::cerr << "Upstream " << operation << " error: " << error.message() << '\n';
    if (error == boost::beast::error::timeout) {
        complete(make_gateway_timeout_response(request_));
    } else {
        complete(make_bad_gateway_response(request_));
    }
}

void UpstreamProxy::complete(HttpResponse response) {
    if (completed_) {
        return;
    }
    completed_ = true;
    resolver_.cancel();
    boost::beast::error_code close_error;
    stream_.socket().close(close_error);
    if (completion_handler_) {
        auto completion_handler = std::move(completion_handler_);
        completion_handler(std::move(response));
    }
}

}  // namespace eventedge
