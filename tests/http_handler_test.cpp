#include <eventedge/http_handler.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace eventedge {
namespace {

HttpRequest make_request(http::verb method, std::string_view target, bool keep_alive = true) {
    HttpRequest request{method, target, 11};
    request.keep_alive(keep_alive);
    return request;
}

TEST(HttpHandler, HealthEndpointReturnsExpectedJson) {
    const auto response = handle_request(make_request(http::verb::get, "/health"));

    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_EQ(response[http::field::content_type], "application/json");
    EXPECT_EQ(response.body(), R"({"status":"ok","service":"eventedge"})");
    EXPECT_TRUE(response.keep_alive());
}

TEST(HttpHandler, UnknownRouteReturnsNotFound) {
    const auto response = handle_request(make_request(http::verb::get, "/not-found"));

    EXPECT_EQ(response.result(), http::status::not_found);
}

TEST(HttpHandler, UnsupportedMethodReturnsMethodNotAllowed) {
    const auto response = handle_request(make_request(http::verb::post, "/health"));

    EXPECT_EQ(response.result(), http::status::method_not_allowed);
    EXPECT_EQ(response[http::field::allow], "GET");
}

TEST(HttpHandler, PreservesConnectionCloseRequest) {
    const auto response = handle_request(make_request(http::verb::get, "/health", false));

    EXPECT_FALSE(response.keep_alive());
}

}  // namespace
}  // namespace eventedge
