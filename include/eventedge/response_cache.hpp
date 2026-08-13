#pragma once

#include <eventedge/http_handler.hpp>

#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace eventedge {

struct CacheOptions {
    std::size_t capacity{1024};
    std::chrono::milliseconds ttl{std::chrono::seconds(2)};
};

class ResponseCache {
public:
    explicit ResponseCache(CacheOptions options = {});

    [[nodiscard]] std::optional<HttpResponse> get(const std::string& key);
    void put(std::string key, HttpResponse response);
    [[nodiscard]] std::size_t size() const;

private:
    struct Entry {
        HttpResponse response;
        std::chrono::steady_clock::time_point expires_at;
        std::list<std::string>::iterator lru_position;
    };

    CacheOptions options_;
    mutable std::mutex mutex_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace eventedge
