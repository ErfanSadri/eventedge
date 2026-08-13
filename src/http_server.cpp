#include <eventedge/http_server.hpp>

#include <eventedge/http_session.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/system/system_error.hpp>

#include <iostream>
#include <utility>

namespace eventedge {
namespace {

void throw_if_error(const boost::system::error_code& error, const char* operation) {
    if (error) {
        throw boost::system::system_error(error, operation);
    }
}

}  // namespace

HttpServer::HttpServer(net::io_context& io_context, tcp::endpoint endpoint, UpstreamConfig upstream)
    : acceptor_(io_context), acceptor_strand_(net::make_strand(io_context)), upstream_(std::move(upstream)) {
    boost::system::error_code error;

    acceptor_.open(endpoint.protocol(), error);
    throw_if_error(error, "opening acceptor");

    acceptor_.set_option(net::socket_base::reuse_address(true), error);
    throw_if_error(error, "setting reuse address");

    acceptor_.bind(endpoint, error);
    throw_if_error(error, "binding acceptor");

    acceptor_.listen(net::socket_base::max_listen_connections, error);
    throw_if_error(error, "listening on acceptor");
}

void HttpServer::run() {
    do_accept();
}

void HttpServer::stop() {
    boost::system::error_code error;
    acceptor_.close(error);
}

net::any_io_executor HttpServer::executor() const {
    return acceptor_strand_;
}

std::uint16_t HttpServer::port() const {
    return acceptor_.local_endpoint().port();
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
        net::make_strand(acceptor_.get_executor()),
        net::bind_executor(acceptor_strand_, [self = shared_from_this()](
                                                boost::system::error_code error, tcp::socket socket) {
            if (!error) {
                std::make_shared<HttpSession>(std::move(socket), self->upstream_)->run();
            } else if (error != net::error::operation_aborted) {
                std::cerr << "Accept error: " << error.message() << '\n';
            }

            if (self->acceptor_.is_open()) {
                self->do_accept();
            }
        }));
}

}  // namespace eventedge
