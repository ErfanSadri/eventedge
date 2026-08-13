#include <eventedge/upstream_proxy.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <future>
#include <thread>

namespace eventedge {
namespace {

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using namespace std::chrono_literals;

class StalledUpstream {
public:
    StalledUpstream()
        : acceptor_(io_context_, tcp::endpoint{net::ip::address_v4::loopback(), 0}),
          thread_([this] {
              tcp::socket socket{io_context_};
              acceptor_.accept(socket);
              beast::flat_buffer buffer;
              HttpRequest request;
              beast::error_code error;
              http::read(socket, buffer, request, error);
              if (!error) {
                  request_arrived_.set_value();
                  std::this_thread::sleep_for(1s);
              }
          }) {}

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    [[nodiscard]] std::future<void> request_arrived() { return request_arrived_.get_future(); }

private:
    net::io_context io_context_{1};
    tcp::acceptor acceptor_;
    std::jthread thread_;
    std::promise<void> request_arrived_;
};

TEST(UpstreamProxy, ReadTimeoutReturnsGatewayTimeoutExactlyOnce) {
    StalledUpstream upstream;
    net::io_context io_context{1};
    std::promise<HttpResponse> result;
    auto future = result.get_future();
    HttpRequest request{http::verb::get, "/stall", 11};
    request.set(http::field::host, "client.example");

    std::make_shared<UpstreamProxy>(
        io_context.get_executor(), UpstreamEndpoint{"127.0.0.1", upstream.port()}, std::move(request),
        [&result](HttpResponse response) { result.set_value(std::move(response)); },
        ProxyTimeouts{100ms, 100ms, 100ms})
        ->run();
    std::jthread worker([&io_context] { io_context.run(); });

    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const auto response = future.get();
    EXPECT_EQ(response.result(), http::status::gateway_timeout);
    EXPECT_EQ(response.body(), "Gateway Timeout\n");
}

TEST(UpstreamProxy, IoContextStopsWithOutstandingProxyWork) {
    StalledUpstream upstream;
    const auto arrived = upstream.request_arrived();
    net::io_context io_context{1};
    HttpRequest request{http::verb::get, "/stall", 11};
    request.set(http::field::host, "client.example");
    std::make_shared<UpstreamProxy>(
        io_context.get_executor(), UpstreamEndpoint{"127.0.0.1", upstream.port()}, std::move(request),
        [](HttpResponse) {}, ProxyTimeouts{100ms, 100ms, std::chrono::seconds(5)})
        ->run();
    std::promise<void> finished;
    const auto finished_future = finished.get_future();
    std::thread worker([&] { io_context.run(); finished.set_value(); });
    ASSERT_EQ(arrived.wait_for(1s), std::future_status::ready);
    io_context.stop();
    EXPECT_EQ(finished_future.wait_for(1s), std::future_status::ready);
    worker.join();
}

}  // namespace
}  // namespace eventedge
