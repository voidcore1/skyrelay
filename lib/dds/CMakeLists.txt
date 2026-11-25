# Generated C++ for the IDL. Every struct must be @appendable, and
# tools/codegen/idl2gen.py (run with --check in CI) rejects one that is not
# annotated. idlc itself would default an unannotated type to final, so it is
# told to default to appendable instead: a missed annotation can never put a
# final type on the wire.
idlcxx_generate(TARGET skyrelay_idl
  FILES ${PROJECT_SOURCE_DIR}/idl/skyrelay/v1/types.idl
  DEFAULT_EXTENSIBILITY appendable)
# Generated code is not ours to hold to -Werror -Wconversion.
skyrelay_system_includes(skyrelay_idl)
get_target_property(_idl_inc skyrelay_idl INTERFACE_INCLUDE_DIRECTORIES)
if(_idl_inc)
  set_target_properties(skyrelay_idl PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_idl_inc}")
endif()

add_library(skyrelay_dds src/qos.cpp src/domain.cpp src/mapping.cpp
  src/dds_sink.cpp)
add_library(skyrelay::dds ALIAS skyrelay_dds)
target_include_directories(skyrelay_dds PUBLIC include)
target_compile_features(skyrelay_dds PUBLIC cxx_std_20)
skyrelay_enable_warnings(skyrelay_dds)
target_link_libraries(skyrelay_dds PUBLIC skyrelay::mavlink skyrelay_idl CycloneDDS-CXX::ddscxx
  CycloneDDS::ddsc nlohmann_json::nlohmann_json)
target_link_libraries(skyrelay_dds PRIVATE spdlog::spdlog)
