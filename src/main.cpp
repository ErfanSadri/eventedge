#include <eventedge/http_server.hpp>
#include <eventedge/upstream_health_monitor.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/signal_set.hpp>

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::uint16_t parse_port(std::string_view value) {
    unsigned int port = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535) {
        throw std::runtime_error("port must be an integer from 1 to 65535");
    }
    return static_cast<std::uint16_t>(port);
}

unsigned int parse_worker_count(std::string_view value) {
    unsigned int workers = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), workers);
    if (error != std::errc{} || end != value.data() + value.size() || workers == 0) {
        throw std::runtime_error("worker count must be a positive integer");
    }
    return workers;
}

unsigned int default_worker_count() {
    return std::max(1U, std::thread::hardware_concurrency());
}

eventedge::UpstreamEndpoint parse_upstream(std::string_view value) {
    std::string_view host;
    std::string_view port;

    if (value.starts_with('[')) {
        const auto closing_bracket = value.find(']');
        if (closing_bracket == std::string_view::npos || closing_bracket + 1 >= value.size() ||
            value[closing_bracket + 1] != ':') {
            throw std::runtime_error("upstream must use host:port or [ipv6-address]:port");
        }
        host = value.substr(1, closing_bracket - 1);
        port = value.substr(closing_bracket + 2);
    } else {
        const auto separator = value.rfind(':');
        if (separator == std::string_view::npos || value.find(':') != separator) {
            throw std::runtime_error("upstream must use host:port or [ipv6-address]:port");
        }
        host = value.substr(0, separator);
        port = value.substr(separator + 1);
    }

    if (host.empty()) {
        throw std::runtime_error("upstream host must not be empty");
    }
    return eventedge::UpstreamEndpoint{std::string{host}, parse_port(port)};
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 1 && argc < 5) {
        std::cerr << "Usage: " << argv[0]
                  << " [listen-address listen-port workers upstream-host:port [upstream-host:port ...]]\n";
        return 1;
    }

    try {
        const auto address = boost::asio::ip::make_address(argc == 1 ? "127.0.0.1" : argv[1]);
        const auto port = argc == 1 ? std::uint16_t{8080} : parse_port(argv[2]);
        const auto worker_count = argc == 1 ? default_worker_count() : parse_worker_count(argv[3]);
        std::vector<eventedge::UpstreamEndpoint> upstreams;
        if (argc == 1) {
            upstreams.push_back({"127.0.0.1", 9000});
        } else {
            upstreams.reserve(static_cast<std::size_t>(argc - 4));
            for (int argument = 4; argument < argc; ++argument) {
                upstreams.push_back(parse_upstream(argv[argument]));
            }
        }

        boost::asio::io_context io_context{1};
        auto upstream_pool = std::make_shared<eventedge::UpstreamPool>(std::move(upstreams));
        auto response_cache = std::make_shared<eventedge::ResponseCache>();
        auto server = std::make_shared<eventedge::HttpServer>(
            io_context, eventedge::tcp::endpoint{address, port}, upstream_pool, response_cache);
        server->run();
        auto health_monitor = std::make_shared<eventedge::UpstreamHealthMonitor>(
            server->executor(), upstream_pool);
        health_monitor->start();

        boost::asio::signal_set signals{io_context, SIGINT, SIGTERM};
        signals.async_wait(boost::asio::bind_executor(server->executor(), [&io_context, health_monitor, server](
                               const boost::system::error_code& error, int) {
            if (!error) {
                health_monitor->stop();
                server->stop();
                io_context.stop();
            }
        }));

        std::cout << "EventEdge listening on " << address << ':' << port
                  << ", proxying to " << (argc == 1 ? 1 : argc - 4) << " upstream(s)"
                  << " with " << worker_count << " worker(s)\n";

        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (unsigned int worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&io_context] {
                try {
                    io_context.run();
                } catch (const std::exception& error) {
                    std::cerr << "EventEdge worker error: " << error.what() << '\n';
                    io_context.stop();
                } catch (...) {
                    std::cerr << "EventEdge worker error: unknown exception\n";
                    io_context.stop();
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
    } catch (const std::exception& error) {
        std::cerr << "EventEdge failed to start: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
