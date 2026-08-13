#include <eventedge/response_cache.hpp>

#include <stdexcept>
#include <utility>

namespace eventedge {

ResponseCache::ResponseCache(CacheOptions options) : options_(options) {
    if (options_.capacity == 0) {
        throw std::invalid_argument("cache capacity must be greater than zero");
    }
}

std::optional<HttpResponse> ResponseCache::get(const std::string& key) {
    std::lock_guard lock(mutex_);
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        return std::nullopt;
    }
    if (std::chrono::steady_clock::now() >= entry->second.expires_at) {
        lru_.erase(entry->second.lru_position);
        entries_.erase(entry);
        return std::nullopt;
    }
    lru_.splice(lru_.begin(), lru_, entry->second.lru_position);
    return entry->second.response;
}

void ResponseCache::put(std::string key, HttpResponse response) {
    std::lock_guard lock(mutex_);
    const auto expires_at = std::chrono::steady_clock::now() + options_.ttl;
    if (const auto existing = entries_.find(key); existing != entries_.end()) {
        existing->second.response = std::move(response);
        existing->second.expires_at = expires_at;
        lru_.splice(lru_.begin(), lru_, existing->second.lru_position);
        return;
    }
    if (entries_.size() == options_.capacity) {
        entries_.erase(lru_.back());
        lru_.pop_back();
    }
    lru_.push_front(key);
    entries_.emplace(std::move(key), Entry{std::move(response), expires_at, lru_.begin()});
}

std::size_t ResponseCache::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

}  // namespace eventedge
