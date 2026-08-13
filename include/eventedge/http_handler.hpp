#pragma once

#include <boost/beast/http.hpp>

namespace eventedge {

namespace http = boost::beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

[[nodiscard]] HttpResponse handle_request(const HttpRequest& request);

}  // namespace eventedge
