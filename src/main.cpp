#include <eventedge/http_server.hpp>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/signal_set.hpp>

#include <charconv>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

std::uint16_t parse_port(std::string_view value) {
    unsigned int port = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535) {
        throw std::runtime_error("port must be an integer from 1 to 65535");
    }
    return static_cast<std::uint16_t>(port);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 1 && argc != 3) {
        std::cerr << "Usage: " << argv[0] << " [address port]\n";
        return 1;
    }

    try {
        const auto address = boost::asio::ip::make_address(argc == 3 ? argv[1] : "127.0.0.1");
        const auto port = argc == 3 ? parse_port(argv[2]) : std::uint16_t{8080};

        boost::asio::io_context io_context{1};
        auto server = std::make_shared<eventedge::HttpServer>(
            io_context, eventedge::tcp::endpoint{address, port});
        server->run();

        boost::asio::signal_set signals{io_context, SIGINT, SIGTERM};
        signals.async_wait([&io_context, server](const boost::system::error_code& error, int) {
            if (!error) {
                server->stop();
                io_context.stop();
            }
        });

        std::cout << "EventEdge listening on " << address << ':' << port << '\n';
        io_context.run();
    } catch (const std::exception& error) {
        std::cerr << "EventEdge failed to start: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
