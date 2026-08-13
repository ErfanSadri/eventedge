#include <eventedge/http_server.hpp>

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

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 1 && argc != 3 && argc != 5 && argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " [listen-address listen-port [upstream-host upstream-port [workers]]]\n";
        return 1;
    }

    try {
        const auto address = boost::asio::ip::make_address(argc >= 3 ? argv[1] : "127.0.0.1");
        const auto port = argc >= 3 ? parse_port(argv[2]) : std::uint16_t{8080};
        const std::string_view upstream_host = argc >= 5 ? argv[3] : "127.0.0.1";
        if (upstream_host.empty()) {
            throw std::runtime_error("upstream host must not be empty");
        }
        const auto upstream_port = argc >= 5 ? parse_port(argv[4]) : std::uint16_t{9000};
        const auto worker_count = argc == 6 ? parse_worker_count(argv[5]) : default_worker_count();

        boost::asio::io_context io_context{1};
        auto server = std::make_shared<eventedge::HttpServer>(
            io_context, eventedge::tcp::endpoint{address, port},
            eventedge::UpstreamConfig{std::string{upstream_host}, std::to_string(upstream_port)});
        server->run();

        boost::asio::signal_set signals{io_context, SIGINT, SIGTERM};
        signals.async_wait(boost::asio::bind_executor(server->executor(), [&io_context, server](
                               const boost::system::error_code& error, int) {
            if (!error) {
                server->stop();
                io_context.stop();
            }
        }));

        std::cout << "EventEdge listening on " << address << ':' << port
                  << ", proxying to " << upstream_host << ':' << upstream_port
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
