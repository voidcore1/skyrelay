# Everything but main() is a library, so the tests can run the server, the
# fan-out and the DDS feed in-process.
add_library(skyrelay_gateway_core STATIC src/static_files.cpp src/fanout.cpp
  src/ws_server.cpp src/dds_feed.cpp)
target_include_directories(skyrelay_gateway_core PUBLIC src)
target_compile_features(skyrelay_gateway_core PUBLIC cxx_std_20)
skyrelay_enable_warnings(skyrelay_gateway_core)
target_link_libraries(skyrelay_gateway_core PUBLIC skyrelay::common skyrelay::dds
  Boost::beast PRIVATE spdlog::spdlog)

add_executable(skyrelay-gateway src/main.cpp)
skyrelay_enable_warnings(skyrelay-gateway)
target_link_libraries(skyrelay-gateway PRIVATE skyrelay_gateway_core CLI11::CLI11
  spdlog::spdlog)
