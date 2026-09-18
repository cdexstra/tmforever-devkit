#pragma once

#include "model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace tmfdev {

enum class NativeAbiStatus {
    ready,
    conditional,
    blocked,
};

enum class NativeAbiIssueLevel {
    conditional,
    blocker,
};

struct NativeAbiIssue {
    NativeAbiIssueLevel level =
        NativeAbiIssueLevel::conditional;

    std::string subject;
    std::string detail;
};

struct NativeAbiAssessment {
    NativeAbiStatus status = NativeAbiStatus::ready;
    std::vector<NativeAbiIssue> issues;
};

struct NativeAbiIssueSummary {
    NativeAbiIssueLevel level =
        NativeAbiIssueLevel::conditional;

    std::string detail;
    std::size_t affected_functions = 0;
    std::size_t occurrences = 0;
};

struct NativeAbiTypeNeed {
    std::string type_name;
    NativeTypeKind kind = NativeTypeKind::unknown;

    std::size_t affected_functions = 0;
    std::size_t occurrences = 0;
};

struct NativeAbiReport {
    std::size_t functions = 0;
    std::size_t ready = 0;
    std::size_t conditional = 0;
    std::size_t blocked = 0;

    std::vector<NativeAbiIssueSummary> issue_summaries;
    std::vector<NativeAbiTypeNeed> type_needs;
};

NativeAbiAssessment assess_native_function_abi(
    const NativeModel& model,
    const NativeFunction& function
);

NativeAbiReport build_native_abi_report(
    const NativeModel& model
);

const char* native_abi_status_name(NativeAbiStatus status);
const char* native_abi_issue_level_name(NativeAbiIssueLevel level);

} // namespace tmfdev
