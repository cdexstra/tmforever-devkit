#pragma once

#include "build.hpp"
#include "model.hpp"

#include <string>

namespace tmfdev {

NativeModel build_native_model(
    const BuildInfo& build,
    const std::string& class_name
);

NativeModel build_native_model_all(
    const BuildInfo& build
);

NativeModel build_native_model_all_fast(
    const BuildInfo& build
);

} // namespace tmfdev
