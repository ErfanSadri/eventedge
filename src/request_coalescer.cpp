#include <eventedge/request_coalescer.hpp>

#include <utility>

namespace eventedge {

RequestCoalescer::Role RequestCoalescer::join_or_start(std::string key, Waiter waiter) {
    std::lock_guard lock(mutex_);
    const auto [flight, inserted] = flights_.try_emplace(std::move(key));
    if (inserted) {
        return Role::leader;
    }
    flight->second.push_back(std::move(waiter));
    return Role::waiter;
}

void RequestCoalescer::complete(const std::string& key, const HttpResponse& response) {
    std::vector<Waiter> waiters;
    {
        std::lock_guard lock(mutex_);
        const auto flight = flights_.find(key);
        if (flight == flights_.end()) {
            return;
        }
        waiters = std::move(flight->second);
        flights_.erase(flight);
    }

    for (auto& waiter : waiters) {
        waiter(response);
    }
}

std::size_t RequestCoalescer::size() const {
    std::lock_guard lock(mutex_);
    return flights_.size();
}

std::size_t RequestCoalescer::waiter_count(const std::string& key) const {
    std::lock_guard lock(mutex_);
    const auto flight = flights_.find(key);
    return flight == flights_.end() ? 0 : flight->second.size();
}

}  // namespace eventedge
