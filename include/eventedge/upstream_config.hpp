#pragma once

#include <atomic>
#include <cstdint>
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
        : endpoints_(std::move(endpoints)) {
        if (endpoints_.empty()) {
            throw std::invalid_argument("at least one upstream endpoint is required");
        }
    }

    [[nodiscard]] UpstreamEndpoint select() {
        const auto index = next_index_.fetch_add(1, std::memory_order_relaxed);
        return endpoints_[index % endpoints_.size()];
    }

    [[nodiscard]] std::size_t size() const {
        return endpoints_.size();
    }

private:
    const std::vector<UpstreamEndpoint> endpoints_;
    std::atomic<std::size_t> next_index_{0};
};

}  // namespace eventedge
