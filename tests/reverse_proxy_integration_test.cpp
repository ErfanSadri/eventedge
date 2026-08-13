#include <eventedge/http_server.hpp>
#include <eventedge/http_handler.hpp>
#include <eventedge/upstream_health_monitor.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <array>
#include <barrier>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace eventedge {
namespace {

namespace beast = boost::beast;

class TestUpstream {
public:
    explicit TestUpstream(std::size_t expected_requests,
                          std::string identity = "upstream",
                          std::uint16_t port = 0)
        : acceptor_(io_context_, tcp::endpoint{net::ip::address_v4::loopback(), port}),
          expected_requests_(expected_requests),
          identity_(std::move(identity)),
          thread_([this] { serve_requests(); }) {}

    [[nodiscard]] std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }

    [[nodiscard]] std::vector<HttpRequest> requests() const {
        std::lock_guard lock(requests_mutex_);
        return requests_;
    }

private:
    void serve_requests() {
        std::size_t requests_served = 0;
        while (requests_served < expected_requests_) {
            tcp::socket socket{io_context_};
            acceptor_.accept(socket);

            beast::flat_buffer buffer;
            HttpRequest request;
            beast::error_code read_error;
            http::read(socket, buffer, request, read_error);
            if (read_error) {
                continue;
            }
            {
                std::lock_guard lock(requests_mutex_);
                requests_.push_back(request);
            }

            HttpResponse response{
                request.method() == http::verb::post ? http::status::created : http::status::ok,
                request.version()};
            response.set(http::field::content_type, "text/plain");
            response.set("X-Test-Upstream", identity_);
            response.set("X-Backend-Hop", "remove-me");
            response.set(http::field::connection, "X-Backend-Hop");
            response.keep_alive(false);
            response.body() = identity_ + " response for " + std::string{request.target()};
            response.prepare_payload();
            http::write(socket, response);

            beast::error_code shutdown_error;
            socket.shutdown(tcp::socket::shutdown_send, shutdown_error);
            ++requests_served;
        }
    }

    net::io_context io_context_{1};
    tcp::acceptor acceptor_;
    std::size_t expected_requests_;
    std::string identity_;
    mutable std::mutex requests_mutex_;
    std::vector<HttpRequest> requests_;
    std::jthread thread_;
};

class EventEdgeServer {
public:
    explicit EventEdgeServer(std::vector<UpstreamEndpoint> upstreams,
                             std::size_t worker_count = 1,
                             HealthCheckOptions health_options = {},
                             bool start_health_monitor = false)
        : upstream_pool_(std::make_shared<UpstreamPool>(std::move(upstreams))),
          server_(std::make_shared<HttpServer>(
              io_context_, tcp::endpoint{net::ip::address_v4::loopback(), 0},
              upstream_pool_)) {
        server_->run();
        if (start_health_monitor) {
            health_monitor_ = std::make_shared<UpstreamHealthMonitor>(
                server_->executor(), upstream_pool_, health_options);
            health_monitor_->start();
        }
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            threads_.emplace_back([this] { io_context_.run(); });
        }
    }

    ~EventEdgeServer() {
        std::promise<void> stopped;
        const auto stopped_future = stopped.get_future();
        net::post(server_->executor(), [server = server_, monitor = health_monitor_, &stopped] {
            if (monitor) {
                monitor->stop();
            }
            server->stop();
            stopped.set_value();
        });
        stopped_future.wait();
        io_context_.stop();
    }

    [[nodiscard]] std::uint16_t port() const {
        return server_->port();
    }

    [[nodiscard]] std::shared_ptr<UpstreamPool> upstream_pool() const {
        return upstream_pool_;
    }

private:
    net::io_context io_context_{1};
    std::shared_ptr<UpstreamPool> upstream_pool_;
    std::shared_ptr<HttpServer> server_;
    std::shared_ptr<UpstreamHealthMonitor> health_monitor_;
    std::vector<std::jthread> threads_;
};

std::vector<UpstreamEndpoint> single_upstream(const TestUpstream& upstream) {
    return {{"127.0.0.1", upstream.port()}};
}

