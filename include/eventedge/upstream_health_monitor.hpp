#pragma once

#include <eventedge/upstream_config.hpp>
#include <eventedge/metrics.hpp>

#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <memory>

namespace eventedge {

struct HealthCheckOptions {
    std::chrono::milliseconds interval{std::chrono::seconds(2)};
    std::chrono::milliseconds timeout{std::chrono::seconds(1)};
};

class UpstreamHealthMonitor : public std::enable_shared_from_this<UpstreamHealthMonitor> {
public:
    UpstreamHealthMonitor(boost::asio::any_io_executor executor,
                          std::shared_ptr<UpstreamPool> upstream_pool,
                          HealthCheckOptions options = {}, std::shared_ptr<MetricsRegistry> metrics = nullptr);

    void start();
    void stop();

private:
    void schedule_next_check(std::chrono::milliseconds delay);
    void run_checks();
    void on_check_complete();

    boost::asio::any_io_executor executor_;
    std::shared_ptr<UpstreamPool> upstream_pool_;
    std::shared_ptr<MetricsRegistry> metrics_;
    HealthCheckOptions options_;
    boost::asio::steady_timer timer_;
    std::size_t checks_remaining_{0};
    bool stopped_{false};
};

}  // namespace eventedge
