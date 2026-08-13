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
                         std::shared_ptr<ResponseCache> response_cache,
                         std::shared_ptr<RequestCoalescer> request_coalescer, std::shared_ptr<MetricsRegistry> metrics)
    : stream_(std::move(socket)),
      upstream_pool_(std::move(upstream_pool)),
      response_cache_(std::move(response_cache)),
      request_coalescer_(std::move(request_coalescer)), metrics_(std::move(metrics)) {}

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
    request_started_ = std::chrono::steady_clock::now();
    metrics_->record_request();

    if (is_health_request(request_)) {
        metrics_->record_health_request();
        return write_response(handle_request(request_));
    }
    if (request_.target() == "/metrics") {
        metrics_->record_metrics_request();
        if (request_.method() != http::verb::get) {
            return write_response(handle_request(request_));
        }
        HttpResponse response{http::status::ok, request_.version()};
        response.set(http::field::content_type, "text/plain; version=0.0.4");
        response.keep_alive(request_.keep_alive());
        response.body() = metrics_->render();
        response.prepare_payload();
        return write_response(std::move(response));
    }

    const auto cache_key = std::string{request_.target()};
    if (cacheable_request()) {
        if (auto cached = response_cache_->get(cache_key)) {
            metrics_->record_cache_hit();
            return write_response_for_current_request(std::move(*cached));
        }
        metrics_->record_cache_miss();

        const auto request_coalescer = request_coalescer_.lock();
        if (!request_coalescer) {
            return write_response_for_current_request(make_service_unavailable_response(request_));
        }
        const auto role = request_coalescer->join_or_start(
            cache_key, [self = shared_from_this(), executor = stream_.get_executor()](HttpResponse response) mutable {
                net::post(executor, [self, response = std::move(response)]() mutable {
                    self->write_response_for_current_request(std::move(response));
                });
            });
        if (role == RequestCoalescer::Role::waiter) {
            metrics_->record_coalescing_waiter();
            return;
        }
        metrics_->record_coalescing_leader();

        const auto upstream = upstream_pool_->select();
        if (!upstream) {
            return complete_flight(cache_key, make_service_unavailable_response(request_), false);
        }

        metrics_->record_proxy_request();
        metrics_->record_upstream_selection(upstream->host + ':' + std::to_string(upstream->port));
        metrics_->proxy_started();

        std::make_shared<UpstreamProxy>(
            stream_.get_executor(), *upstream, request_,
            [self = shared_from_this(), cache_key](HttpResponse response) {
                self->metrics_->proxy_completed();
                self->complete_flight(cache_key, std::move(response), true);
            })
            ->run();
        return;
    }

    const auto upstream = upstream_pool_->select();
    if (!upstream) {
        return write_response(make_service_unavailable_response(request_));
    }
    metrics_->record_proxy_request();
    metrics_->record_upstream_selection(upstream->host + ':' + std::to_string(upstream->port));
    metrics_->proxy_started();

    std::make_shared<UpstreamProxy>(
        stream_.get_executor(), *upstream, request_, [self = shared_from_this()](HttpResponse response) {
            self->metrics_->proxy_completed();
            self->write_response_for_current_request(std::move(response));
        })
        ->run();
}

void HttpSession::complete_flight(const std::string& key, HttpResponse response, bool should_cache) {
    if (should_cache && cacheable_response(response)) {
        response_cache_->put(key, response);
    }
    if (const auto request_coalescer = request_coalescer_.lock()) {
        request_coalescer->complete(key, response);
    }
    write_response_for_current_request(std::move(response));
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
    metrics_->record_response_status(static_cast<unsigned>(response.result_int()));
    metrics_->observe_duration(std::chrono::steady_clock::now() - request_started_);
    auto response_to_write = std::make_shared<HttpResponse>(std::move(response));
    const bool close_after_write = response_to_write->need_eof();

    http::async_write(stream_, *response_to_write,
                      net::bind_executor(stream_.get_executor(), [self = shared_from_this(), response_to_write, close_after_write](
                          beast::error_code write_error, std::size_t bytes_transferred) {
                          self->on_write(close_after_write, write_error, bytes_transferred);
                      }));
}

void HttpSession::write_response_for_current_request(HttpResponse response) {
    response.version(request_.version());
    response.keep_alive(request_.keep_alive());
    write_response(std::move(response));
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
