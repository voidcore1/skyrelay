include(GoogleTest)

add_executable(skyrelay_unit_tests
  unit/bridge/config_test.cpp
  unit/common/clock_test.cpp
  unit/common/rate_limiter_test.cpp
  unit/dds/domain_test.cpp
  unit/dds/json_test.cpp
  unit/dds/mapping_test.cpp
  unit/dds/qos_test.cpp
  unit/dds/topics_test.cpp
  unit/gateway/fanout_test.cpp
  unit/gateway/static_files_test.cpp
  unit/mavlink/command_client_test.cpp
  unit/mavlink/command_transaction_test.cpp
  unit/mavlink/command_validation_test.cpp
  unit/mavlink/framer_test.cpp
  unit/mavlink/link_monitor_test.cpp
  unit/mavlink/link_uri_test.cpp
  unit/mavlink/packer_test.cpp
  unit/mavlink/px4_adapter_test.cpp
  unit/mavlink/session_test.cpp
  unit/mavlink/setup_sequence_test.cpp
  unit/mavlink/vehicle_model_test.cpp)
target_include_directories(skyrelay_unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(skyrelay_unit_tests PRIVATE
  SKYRELAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(skyrelay_unit_tests PRIVATE
  skyrelay::common skyrelay::mavlink skyrelay::dds skyrelay_bridge_core
  skyrelay_gateway_core GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_unit_tests)
gtest_discover_tests(skyrelay_unit_tests PROPERTIES LABELS unit)

add_executable(skyrelay_integration_tests integration/udp_link_test.cpp
  integration/tcp_link_test.cpp integration/serial_link_test.cpp
  integration/dds_domain_test.cpp integration/dds_roundtrip_test.cpp
  integration/dds_sink_test.cpp integration/session_runner_test.cpp
  integration/gateway_test.cpp)
target_include_directories(skyrelay_integration_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
# Gateway.RefusesNonLoopbackListen runs the real executable.
target_compile_definitions(skyrelay_integration_tests PRIVATE
  SKYRELAY_GATEWAY_EXE="$<TARGET_FILE:skyrelay-gateway>")
add_dependencies(skyrelay_integration_tests skyrelay-gateway)
target_link_libraries(skyrelay_integration_tests PRIVATE
  skyrelay::mavlink skyrelay::dds skyrelay_bridge_core skyrelay_gateway_core
  GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_integration_tests)
gtest_discover_tests(skyrelay_integration_tests PROPERTIES LABELS integration)
