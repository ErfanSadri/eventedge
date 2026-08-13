cmake_minimum_required(VERSION 3.24)

project(EventEdge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(eventedge src/main.cpp)
target_include_directories(eventedge PRIVATE include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(eventedge PRIVATE -Wall -Wextra -Wpedantic)
endif()

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

    add_executable(eventedge_tests tests/foundation_test.cpp)
    target_link_libraries(eventedge_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(eventedge_tests)
endif()
