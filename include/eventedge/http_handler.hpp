#pragma once

#include <boost/beast/http.hpp>

namespace eventedge {

namespace http = boost::beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

[[nodiscard]] bool is_health_request(const HttpRequest& request);
[[nodiscard]] HttpResponse handle_request(const HttpRequest& request);
[[nodiscard]] HttpResponse make_bad_gateway_response(const HttpRequest& request);
[[nodiscard]] HttpResponse make_service_unavailable_response(const HttpRequest& request);
[[nodiscard]] HttpResponse make_gateway_timeout_response(const HttpRequest& request);

}  // namespace eventedge