http::response<http::string_body> send_request(
    std::uint16_t port, const http::request<http::string_body>& request) {
    net::io_context client_io{1};
    beast::tcp_stream stream{client_io};
    stream.connect(tcp::endpoint{net::ip::address_v4::loopback(), port});
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    return response;
}

TEST(ReverseProxyIntegration, ForwardsTargetRewritesHostAndRelaysResponse) {
    TestUpstream upstream{1};
    EventEdgeServer eventedge{single_upstream(upstream)};

    http::request<http::string_body> request{http::verb::get, "/live/game/42", 11};
    request.set(http::field::host, "client.example");
    request.set("X-Client-Hop", "remove-me");
    request.set(http::field::connection, "X-Client-Hop");
    const auto response = send_request(eventedge.port(), request);

    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_EQ(response["X-Test-Upstream"], "upstream");
    EXPECT_EQ(response.find("X-Backend-Hop"), response.end());
    EXPECT_EQ(response.body(), "upstream response for /live/game/42");

    const auto requests = upstream.requests();
    ASSERT_EQ(requests.size(), 1);
    EXPECT_EQ(requests.front().target(), "/live/game/42");
    EXPECT_EQ(requests.front()[http::field::host], "127.0.0.1:" + std::to_string(upstream.port()));
    EXPECT_EQ(requests.front().find("X-Client-Hop"), requests.front().end());
    EXPECT_FALSE(requests.front().keep_alive());
}

TEST(ReverseProxyIntegration, ProxiesPostMethodAndBody) {
    TestUpstream upstream{1};
    EventEdgeServer eventedge{single_upstream(upstream)};

    http::request<http::string_body> request{http::verb::post, "/events", 11};
    request.set(http::field::host, "client.example");
    request.set(http::field::content_type, "application/json");
    request.body() = R"({"name":"test"})";
    request.prepare_payload();
    const auto response = send_request(eventedge.port(), request);

    EXPECT_EQ(response.result(), http::status::created);
    const auto requests = upstream.requests();
    ASSERT_EQ(requests.size(), 1);
    EXPECT_EQ(requests.front().method(), http::verb::post);
    EXPECT_EQ(requests.front().body(), R"({"name":"test"})");
}

TEST(ReverseProxyIntegration, KeepsClientConnectionAliveAcrossProxiedRequests) {
    TestUpstream upstream{2};
    EventEdgeServer eventedge{single_upstream(upstream)};

    net::io_context client_io{1};
    beast::tcp_stream stream{client_io};
    stream.connect(tcp::endpoint{net::ip::address_v4::loopback(), eventedge.port()});

    http::request<http::string_body> first_request{http::verb::get, "/first", 11};
    first_request.set(http::field::host, "client.example");
    http::write(stream, first_request);
    beast::flat_buffer first_buffer;
    http::response<http::string_body> first_response;
    http::read(stream, first_buffer, first_response);
    EXPECT_EQ(first_response.result(), http::status::ok);
    EXPECT_TRUE(first_response.keep_alive());

    http::request<http::string_body> second_request{http::verb::get, "/second", 11};
    second_request.set(http::field::host, "client.example");
    second_request.keep_alive(false);
    http::write(stream, second_request);
    beast::flat_buffer second_buffer;
    http::response<http::string_body> second_response;
    http::read(stream, second_buffer, second_response);
    EXPECT_EQ(second_response.result(), http::status::ok);
    EXPECT_FALSE(second_response.keep_alive());

    const auto requests = upstream.requests();
    ASSERT_EQ(requests.size(), 2);
    EXPECT_EQ(requests[0].target(), "/first");
    EXPECT_EQ(requests[1].target(), "/second");
}

