#pragma once

#include "model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace tmfdev {

struct NativeSdkSkip {
    std::string function_name;
    std::string reason;
};

struct NativeSdkOutput {
    std::string text;
    std::size_t emitted_functions = 0;
    std::size_t skipped_functions = 0;
    std::vector<NativeSdkSkip> skips;
};

struct NativeSdkFunctionAssessment {
    bool emitted = false;
    std::string reason;
};

struct NativeSdkManifestOutput {
    std::string text;
};

struct NativeSdkSearchIndexOutput {
    std::string text;
};

NativeSdkOutput generate_native_sdk_header(
    const NativeModel& model,
    const std::string& requested_class
);

NativeSdkOutput generate_native_runtime_header(
    const NativeModel& model
);

NativeSdkOutput generate_native_runtime_source(
    const NativeModel& model
);

NativeSdkFunctionAssessment assess_native_sdk_function(
    const NativeModel& model,
    const NativeFunction& function
);

NativeSdkManifestOutput generate_native_sdk_manifest_json(
    const NativeModel& model,
    const std::string& requested_class
);

NativeSdkSearchIndexOutput generate_native_sdk_search_index_tsv(
    const NativeModel& model
);

} // namespace tmfdev
