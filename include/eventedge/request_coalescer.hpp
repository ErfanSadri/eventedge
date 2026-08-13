#pragma once

#include <eventedge/http_handler.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eventedge {

class RequestCoalescer {
public:
    using Waiter = std::function<void(HttpResponse)>;

    enum class Role {
        leader,
        waiter,
    };

    [[nodiscard]] Role join_or_start(std::string key, Waiter waiter);
    void complete(const std::string& key, const HttpResponse& response);
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t waiter_count(const std::string& key) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<Waiter>> flights_;
};

}  // namespace eventedge
