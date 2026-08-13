#include <eventedge/http_server.hpp>
#include <eventedge/http_handler.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace eventedge {
namespace {

namespace beast = boost::beast;

class TestUpstream {
public:
    explicit TestUpstream(std::size_t expected_requests)
        : acceptor_(io_context_, tcp::endpoint{net::ip::address_v4::loopback(), 0}),
          expected_requests_(expected_requests),
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
        for (std::size_t request_number = 0; request_number < expected_requests_; ++request_number) {
            tcp::socket socket{io_context_};
            acceptor_.accept(socket);

            beast::flat_buffer buffer;
            HttpRequest request;
            http::read(socket, buffer, request);
            {
                std::lock_guard lock(requests_mutex_);
                requests_.push_back(request);
            }

            HttpResponse response{
                request.method() == http::verb::post ? http::status::created : http::status::ok,
                request.version()};
            response.set(http::field::content_type, "text/plain");
            response.set("X-Test-Upstream", "eventedge-test");
            response.set("X-Backend-Hop", "remove-me");
            response.set(http::field::connection, "X-Backend-Hop");
            response.keep_alive(false);
            response.body() = "upstream response for " + std::string{request.target()};
            response.prepare_payload();
            http::write(socket, response);

            beast::error_code shutdown_error;
            socket.shutdown(tcp::socket::shutdown_send, shutdown_error);
        }
    }

    net::io_context io_context_{1};
    tcp::acceptor acceptor_;
    std::size_t expected_requests_;
    mutable std::mutex requests_mutex_;
    std::vector<HttpRequest> requests_;
    std::jthread thread_;
};

class EventEdgeServer {
public:
    explicit EventEdgeServer(UpstreamConfig upstream)
        : server_(std::make_shared<HttpServer>(
              io_context_, tcp::endpoint{net::ip::address_v4::loopback(), 0}, std::move(upstream))) {
        server_->run();
        thread_ = std::jthread([this] { io_context_.run(); });
    }

    ~EventEdgeServer() {
        std::promise<void> stopped;
        const auto stopped_future = stopped.get_future();
        net::post(io_context_, [server = server_, &stopped] {
            server->stop();
            stopped.set_value();
        });
        stopped_future.wait();
        io_context_.stop();
    }

    [[nodiscard]] std::uint16_t port() const {
        return server_->port();
    }

private:
    net::io_context io_context_{1};
    std::shared_ptr<HttpServer> server_;
    std::jthread thread_;
};

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
    EventEdgeServer eventedge{UpstreamConfig{"127.0.0.1", std::to_string(upstream.port())}};

    http::request<http::string_body> request{http::verb::get, "/live/game/42", 11};
    request.set(http::field::host, "client.example");
    request.set("X-Client-Hop", "remove-me");
    request.set(http::field::connection, "X-Client-Hop");
    const auto response = send_request(eventedge.port(), request);

    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_EQ(response["X-Test-Upstream"], "eventedge-test");
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
    EventEdgeServer eventedge{UpstreamConfig{"127.0.0.1", std::to_string(upstream.port())}};

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
    EventEdgeServer eventedge{UpstreamConfig{"127.0.0.1", std::to_string(upstream.port())}};

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

TEST(ReverseProxyIntegration, ReturnsBadGatewayWithoutAffectingLocalHealth) {
    net::io_context reservation_io{1};
    tcp::acceptor reservation{reservation_io, tcp::endpoint{net::ip::address_v4::loopback(), 0}};
    const auto unavailable_port = reservation.local_endpoint().port();
    reservation.close();

    EventEdgeServer eventedge{UpstreamConfig{"127.0.0.1", std::to_string(unavailable_port)}};

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
