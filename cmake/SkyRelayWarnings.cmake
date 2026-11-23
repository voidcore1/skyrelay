# Strict warnings for SkyRelay's own targets only. Third-party and generated
# code is consumed through SYSTEM include directories, so these flags never
# apply to it.
function(skyrelay_enable_warnings target)
  get_target_property(_type ${target} TYPE)
  if(_type STREQUAL "INTERFACE_LIBRARY")
    set(_scope INTERFACE)
  else()
    set(_scope PRIVATE)
  endif()
  target_compile_options(${target} ${_scope}
    -Wall -Wextra -Wpedantic -Wconversion -Werror)
endfunction()

# Mark an imported target's include directories as SYSTEM so that warnings
# raised inside third-party headers are suppressed.
function(skyrelay_system_includes)
  foreach(_t IN LISTS ARGN)
    if(TARGET ${_t})
      get_target_property(_aliased ${_t} ALIASED_TARGET)
      if(_aliased)
        set(_t ${_aliased})
      endif()
      set_target_properties(${_t} PROPERTIES IMPORTED_NO_SYSTEM FALSE)
      get_target_property(_inc ${_t} INTERFACE_INCLUDE_DIRECTORIES)
      if(_inc)
        set_target_properties(${_t} PROPERTIES
          INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
      endif()
    endif()
  endforeach()
endfunction()
