cmake_minimum_required(VERSION 3.25)
project(skyrelay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SkyRelayWarnings)

find_package(GTest CONFIG REQUIRED)
find_package(CycloneDDS-CXX CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(Threads REQUIRED)
skyrelay_system_includes(Boost::asio GTest::gtest GTest::gtest_main GTest::gmock
                         CycloneDDS-CXX::ddscxx CycloneDDS::ddsc
                         yaml-cpp::yaml-cpp CLI11::CLI11)

enable_testing()
add_subdirectory(lib/common)
add_subdirectory(lib/mavlink)
add_subdirectory(lib/dds)
add_subdirectory(apps/bridge)
add_subdirectory(tests)
