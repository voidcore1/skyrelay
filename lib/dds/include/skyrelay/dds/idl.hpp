#pragma once

// The IDL compiler writes types.hpp flat into the build tree, named after
// idl/skyrelay/v1/types.idl. This wrapper is the one place that knows that, so
// everything else includes <skyrelay/dds/idl.hpp> and uses skyrelay::v1.
#include <types.hpp>
