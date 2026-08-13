#include <eventedge/http_session.hpp>
#include <eventedge/upstream_proxy.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <iostream>
#include <utility>

namespace eventedge {
namespace net = boost::asio;
using tcp = net::ip::tcp;

HttpSession::HttpSession(tcp::socket&& socket, UpstreamConfig upstream)
    : stream_(std::move(socket)), upstream_(std::move(upstream)) {}

void HttpSession::run() {
    do_read();
}

void HttpSession::do_read() {
    request_ = {};
    stream_.expires_after(std::chrono::seconds(30));

    http::async_read(stream_, buffer_, request_,
                     net::bind_executor(stream_.get_executor(), [self = shared_from_this()](
                                                                    beast::error_code error,
                                                                    std::size_t bytes_transferred) {
                         self->on_read(error, bytes_transferred);
                     }));
}

void HttpSession::on_read(beast::error_code error, std::size_t) {
    if (error == http::error::end_of_stream) {
        return close();
    }

    if (error) {
        if (error != net::error::operation_aborted) {
            std::cerr << "Read error: " << error.message() << '\n';
        }
        return;
    }

    if (is_health_request(request_)) {
        return write_response(handle_request(request_));
    }

    std::make_shared<UpstreamProxy>(
        stream_.get_executor(), upstream_, std::move(request_),
        [self = shared_from_this()](HttpResponse response) { self->write_response(std::move(response)); })
        ->run();
}

void HttpSession::write_response(HttpResponse response) {
    auto response_to_write = std::make_shared<HttpResponse>(std::move(response));
    const bool close_after_write = response_to_write->need_eof();

    http::async_write(stream_, *response_to_write,
                      net::bind_executor(stream_.get_executor(), [self = shared_from_this(), response_to_write, close_after_write](
                          beast::error_code write_error, std::size_t bytes_transferred) {
                          self->on_write(close_after_write, write_error, bytes_transferred);
                      }));
}

void HttpSession::on_write(bool close_after_write, beast::error_code error, std::size_t) {
    if (error) {
        if (error != net::error::operation_aborted) {
            std::cerr << "Write error: " << error.message() << '\n';
        }
        return;
    }

    if (close_after_write) {
        return close();
    }

    do_read();
}

void HttpSession::close() {
    beast::error_code error;
    stream_.socket().shutdown(tcp::socket::shutdown_send, error);
}

}  // namespace eventedge
