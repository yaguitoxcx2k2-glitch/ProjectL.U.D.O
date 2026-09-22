#pragma once
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <string_view>

namespace game::pure::audio_policy {
inline constexpr int MaxEffectChannels = 24;
inline constexpr int SourceUrlCacheEntries = 128;
inline constexpr std::int64_t AudioPreloadPerFileBytes = 16ll * 1024 * 1024;
inline constexpr std::int64_t AudioPreloadBudgetBytes = 96ll * 1024 * 1024;
inline bool shouldCacheAudioBytes(std::int64_t fileSize, std::int64_t bytesAlreadyCached) { return fileSize>0 && fileSize<=AudioPreloadPerFileBytes && bytesAlreadyCached>=0 && bytesAlreadyCached+fileSize<=AudioPreloadBudgetBytes; }
inline double combinedVolume(int baseVolume,int masterVolume,int localVolume) { return std::clamp(baseVolume,0,100)/100.0 * std::clamp(masterVolume,0,100)/100.0 * std::clamp(localVolume,0,100)/100.0; }
inline bool prefersLowLatencyEffect(std::string_view file) { auto ext=std::filesystem::path(std::string(file)).extension().string(); std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));}); return ext==".wav" || ext==".wave"; }
}
