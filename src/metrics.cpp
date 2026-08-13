#include <eventedge/metrics.hpp>
#include <eventedge/upstream_config.hpp>

#include <iomanip>
#include <sstream>

namespace eventedge {
namespace {
std::string endpoint_label(const UpstreamEndpoint& endpoint) { return endpoint.host + ':' + std::to_string(endpoint.port); }
void counter(std::ostringstream& out, const char* name, std::uint64_t value) { out << name << ' ' << value << '\n'; }
}

MetricsRegistry::MetricsRegistry(std::shared_ptr<UpstreamPool> upstream_pool) : upstream_pool_(std::move(upstream_pool)) {
    for (std::size_t index = 0; index < upstream_pool_->size(); ++index) {
        upstreams_.push_back({endpoint_label(upstream_pool_->endpoint(index)), std::make_shared<std::atomic<std::uint64_t>>(0)});
    }
}
void MetricsRegistry::record_request() { requests_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_health_request() { health_requests_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_metrics_request() { metrics_requests_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_proxy_request() { proxy_requests_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_cache_hit() { cache_hits_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_cache_miss() { cache_misses_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_coalescing_leader() { coalescing_leaders_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_coalescing_waiter() { coalescing_waiters_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_health_transition() { health_transitions_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::proxy_started() { proxy_in_flight_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::proxy_completed() { proxy_in_flight_.fetch_sub(1, std::memory_order_relaxed); }
void MetricsRegistry::record_response_status(unsigned status) { if (status == 502) bad_gateway_.fetch_add(1, std::memory_order_relaxed); else if (status == 503) service_unavailable_.fetch_add(1, std::memory_order_relaxed); else if (status == 504) gateway_timeout_.fetch_add(1, std::memory_order_relaxed); }
void MetricsRegistry::record_upstream_selection(const std::string& upstream) { for (const auto& value : upstreams_) if (value.label == upstream) { value.selections->fetch_add(1, std::memory_order_relaxed); return; } }
void MetricsRegistry::observe_duration(std::chrono::steady_clock::duration duration) { const auto nanos=std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(); duration_count_.fetch_add(1,std::memory_order_relaxed); duration_sum_nanos_.fetch_add(nanos,std::memory_order_relaxed); const auto seconds=std::chrono::duration<double>(duration).count(); for(std::size_t i=0;i<buckets_.size();++i) if(seconds<=buckets_[i]) duration_buckets_[i].fetch_add(1,std::memory_order_relaxed); }
std::string MetricsRegistry::render() const {
    std::ostringstream out; out << "# TYPE eventedge_requests_total counter\n";
    counter(out,"eventedge_requests_total",requests_.load()); counter(out,"eventedge_local_health_requests_total",health_requests_.load()); counter(out,"eventedge_metrics_requests_total",metrics_requests_.load()); counter(out,"eventedge_proxy_requests_total",proxy_requests_.load()); counter(out,"eventedge_cache_hits_total",cache_hits_.load()); counter(out,"eventedge_cache_misses_total",cache_misses_.load()); counter(out,"eventedge_coalescing_leaders_total",coalescing_leaders_.load()); counter(out,"eventedge_coalescing_waiters_total",coalescing_waiters_.load()); counter(out,"eventedge_bad_gateway_total",bad_gateway_.load()); counter(out,"eventedge_service_unavailable_total",service_unavailable_.load()); counter(out,"eventedge_gateway_timeout_total",gateway_timeout_.load()); counter(out,"eventedge_upstream_health_transitions_total",health_transitions_.load()); counter(out,"eventedge_proxy_in_flight",proxy_in_flight_.load());
    std::uint64_t cumulative=0; for(std::size_t i=0;i<buckets_.size();++i) { cumulative=duration_buckets_[i].load(); out<<"eventedge_request_duration_seconds_bucket{le=\""<<buckets_[i]<<"\"} "<<cumulative<<'\n'; } out<<"eventedge_request_duration_seconds_bucket{le=\"+Inf\"} "<<duration_count_.load()<<'\n'; out<<"eventedge_request_duration_seconds_count "<<duration_count_.load()<<'\n'; out<<"eventedge_request_duration_seconds_sum "<<std::setprecision(12)<<static_cast<double>(duration_sum_nanos_.load())/1e9<<'\n';
    for(std::size_t i=0;i<upstreams_.size();++i) { const auto& value=upstreams_[i]; out<<"eventedge_upstream_selections_total{upstream=\""<<value.label<<"\"} "<<value.selections->load()<<'\n'; out<<"eventedge_upstream_healthy{upstream=\""<<value.label<<"\"} "<<(upstream_pool_->is_healthy(i)?1:0)<<'\n'; } return out.str(); }
}  // namespace eventedge
