#include <eventedge/upstream_health_monitor.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include <iostream>
#include <memory>
#include <functional>
#include <string>
#include <utility>

namespace eventedge {
namespace {

class HealthCheck : public std::enable_shared_from_this<HealthCheck> {
public:
    using CompletionHandler = std::function<void()>;

    HealthCheck(boost::asio::any_io_executor executor,
                std::shared_ptr<UpstreamPool> upstream_pool,
                std::shared_ptr<MetricsRegistry> metrics,
                std::size_t index,
                std::chrono::milliseconds timeout,
                CompletionHandler completion_handler)
        : executor_(executor),
          upstream_pool_(std::move(upstream_pool)),
          metrics_(std::move(metrics)),
          index_(index),
          timeout_(timeout),
          resolver_(executor),
          stream_(executor),
          timeout_timer_(executor),
          completion_handler_(std::move(completion_handler)) {}

    void run() {
        timeout_timer_.expires_after(timeout_);
        timeout_timer_.async_wait(boost::asio::bind_executor(executor_, [self = shared_from_this()](
                                                                     boost::system::error_code error) {
            if (!error) {
                self->resolver_.cancel();
                boost::beast::error_code close_error;
                self->stream_.socket().close(close_error);
                self->complete(false);
            }
        }));

        const auto endpoint = upstream_pool_->endpoint(index_);
        resolver_.async_resolve(endpoint.host, std::to_string(endpoint.port),
                                boost::asio::bind_executor(executor_, [self = shared_from_this()](
                                                                     boost::system::error_code error,
                                                                     boost::asio::ip::tcp::resolver::results_type results) {
            if (self->completed_) {
                return;
            }
            if (error) {
                return self->complete(false);
            }
            self->stream_.async_connect(results,
                                        boost::asio::bind_executor(self->executor_, [self](
                                                                 boost::system::error_code connect_error,
                                                                 const boost::asio::ip::tcp::resolver::results_type::endpoint_type&) {
                self->complete(!connect_error);
            }));
        }));
    }

private:
    void complete(bool healthy) {
        if (completed_) {
            return;
        }
        completed_ = true;
        timeout_timer_.cancel();
        boost::beast::error_code socket_error;
        stream_.socket().close(socket_error);

        if (upstream_pool_->set_healthy(index_, healthy)) {
            if (metrics_) {
                metrics_->record_health_transition();
            }
            const auto endpoint = upstream_pool_->endpoint(index_);
            std::cerr << "Upstream " << endpoint.host << ':' << endpoint.port
                      << (healthy ? " recovered\n" : " became unhealthy\n");
        }

        auto completion_handler = std::move(completion_handler_);
        completion_handler();
    }

    boost::asio::any_io_executor executor_;
    std::shared_ptr<UpstreamPool> upstream_pool_;
    std::shared_ptr<MetricsRegistry> metrics_;
    std::size_t index_;
    std::chrono::milliseconds timeout_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::tcp_stream stream_;
    boost::asio::steady_timer timeout_timer_;
    CompletionHandler completion_handler_;
    bool completed_{false};
};

}  // namespace

UpstreamHealthMonitor::UpstreamHealthMonitor(boost::asio::any_io_executor executor,
                                             std::shared_ptr<UpstreamPool> upstream_pool,
                                             HealthCheckOptions options, std::shared_ptr<MetricsRegistry> metrics)
    : executor_(executor),
      upstream_pool_(std::move(upstream_pool)),
      metrics_(std::move(metrics)),
      options_(options),
      timer_(executor) {}

void UpstreamHealthMonitor::start() {
    schedule_next_check(std::chrono::milliseconds::zero());
}

void UpstreamHealthMonitor::stop() {
    stopped_ = true;
    timer_.cancel();
}

void UpstreamHealthMonitor::schedule_next_check(std::chrono::milliseconds delay) {
    if (stopped_) {
        return;
    }
    timer_.expires_after(delay);
    timer_.async_wait(boost::asio::bind_executor(executor_, [self = shared_from_this()](
                                                               boost::system::error_code error) {
        if (!error) {
            self->run_checks();
        }
    }));
}

void UpstreamHealthMonitor::run_checks() {
    if (stopped_) {
        return;
    }
    checks_remaining_ = upstream_pool_->size();
    for (std::size_t index = 0; index < checks_remaining_; ++index) {
        std::make_shared<HealthCheck>(executor_, upstream_pool_, metrics_, index, options_.timeout,
                                      [self = shared_from_this()] { self->on_check_complete(); })
            ->run();
    }
}

void UpstreamHealthMonitor::on_check_complete() {
    if (checks_remaining_ > 0) {
        --checks_remaining_;
    }
    if (checks_remaining_ == 0 && !stopped_) {
        schedule_next_check(options_.interval);
    }
}

}  // namespace eventedge
