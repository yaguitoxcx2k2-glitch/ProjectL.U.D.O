#include "RuntimeGpuPolicy.h"
#include <algorithm>
#include <cctype>

namespace game::pure {
std::string normalizeRuntimeGpuBackend(std::string_view backend)
{
    std::string id(backend); while(!id.empty() && std::isspace((unsigned char)id.front())) id.erase(id.begin()); while(!id.empty() && std::isspace((unsigned char)id.back())) id.pop_back();
    std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){return char(std::tolower(c));});
    if (id=="d3d11"||id=="vulkan"||id=="d3d12"||id=="opengl"||id=="metal") return id;
    return "auto";
}
std::string runtimeGpuBackendDisplayName(std::string_view backend)
{
    const auto id=normalizeRuntimeGpuBackend(backend);
    if(id=="d3d11")return "Direct3D 11"; if(id=="d3d12")return "Direct3D 12"; if(id=="vulkan")return "Vulkan"; if(id=="opengl")return "OpenGL"; if(id=="metal")return "Metal"; return "Automático";
}
std::vector<std::string> runtimeGpuBackendFallbackChain(std::string_view requested, Platform platform)
{
    const auto id=normalizeRuntimeGpuBackend(requested); std::vector<std::string> defaults;
    if(platform==Platform::Windows) defaults={"d3d11","vulkan"}; else if(platform==Platform::MacOS) defaults={"metal"}; else defaults={"vulkan","opengl"};
    if(id=="auto") return defaults; std::vector<std::string> out{id};
    for(const auto& v:defaults) if(std::find(out.begin(),out.end(),v)==out.end()) out.push_back(v); return out;
}
} // namespace game::pure
