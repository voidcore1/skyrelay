# Everything but main() is a library, so the tests can use the config loader
# and the session runner.
add_library(skyrelay_bridge_core STATIC src/config.cpp src/session_runner.cpp)
target_include_directories(skyrelay_bridge_core PUBLIC src)
target_compile_features(skyrelay_bridge_core PUBLIC cxx_std_20)
skyrelay_enable_warnings(skyrelay_bridge_core)
target_link_libraries(skyrelay_bridge_core PUBLIC skyrelay::mavlink
  PRIVATE yaml-cpp::yaml-cpp spdlog::spdlog)

add_executable(skyrelay-bridge src/main.cpp)
skyrelay_enable_warnings(skyrelay-bridge)
target_link_libraries(skyrelay-bridge PRIVATE skyrelay_bridge_core skyrelay::dds
  CLI11::CLI11 spdlog::spdlog)
