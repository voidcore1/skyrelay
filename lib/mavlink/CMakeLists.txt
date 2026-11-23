add_library(skyrelay_mavlink src/framer.cpp src/packer.cpp
  src/link_uri.cpp src/udp_link.cpp)
add_library(skyrelay::mavlink ALIAS skyrelay_mavlink)
target_include_directories(skyrelay_mavlink PUBLIC include)
# The vendored headers are generated C; SYSTEM keeps -Werror -Wconversion
# from applying to them.
target_include_directories(skyrelay_mavlink SYSTEM PUBLIC
  ${PROJECT_SOURCE_DIR}/third_party/mavlink)
target_compile_features(skyrelay_mavlink PUBLIC cxx_std_20)
skyrelay_enable_warnings(skyrelay_mavlink)
target_link_libraries(skyrelay_mavlink PUBLIC Boost::asio Threads::Threads)
