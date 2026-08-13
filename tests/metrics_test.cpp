#include <eventedge/metrics.hpp>
#include <eventedge/upstream_config.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace eventedge {
namespace {

std::shared_ptr<MetricsRegistry> metrics() {
    return std::make_shared<MetricsRegistry>(std::make_shared<UpstreamPool>(
        std::vector<UpstreamEndpoint>{{"127.0.0.1", 9001}, {"127.0.0.1", 9002}}));
}

TEST(MetricsRegistry, StartsAtZeroAndRendersPrometheusText) {
    const auto registry = metrics();
    const auto output = registry->render();
    EXPECT_NE(output.find("eventedge_requests_total 0"), std::string::npos);
    EXPECT_NE(output.find("eventedge_upstream_healthy{upstream=\"127.0.0.1:9001\"} 1"), std::string::npos);
}

TEST(MetricsRegistry, ConcurrentCountersAndGaugeAreExact) {
    const auto registry = metrics();
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 8; ++thread) {
        threads.emplace_back([&] { for (int count = 0; count < 1000; ++count) { registry->record_request(); registry->proxy_started(); registry->proxy_completed(); } });
    }
    for (auto& thread : threads) thread.join();
    const auto output = registry->render();
    EXPECT_NE(output.find("eventedge_requests_total 8000"), std::string::npos);
    EXPECT_NE(output.find("eventedge_proxy_in_flight 0"), std::string::npos);
}

TEST(MetricsRegistry, RecordsCountersSelectionsAndDurations) {
    const auto registry = metrics();
    registry->record_cache_miss(); registry->record_cache_hit(); registry->record_coalescing_leader(); registry->record_coalescing_waiter();
    registry->record_upstream_selection("127.0.0.1:9002"); registry->record_response_status(504);
    registry->observe_duration(std::chrono::milliseconds(10));
    const auto output = registry->render();
    EXPECT_NE(output.find("eventedge_cache_hits_total 1"), std::string::npos);
    EXPECT_NE(output.find("eventedge_gateway_timeout_total 1"), std::string::npos);
    EXPECT_NE(output.find("eventedge_upstream_selections_total{upstream=\"127.0.0.1:9002\"} 1"), std::string::npos);
    EXPECT_NE(output.find("eventedge_request_duration_seconds_count 1"), std::string::npos);
}

}  // namespace
}  // namespace eventedge
