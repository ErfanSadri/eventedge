cmake_minimum_required(VERSION 3.24)

project(EventEdge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost 1.74 CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(eventedge_core
    src/http_handler.cpp
    src/http_server.cpp
    src/http_session.cpp
    src/upstream_health_monitor.cpp
    src/upstream_proxy.cpp
)
target_include_directories(eventedge_core PUBLIC include)
target_link_libraries(eventedge_core PUBLIC Boost::headers Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(eventedge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(eventedge src/main.cpp)
target_link_libraries(eventedge PRIVATE eventedge_core)

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(eventedge_tests
        tests/http_handler_test.cpp
        tests/http_server_integration_test.cpp
        tests/upstream_pool_test.cpp
        tests/upstream_health_monitor_test.cpp
        tests/upstream_proxy_timeout_test.cpp
        tests/reverse_proxy_integration_test.cpp
    )
    target_link_libraries(eventedge_tests PRIVATE eventedge_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(eventedge_tests)
endif()
