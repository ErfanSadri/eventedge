#include <eventedge/http_handler.hpp>

#include <string_view>

namespace eventedge {
namespace {

HttpResponse make_response(const HttpRequest& request,
                           http::status status,
                           std::string_view content_type,
                           std::string_view body) {
    HttpResponse response{status, request.version()};
    response.set(http::field::server, "EventEdge");
    response.set(http::field::content_type, content_type);
    response.keep_alive(request.keep_alive());
    response.body() = body;
    response.prepare_payload();
    return response;
}

}  // namespace

bool is_health_request(const HttpRequest& request) {
    return request.target() == "/health";
}

HttpResponse handle_request(const HttpRequest& request) {
    if (request.method() != http::verb::get) {
        auto response = make_response(request,
                                      http::status::method_not_allowed,
                                      "text/plain",
                                      "Method not allowed\n");
        response.set(http::field::allow, "GET");
        return response;
    }

    if (is_health_request(request)) {
        return make_response(request,
                             http::status::ok,
                             "application/json",
                             R"({"status":"ok","service":"eventedge"})");
    }

    return make_response(request,
                         http::status::not_found,
                         "text/plain",
                         "Not found\n");
}

HttpResponse make_bad_gateway_response(const HttpRequest& request) {
    return make_response(request, http::status::bad_gateway, "text/plain", "Bad Gateway\n");
}

HttpResponse make_service_unavailable_response(const HttpRequest& request) {
    return make_response(request,
                         http::status::service_unavailable,
                         "text/plain",
                         "Service Unavailable\n");
}

}  // namespace eventedge
