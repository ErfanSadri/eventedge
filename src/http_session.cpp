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

HttpSession::HttpSession(tcp::socket&& socket,
                         std::shared_ptr<UpstreamPool> upstream_pool,
                         std::shared_ptr<ResponseCache> response_cache)
    : stream_(std::move(socket)), upstream_pool_(std::move(upstream_pool)), response_cache_(std::move(response_cache)) {}

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

    const auto cache_key = std::string{request_.target()};
    if (cacheable_request()) {
        if (auto cached = response_cache_->get(cache_key)) {
            cached->version(request_.version());
            cached->keep_alive(request_.keep_alive());
            return write_response(std::move(*cached));
        }
    }

    const auto upstream = upstream_pool_->select();
    if (!upstream) {
        return write_response(make_service_unavailable_response(request_));
    }

    std::make_shared<UpstreamProxy>(
        stream_.get_executor(), *upstream, std::move(request_),
        [self = shared_from_this(), cache_key, should_cache = cacheable_request()](HttpResponse response) {
            if (should_cache && self->cacheable_response(response)) {
                self->response_cache_->put(cache_key, response);
            }
            self->write_response(std::move(response));
        })
        ->run();
}

bool HttpSession::cacheable_request() const {
    return request_.method() == http::verb::get && request_.find(http::field::authorization) == request_.end() &&
           !is_health_request(request_);
}

bool HttpSession::cacheable_response(const HttpResponse& response) const {
    if (response.result() != http::status::ok || response.find(http::field::set_cookie) != response.end()) {
        return false;
    }
    const auto cache_control = response[http::field::cache_control];
    return cache_control.find("no-store") == boost::beast::string_view::npos &&
           cache_control.find("private") == boost::beast::string_view::npos;
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
