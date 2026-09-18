#include "demangle.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <array>

namespace tmfdev {

std::string demangle_msvc(const std::string& name)
{
    std::array<char, 4096> buffer{};

    const auto length = UnDecorateSymbolName(
        name.c_str(),
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        UNDNAME_COMPLETE
    );

    if (length == 0) {
        return {};
    }

    return std::string(buffer.data(), length);
}

} // namespace tmfdev