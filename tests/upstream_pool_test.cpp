#include <eventedge/upstream_config.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace eventedge {
namespace {

std::vector<UpstreamEndpoint> three_endpoints() {
    return {{"backend-a", 9001}, {"backend-b", 9002}, {"backend-c", 9003}};
}

TEST(UpstreamPool, CyclesInRoundRobinOrder) {
    UpstreamPool pool{three_endpoints()};

    EXPECT_EQ(pool.select().host, "backend-a");
    EXPECT_EQ(pool.select().host, "backend-b");
    EXPECT_EQ(pool.select().host, "backend-c");
    EXPECT_EQ(pool.select().host, "backend-a");
    EXPECT_EQ(pool.select().host, "backend-b");
    EXPECT_EQ(pool.select().host, "backend-c");
}

TEST(UpstreamPool, SingleEndpointIsAlwaysSelected) {
    UpstreamPool pool{{{"backend-only", 9001}}};

    for (int selection = 0; selection < 10; ++selection) {
        const auto endpoint = pool.select();
        EXPECT_EQ(endpoint.host, "backend-only");
        EXPECT_EQ(endpoint.port, 9001);
    }
}

TEST(UpstreamPool, ConcurrentSelectionsRemainConsistent) {
    constexpr std::size_t selection_count = 1200;
    constexpr std::size_t thread_count = 8;
    UpstreamPool pool{three_endpoints()};
    std::array<std::atomic<std::size_t>, 3> counts{};
    for (auto& count : counts) {
        count.store(0);
    }

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&pool, &counts] {
            for (std::size_t selection = 0; selection < selection_count / thread_count; ++selection) {
                const auto endpoint = pool.select();
                if (endpoint.host == "backend-a") {
                    counts[0].fetch_add(1, std::memory_order_relaxed);
                } else if (endpoint.host == "backend-b") {
                    counts[1].fetch_add(1, std::memory_order_relaxed);
                } else {
                    EXPECT_EQ(endpoint.host, "backend-c");
                    counts[2].fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(counts[0].load(), selection_count / 3);
    EXPECT_EQ(counts[1].load(), selection_count / 3);
    EXPECT_EQ(counts[2].load(), selection_count / 3);
}

}  // namespace
}  // namespace eventedge
