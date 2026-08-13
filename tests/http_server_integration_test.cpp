#include <eventedge/http_server.hpp>
#include <eventedge/http_handler.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <future>
#include <thread>

namespace eventedge {
namespace {

namespace beast = boost::beast;

class IoContextStopper {
public:
    explicit IoContextStopper(net::io_context& io_context) : io_context_(io_context) {}

    ~IoContextStopper() {
        io_context_.stop();
    }

private:
    net::io_context& io_context_;
};

TEST(HttpServerIntegration, ServesHealthEndpointOverTcp) {
    net::io_context server_io{1};
    auto server = std::make_shared<HttpServer>(
        server_io, tcp::endpoint{net::ip::address_v4::loopback(), 0},
        std::make_shared<UpstreamPool>(std::vector<UpstreamEndpoint>{{"127.0.0.1", 9000}}),
        std::make_shared<ResponseCache>(), std::make_shared<RequestCoalescer>());
    server->run();

    std::jthread server_thread([&server_io] { server_io.run(); });
    const IoContextStopper io_context_stopper{server_io};

    net::io_context client_io{1};
    beast::tcp_stream stream{client_io};
    stream.connect(tcp::endpoint{net::ip::address_v4::loopback(), server->port()});

    http::request<http::string_body> request{http::verb::get, "/health", 11};
    request.set(http::field::host, "localhost");
    request.prepare_payload();
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_EQ(response.body(), R"({"status":"ok","service":"eventedge"})");

    std::promise<void> stopped;
    const auto stopped_future = stopped.get_future();
    net::post(server->executor(), [server, &stopped] {
        server->stop();
        stopped.set_value();
    });
    stopped_future.wait();
    server_io.stop();
}

TEST(HttpServerIntegration, ServesMetricsLocally) {
    net::io_context server_io{1};
    auto server = std::make_shared<HttpServer>(
        server_io, tcp::endpoint{net::ip::address_v4::loopback(), 0},
        std::make_shared<UpstreamPool>(std::vector<UpstreamEndpoint>{{"127.0.0.1", 9000}}),
        std::make_shared<ResponseCache>(), std::make_shared<RequestCoalescer>());
    server->run();
    std::jthread server_thread([&server_io] { server_io.run(); });

    net::io_context client_io{1}; beast::tcp_stream stream{client_io};
    stream.connect(tcp::endpoint{net::ip::address_v4::loopback(), server->port()});
    http::request<http::string_body> request{http::verb::get, "/metrics", 11}; request.set(http::field::host, "localhost");
    http::write(stream, request); beast::flat_buffer buffer; http::response<http::string_body> response; http::read(stream, buffer, response);
    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_EQ(response[http::field::content_type], "text/plain; version=0.0.4");
    EXPECT_NE(response.body().find("eventedge_metrics_requests_total 1"), std::string::npos);
    server->stop(); server_io.stop();
}

}  // namespace
}  // namespace eventedge
