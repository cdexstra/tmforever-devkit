#include "build.hpp"

namespace tmfdev {

BuildInfo inspect_build(
    const std::filesystem::path& executable_path,
    const std::filesystem::path& map_path
)
{
    BuildInfo result{
        inspect_executable(executable_path),
        inspect_map(map_path),
    };

    if (!result.map.has_timestamp) {
        result.issues.push_back({
            "map file does not contain a linker timestamp"
        });
    }
    else {
        result.timestamp_matches =
            result.executable.pe.timestamp == result.map.timestamp;

        if (!result.timestamp_matches) {
            result.issues.push_back({
                "executable timestamp does not match map timestamp"
            });
        }
    }

    if (!result.map.has_preferred_load_address) {
        result.issues.push_back({
            "map file does not contain a preferred load address"
        });
    }
    else {
        result.image_base_matches =
            result.executable.pe.image_base
            == result.map.preferred_load_address;

        if (!result.image_base_matches) {
            result.issues.push_back({
                "executable image base does not match map preferred load address"
            });
        }
    }

    return result;
}

} // namespace tmfdev