TEST(ReverseProxyIntegration, ConcurrentClientsReceiveTheirOwnProxiedResponses) {
    constexpr std::size_t request_count = 32;
    TestUpstream upstream{request_count};
    EventEdgeServer eventedge{single_upstream(upstream), 4};

    std::barrier start_gate{static_cast<std::ptrdiff_t>(request_count + 1)};
    std::vector<std::future<HttpResponse>> responses;
    responses.reserve(request_count);
    for (std::size_t request_number = 0; request_number < request_count; ++request_number) {
        responses.push_back(std::async(std::launch::async, [request_number, &eventedge, &start_gate] {
            start_gate.arrive_and_wait();
            http::request<http::string_body> request{
                http::verb::get, "/concurrent/" + std::to_string(request_number), 11};
            request.set(http::field::host, "client.example");
            return send_request(eventedge.port(), request);
        }));
    }
    start_gate.arrive_and_wait();

    for (std::size_t request_number = 0; request_number < request_count; ++request_number) {
        const auto response = responses[request_number].get();
        EXPECT_EQ(response.result(), http::status::ok);
        EXPECT_EQ(response.body(), "upstream response for /concurrent/" + std::to_string(request_number));
    }

    EXPECT_EQ(upstream.requests().size(), request_count);
}

TEST(ReverseProxyIntegration, CyclesAcrossThreeUpstreamsWithoutHealthAdvancingSelection) {
    TestUpstream backend_a{2, "backend-a"};
    TestUpstream backend_b{2, "backend-b"};
    TestUpstream backend_c{2, "backend-c"};
    EventEdgeServer eventedge{{{"127.0.0.1", backend_a.port()},
                               {"127.0.0.1", backend_b.port()},
                               {"127.0.0.1", backend_c.port()}}};

    http::request<http::string_body> health_request{http::verb::get, "/health", 11};
    health_request.set(http::field::host, "client.example");
    EXPECT_EQ(send_request(eventedge.port(), health_request).result(), http::status::ok);

    const std::array<std::string, 6> expected_backends{
        "backend-a", "backend-b", "backend-c", "backend-a", "backend-b", "backend-c"};
    for (std::size_t request_number = 0; request_number < expected_backends.size(); ++request_number) {
        http::request<http::string_body> request{
            http::verb::get, "/round-robin/" + std::to_string(request_number), 11};
        request.set(http::field::host, "client.example");
        const auto response = send_request(eventedge.port(), request);
        EXPECT_EQ(response.result(), http::status::ok);
        EXPECT_EQ(response["X-Test-Upstream"], expected_backends[request_number]);
    }

    const auto requests_a = backend_a.requests();
    const auto requests_b = backend_b.requests();
    const auto requests_c = backend_c.requests();
    ASSERT_EQ(requests_a.size(), 2);
    ASSERT_EQ(requests_b.size(), 2);
    ASSERT_EQ(requests_c.size(), 2);
    EXPECT_EQ(requests_a.front()[http::field::host], "127.0.0.1:" + std::to_string(backend_a.port()));
    EXPECT_EQ(requests_b.front()[http::field::host], "127.0.0.1:" + std::to_string(backend_b.port()));
    EXPECT_EQ(requests_c.front()[http::field::host], "127.0.0.1:" + std::to_string(backend_c.port()));
}

TEST(ReverseProxyIntegration, ConcurrentClientsAreDistributedAcrossMultipleUpstreams) {
    constexpr std::size_t request_count_per_backend = 10;
    constexpr std::size_t request_count = request_count_per_backend * 3;
    TestUpstream backend_a{request_count_per_backend, "backend-a"};
    TestUpstream backend_b{request_count_per_backend, "backend-b"};
    TestUpstream backend_c{request_count_per_backend, "backend-c"};
    EventEdgeServer eventedge{{{"127.0.0.1", backend_a.port()},
                               {"127.0.0.1", backend_b.port()},
                               {"127.0.0.1", backend_c.port()}},
                              4};

    std::barrier start_gate{static_cast<std::ptrdiff_t>(request_count + 1)};
    std::vector<std::future<HttpResponse>> responses;
    responses.reserve(request_count);
    for (std::size_t request_number = 0; request_number < request_count; ++request_number) {
        responses.push_back(std::async(std::launch::async, [request_number, &eventedge, &start_gate] {
            start_gate.arrive_and_wait();
            http::request<http::string_body> request{
                http::verb::get, "/multi/" + std::to_string(request_number), 11};
            request.set(http::field::host, "client.example");
            return send_request(eventedge.port(), request);
        }));
    }
    start_gate.arrive_and_wait();

    for (auto& response_future : responses) {
        const auto response = response_future.get();
        EXPECT_EQ(response.result(), http::status::ok);
        EXPECT_TRUE(response["X-Test-Upstream"] == "backend-a" ||
                    response["X-Test-Upstream"] == "backend-b" ||
                    response["X-Test-Upstream"] == "backend-c");
    }

    EXPECT_EQ(backend_a.requests().size(), request_count_per_backend);
    EXPECT_EQ(backend_b.requests().size(), request_count_per_backend);
    EXPECT_EQ(backend_c.requests().size(), request_count_per_backend);
}

