#include <eventedge/request_coalescer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

namespace eventedge {
namespace {

HttpResponse response(http::status status = http::status::ok) {
    HttpResponse value{status, 11};
    value.body() = "result";
    value.prepare_payload();
    return value;
}

TEST(RequestCoalescer, FirstCallerLeadsAndSecondCallerWaits) {
    RequestCoalescer coalescer;

    EXPECT_EQ(coalescer.join_or_start("/live", {}), RequestCoalescer::Role::leader);
    EXPECT_EQ(coalescer.join_or_start("/live", {}), RequestCoalescer::Role::waiter);
    EXPECT_EQ(coalescer.size(), 1);
}

TEST(RequestCoalescer, DifferentKeysHaveIndependentLeaders) {
    RequestCoalescer coalescer;

    EXPECT_EQ(coalescer.join_or_start("/live/a", {}), RequestCoalescer::Role::leader);
    EXPECT_EQ(coalescer.join_or_start("/live/b", {}), RequestCoalescer::Role::leader);
    EXPECT_EQ(coalescer.size(), 2);
}

TEST(RequestCoalescer, CompletionDeliversEveryWaiterOnceAndReleasesFlight) {
    RequestCoalescer coalescer;
    std::atomic<int> deliveries{0};
    EXPECT_EQ(coalescer.join_or_start("/live", {}), RequestCoalescer::Role::leader);
    EXPECT_EQ(coalescer.join_or_start("/live", [&deliveries](HttpResponse value) {
                  EXPECT_EQ(value.body(), "result");
                  ++deliveries;
              }),
              RequestCoalescer::Role::waiter);
    EXPECT_EQ(coalescer.join_or_start("/live", [&deliveries](HttpResponse) { ++deliveries; }),
              RequestCoalescer::Role::waiter);

    coalescer.complete("/live", response());

    EXPECT_EQ(deliveries, 2);
    EXPECT_EQ(coalescer.size(), 0);
    EXPECT_EQ(coalescer.join_or_start("/live", {}), RequestCoalescer::Role::leader);
}

TEST(RequestCoalescer, CompletionInvokesCallbacksOutsideTheMutex) {
    RequestCoalescer coalescer;
    EXPECT_EQ(coalescer.join_or_start("/first", {}), RequestCoalescer::Role::leader);
    EXPECT_EQ(coalescer.join_or_start("/first", [&coalescer](HttpResponse) {
                  EXPECT_EQ(coalescer.join_or_start("/second", {}), RequestCoalescer::Role::leader);
              }),
              RequestCoalescer::Role::waiter);

    coalescer.complete("/first", response());

    EXPECT_EQ(coalescer.size(), 1);
}

TEST(RequestCoalescer, ConcurrentJoinsElectExactlyOneLeader) {
    constexpr std::size_t callers = 32;
    RequestCoalescer coalescer;
    std::barrier start_gate{static_cast<std::ptrdiff_t>(callers)};
    std::atomic<int> leaders{0};
    std::atomic<int> deliveries{0};
    std::vector<std::jthread> threads;
    threads.reserve(callers);

    for (std::size_t caller = 0; caller < callers; ++caller) {
        threads.emplace_back([&] {
            start_gate.arrive_and_wait();
            if (coalescer.join_or_start("/live", [&deliveries](HttpResponse) { ++deliveries; }) ==
                RequestCoalescer::Role::leader) {
                ++leaders;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(leaders, 1);
    coalescer.complete("/live", response(http::status::bad_gateway));
    EXPECT_EQ(deliveries, static_cast<int>(callers - 1));
    EXPECT_EQ(coalescer.size(), 0);
}

}  // namespace
}  // namespace eventedge
