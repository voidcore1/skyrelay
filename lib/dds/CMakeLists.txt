# Generated C++ for the IDL. -Wno-implicit-extensibility is off by default
# anyway; every type is annotated explicitly.
idlcxx_generate(TARGET skyrelay_idl
  FILES ${PROJECT_SOURCE_DIR}/idl/skyrelay/v1/types.idl
  WARNINGS no-implicit-extensibility)
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
