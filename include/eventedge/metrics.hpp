#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace eventedge {

class UpstreamPool;

class MetricsRegistry {
public:
    explicit MetricsRegistry(std::shared_ptr<UpstreamPool> upstream_pool);

    void record_request();
    void record_health_request();
    void record_metrics_request();
    void record_proxy_request();
    void record_cache_hit();
    void record_cache_miss();
    void record_coalescing_leader();
    void record_coalescing_waiter();
    void record_response_status(unsigned status);
    void record_upstream_selection(const std::string& upstream);
    void record_health_transition();
    void proxy_started();
    void proxy_completed();
    void observe_duration(std::chrono::steady_clock::duration duration);

    [[nodiscard]] std::string render() const;

private:
    static constexpr std::array<double, 8> buckets_{0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0};
    struct UpstreamMetrics {
        std::string label;
        std::shared_ptr<std::atomic<std::uint64_t>> selections;
    };
    std::shared_ptr<UpstreamPool> upstream_pool_;
    std::vector<UpstreamMetrics> upstreams_;
    std::atomic<std::uint64_t> requests_{0}, health_requests_{0}, metrics_requests_{0}, proxy_requests_{0};
    std::atomic<std::uint64_t> cache_hits_{0}, cache_misses_{0}, coalescing_leaders_{0}, coalescing_waiters_{0};
    std::atomic<std::uint64_t> bad_gateway_{0}, service_unavailable_{0}, gateway_timeout_{0}, health_transitions_{0};
    std::atomic<std::uint64_t> proxy_in_flight_{0}, duration_count_{0}, duration_sum_nanos_{0};
    std::array<std::atomic<std::uint64_t>, buckets_.size()> duration_buckets_{};
};

}  // namespace eventedge
