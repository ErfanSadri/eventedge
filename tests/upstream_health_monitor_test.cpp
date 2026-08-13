#include <eventedge/upstream_health_monitor.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <functional>
#include <thread>

namespace eventedge {
namespace {

using namespace std::chrono_literals;

class TcpBackend {
public:
    TcpBackend()
        : acceptor_(io_context_, boost::asio::ip::tcp::endpoint{
              boost::asio::ip::address_v4::loopback(), 0}),
          thread_([this] { accept_connections(); }) {}

    ~TcpBackend() {
        boost::system::error_code error;
        acceptor_.close(error);
    }

    [[nodiscard]] std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }

private:
    void accept_connections() {
        while (acceptor_.is_open()) {
            boost::asio::ip::tcp::socket socket{io_context_};
            boost::system::error_code error;
            acceptor_.accept(socket, error);
            if (error) {
                break;
            }
        }
    }

    boost::asio::io_context io_context_{1};
    boost::asio::ip::tcp::acceptor acceptor_;
    std::jthread thread_;
};

bool wait_for(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

TEST(UpstreamHealthMonitor, MarksLiveAndUnavailableEndpoints) {
    TcpBackend live_backend;
    boost::asio::io_context reservation_io{1};
    boost::asio::ip::tcp::acceptor reservation{
        reservation_io, boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0}};
    const auto unavailable_port = reservation.local_endpoint().port();
    reservation.close();

    auto pool = std::make_shared<UpstreamPool>(std::vector<UpstreamEndpoint>{
        {"127.0.0.1", live_backend.port()}, {"127.0.0.1", unavailable_port}});
    boost::asio::io_context io_context{1};
    auto monitor = std::make_shared<UpstreamHealthMonitor>(
        io_context.get_executor(), pool, HealthCheckOptions{20ms, 100ms});
    monitor->start();
    std::jthread worker([&io_context] { io_context.run(); });

    EXPECT_TRUE(wait_for([&] { return pool->is_healthy(0) && !pool->is_healthy(1); }));
    monitor->stop();
    io_context.stop();
}

TEST(UpstreamHealthMonitor, StopWithScheduledChecksAllowsBoundedShutdown) {
    auto pool = std::make_shared<UpstreamPool>(std::vector<UpstreamEndpoint>{{"127.0.0.1", 9}});
    boost::asio::io_context io_context{1};
    auto monitor = std::make_shared<UpstreamHealthMonitor>(
        io_context.get_executor(), pool, HealthCheckOptions{std::chrono::seconds(30), 200ms});
    monitor->start();

    std::promise<void> stopped;
    const auto stopped_future = stopped.get_future();
    std::thread worker([&] { io_context.run(); });
    boost::asio::post(io_context, [&] {
        monitor->stop();
        monitor.reset();
        io_context.stop();
        stopped.set_value();
    });
    EXPECT_EQ(stopped_future.wait_for(1s), std::future_status::ready);
    worker.join();
}

TEST(UpstreamPool, SkipsUnhealthyEndpointsAndAllowsRecovery) {
    UpstreamPool pool{{{"backend-a", 9001}, {"backend-b", 9002}, {"backend-c", 9003}}};
    static_cast<void>(pool.set_healthy(1, false));

    EXPECT_EQ(pool.select()->host, "backend-a");
    EXPECT_EQ(pool.select()->host, "backend-c");
    EXPECT_EQ(pool.select()->host, "backend-a");
    EXPECT_EQ(pool.select()->host, "backend-c");

    static_cast<void>(pool.set_healthy(1, true));
    EXPECT_EQ(pool.select()->host, "backend-a");
    EXPECT_EQ(pool.select()->host, "backend-b");
    EXPECT_EQ(pool.select()->host, "backend-c");
}

TEST(UpstreamPool, ReportsNoEndpointWhenAllAreUnhealthy) {
    UpstreamPool pool{{{"backend-a", 9001}, {"backend-b", 9002}}};
    static_cast<void>(pool.set_healthy(0, false));
    static_cast<void>(pool.set_healthy(1, false));

    EXPECT_FALSE(pool.select().has_value());
}

}  // namespace
}  // namespace eventedge
