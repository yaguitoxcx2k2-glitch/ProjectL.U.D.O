#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace game::pure {

enum class Platform { Windows, MacOS, Other };
std::string normalizeRuntimeGpuBackend(std::string_view backend);
std::string runtimeGpuBackendDisplayName(std::string_view backend);
std::vector<std::string> runtimeGpuBackendFallbackChain(std::string_view requested, Platform platform);

} // namespace game::pure