TEST(ReverseProxyIntegration, DeadBackendReturnsBadGatewayAndRotationContinues) {
    TestUpstream backend_a{1, "backend-a"};
    TestUpstream backend_b{1, "backend-b"};
    net::io_context reservation_io{1};
    tcp::acceptor reservation{reservation_io, tcp::endpoint{net::ip::address_v4::loopback(), 0}};
    const auto unavailable_port = reservation.local_endpoint().port();
    reservation.close();
    EventEdgeServer eventedge{{{"127.0.0.1", backend_a.port()},
                               {"127.0.0.1", unavailable_port},
                               {"127.0.0.1", backend_b.port()}}};

    http::request<http::string_body> health_request{http::verb::get, "/health", 11};
    health_request.set(http::field::host, "client.example");
    EXPECT_EQ(send_request(eventedge.port(), health_request).result(), http::status::ok);

    http::request<http::string_body> first_request{http::verb::get, "/first", 11};
    first_request.set(http::field::host, "client.example");
    EXPECT_EQ(send_request(eventedge.port(), first_request)["X-Test-Upstream"], "backend-a");

    http::request<http::string_body> failed_request{http::verb::get, "/dead", 11};
    failed_request.set(http::field::host, "client.example");
    EXPECT_EQ(send_request(eventedge.port(), failed_request).result(), http::status::bad_gateway);

    http::request<http::string_body> third_request{http::verb::get, "/third", 11};
    third_request.set(http::field::host, "client.example");
    EXPECT_EQ(send_request(eventedge.port(), third_request)["X-Test-Upstream"], "backend-b");
}

TEST(ReverseProxyIntegration, ReturnsServiceUnavailableWhenNoBackendIsHealthy) {
    TestUpstream backend{0, "backend"};
    EventEdgeServer eventedge{single_upstream(backend)};
    static_cast<void>(eventedge.upstream_pool()->set_healthy(0, false));

    http::request<http::string_body> proxied_request{http::verb::get, "/unavailable", 11};
    proxied_request.set(http::field::host, "client.example");
    const auto unavailable = send_request(eventedge.port(), proxied_request);
    EXPECT_EQ(unavailable.result(), http::status::service_unavailable);
    EXPECT_EQ(unavailable.body(), "Service Unavailable\n");

    http::request<http::string_body> health_request{http::verb::get, "/health", 11};
    health_request.set(http::field::host, "client.example");
    EXPECT_EQ(send_request(eventedge.port(), health_request).result(), http::status::ok);
}

TEST(ReverseProxyIntegration, ReturnsBadGatewayWithoutAffectingLocalHealth) {
    net::io_context reservation_io{1};
    tcp::acceptor reservation{reservation_io, tcp::endpoint{net::ip::address_v4::loopback(), 0}};
    const auto unavailable_port = reservation.local_endpoint().port();
    reservation.close();

    EventEdgeServer eventedge{{{"127.0.0.1", unavailable_port}}};

    http::request<http::string_body> proxied_request{http::verb::get, "/unavailable", 11};
    proxied_request.set(http::field::host, "client.example");
    const auto bad_gateway = send_request(eventedge.port(), proxied_request);
    EXPECT_EQ(bad_gateway.result(), http::status::bad_gateway);
    EXPECT_EQ(bad_gateway.body(), "Bad Gateway\n");

    http::request<http::string_body> health_request{http::verb::get, "/health", 11};
    health_request.set(http::field::host, "client.example");
    const auto health = send_request(eventedge.port(), health_request);
    EXPECT_EQ(health.result(), http::status::ok);
}

}  // namespace
}  // namespace eventedge
