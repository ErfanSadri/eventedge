#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace eventedge {

struct UpstreamEndpoint {
    std::string host;
    std::uint16_t port;
};

class UpstreamPool {
public:
    explicit UpstreamPool(std::vector<UpstreamEndpoint> endpoints)
        : backends_() {
        if (endpoints.empty()) {
            throw std::invalid_argument("at least one upstream endpoint is required");
        }
        backends_.reserve(endpoints.size());
        for (auto& endpoint : endpoints) {
            backends_.push_back({std::move(endpoint), true});
        }
    }

    [[nodiscard]] std::optional<UpstreamEndpoint> select() {
        std::lock_guard lock(mutex_);
        for (std::size_t offset = 0; offset < backends_.size(); ++offset) {
            const auto index = (next_index_ + offset) % backends_.size();
            if (backends_[index].healthy) {
                next_index_ = (index + 1) % backends_.size();
                return backends_[index].endpoint;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t size() const {
        return backends_.size();
    }

    [[nodiscard]] UpstreamEndpoint endpoint(std::size_t index) const {
        std::lock_guard lock(mutex_);
        return backends_.at(index).endpoint;
    }

    [[nodiscard]] bool is_healthy(std::size_t index) const {
        std::lock_guard lock(mutex_);
        return backends_.at(index).healthy;
    }

    [[nodiscard]] bool set_healthy(std::size_t index, bool healthy) {
        std::lock_guard lock(mutex_);
        auto& backend = backends_.at(index);
        if (backend.healthy == healthy) {
            return false;
        }
        backend.healthy = healthy;
        return true;
    }

private:
    struct Backend {
        UpstreamEndpoint endpoint;
        bool healthy;
    };

    mutable std::mutex mutex_;
    std::vector<Backend> backends_;
    std::size_t next_index_{0};
};

}  // namespace eventedge
