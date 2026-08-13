cmake_minimum_required(VERSION 3.24)

project(EventEdge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EVENTEDGE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(EVENTEDGE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(EVENTEDGE_ENABLE_ASAN AND EVENTEDGE_ENABLE_TSAN)
    message(FATAL_ERROR "EVENTEDGE_ENABLE_ASAN and EVENTEDGE_ENABLE_TSAN cannot be enabled together")
endif()

find_package(Boost 1.74 CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(eventedge_core
    src/http_handler.cpp
    src/http_server.cpp
    src/http_session.cpp
    src/metrics.cpp
    src/request_coalescer.cpp
    src/response_cache.cpp
    src/upstream_health_monitor.cpp
    src/upstream_proxy.cpp
)
target_include_directories(eventedge_core PUBLIC include)
target_link_libraries(eventedge_core PUBLIC Boost::headers Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(eventedge_core PRIVATE -Wall -Wextra -Wpedantic)
    if(EVENTEDGE_ENABLE_ASAN)
        target_compile_options(eventedge_core PUBLIC -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(eventedge_core PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    elseif(EVENTEDGE_ENABLE_TSAN)
        target_compile_options(eventedge_core PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(eventedge_core PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
    endif()
elseif(EVENTEDGE_ENABLE_ASAN OR EVENTEDGE_ENABLE_TSAN)
    message(FATAL_ERROR "EventEdge sanitizers require a Clang or GCC-like compiler")
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
        tests/metrics_test.cpp
        tests/upstream_pool_test.cpp
        tests/upstream_health_monitor_test.cpp
        tests/upstream_proxy_timeout_test.cpp
        tests/request_coalescer_test.cpp
        tests/response_cache_test.cpp
        tests/reverse_proxy_integration_test.cpp
    )
    target_link_libraries(eventedge_tests PRIVATE eventedge_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(eventedge_tests DISCOVERY_MODE PRE_TEST)
endif()
