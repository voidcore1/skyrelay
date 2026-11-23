include(GoogleTest)

add_executable(skyrelay_unit_tests
  unit/common/clock_test.cpp
  unit/mavlink/framer_test.cpp
  unit/mavlink/link_monitor_test.cpp
  unit/mavlink/link_uri_test.cpp
  unit/mavlink/packer_test.cpp)
target_link_libraries(skyrelay_unit_tests PRIVATE
  skyrelay::common skyrelay::mavlink GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_unit_tests)
gtest_discover_tests(skyrelay_unit_tests PROPERTIES LABELS unit)

add_executable(skyrelay_integration_tests integration/udp_link_test.cpp)
target_link_libraries(skyrelay_integration_tests PRIVATE
  skyrelay::mavlink GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_integration_tests)
gtest_discover_tests(skyrelay_integration_tests PROPERTIES LABELS integration)
