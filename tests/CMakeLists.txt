include(GoogleTest)

add_executable(skyrelay_unit_tests
  unit/common/clock_test.cpp
  unit/mavlink/framer_test.cpp
  unit/mavlink/packer_test.cpp)
target_link_libraries(skyrelay_unit_tests PRIVATE
  skyrelay::common skyrelay::mavlink GTest::gtest_main)
skyrelay_enable_warnings(skyrelay_unit_tests)
gtest_discover_tests(skyrelay_unit_tests PROPERTIES LABELS unit)
