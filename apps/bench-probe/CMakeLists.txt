add_executable(skyrelay-bench-probe src/main.cpp)
skyrelay_enable_warnings(skyrelay-bench-probe)
target_link_libraries(skyrelay-bench-probe PRIVATE skyrelay::dds CLI11::CLI11)
