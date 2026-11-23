include(GoogleTest)

add_executable(skyrelay_unit_tests
  unit/common/clock_test.cpp
  unit/dds/domain_test.cpp
  unit/dds/qos_test.cpp
  unit/mavlink/command_transaction_test.cpp
  unit/mavlink/framer_test.cpp
  unit/mavlink/link_monitor_test.cpp
  unit/mavlink/link_uri_test.cpp
  unit/mavlink/packer_test.cpp
  unit/mavlink/px4_adapter_test.cpp
  unit/mavlink/setup_sequence_test.cpp
  unit/mavlink/vehicle_model_test.cpp)
target_link_libraries(skyrelay_unit_tests PRIVATE
  skyrelay::common skyrelay::mavlink skyrelay::dds GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_unit_tests)
gtest_discover_tests(skyrelay_unit_tests PROPERTIES LABELS unit)

add_executable(skyrelay_integration_tests integration/udp_link_test.cpp
  integration/dds_roundtrip_test.cpp)
target_link_libraries(skyrelay_integration_tests PRIVATE
  skyrelay::mavlink skyrelay::dds GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_integration_tests)
gtest_discover_tests(skyrelay_integration_tests PROPERTIES LABELS integration)
