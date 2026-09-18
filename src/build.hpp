#pragma once

#include "map.hpp"
#include "pe.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tmfdev {

struct BuildIssue {
    std::string message;
};

struct BuildInfo {
    ExecutableInfo executable;
    MapInfo map;

    bool timestamp_matches = false;
    bool image_base_matches = false;

    std::vector<BuildIssue> issues;

    bool matches() const
    {
        return timestamp_matches
            && image_base_matches
            && issues.empty();
    }
};

BuildInfo inspect_build(
    const std::filesystem::path& executable_path,
    const std::filesystem::path& map_path
);

} // namespace tmfdev