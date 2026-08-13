#include <eventedge/response_cache.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace eventedge {
namespace {

HttpResponse response(std::string body) {
    HttpResponse value{http::status::ok, 11};
    value.body() = std::move(body);
    value.prepare_payload();
    return value;
}

TEST(ResponseCache, ReturnsStoredResponseAndMissesUnknownKey) {
    ResponseCache cache{CacheOptions{3, std::chrono::seconds(1)}};
    cache.put("A", response("one"));

    ASSERT_TRUE(cache.get("A"));
    EXPECT_EQ(cache.get("A")->body(), "one");
    EXPECT_FALSE(cache.get("missing"));
}

TEST(ResponseCache, EvictsLeastRecentlyUsedEntry) {
    ResponseCache cache{CacheOptions{3, std::chrono::seconds(1)}};
    cache.put("A", response("A"));
    cache.put("B", response("B"));
    cache.put("C", response("C"));
    ASSERT_TRUE(cache.get("A"));
    cache.put("D", response("D"));

    EXPECT_TRUE(cache.get("A"));
    EXPECT_FALSE(cache.get("B"));
    EXPECT_TRUE(cache.get("C"));
    EXPECT_TRUE(cache.get("D"));
}

TEST(ResponseCache, UpdatesRefreshValueRecencyAndTtl) {
    ResponseCache cache{CacheOptions{2, std::chrono::seconds(1)}};
    cache.put("A", response("old"));
    cache.put("B", response("B"));
    cache.put("A", response("new"));
    cache.put("C", response("C"));

    ASSERT_TRUE(cache.get("A"));
    EXPECT_EQ(cache.get("A")->body(), "new");
    EXPECT_FALSE(cache.get("B"));
}

TEST(ResponseCache, RemovesExpiredEntries) {
    ResponseCache cache{CacheOptions{2, std::chrono::milliseconds(20)}};
    cache.put("A", response("A"));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    EXPECT_FALSE(cache.get("A"));
    EXPECT_EQ(cache.size(), 0);
}

TEST(ResponseCache, ConcurrentAccessStaysWithinCapacity) {
    ResponseCache cache{CacheOptions{16, std::chrono::seconds(1)}};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 8; ++thread) {
        threads.emplace_back([&cache, thread] {
            for (int operation = 0; operation < 200; ++operation) {
                const auto key = std::to_string((thread + operation) % 32);
                cache.put(key, response(key));
                static_cast<void>(cache.get(key));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_LE(cache.size(), 16);
}

}  // namespace
}  // namespace eventedge
