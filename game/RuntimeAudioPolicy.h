// ============================================================================
// RuntimeAudioPolicy.h — adapter Qt para as regras puras de áudio do runtime.
// A autoridade das regras vive em game/pure/RuntimeAudioPolicy.h.
// ============================================================================
#pragma once

#include "game/pure/RuntimeAudioPolicy.h"

#include <QString>
#include <QtGlobal>

namespace game::audio_policy {

inline constexpr int MaxEffectChannels = 24;
static_assert(MaxEffectChannels == pure::audio_policy::MaxEffectChannels);
inline constexpr int SourceUrlCacheEntries = pure::audio_policy::SourceUrlCacheEntries;
inline constexpr qint64 AudioPreloadPerFileBytes = pure::audio_policy::AudioPreloadPerFileBytes;
inline constexpr qint64 AudioPreloadBudgetBytes = pure::audio_policy::AudioPreloadBudgetBytes;

inline bool shouldCacheAudioBytes(qint64 fileSize, qint64 bytesAlreadyCached)
{
    return pure::audio_policy::shouldCacheAudioBytes(fileSize, bytesAlreadyCached);
}
inline double combinedVolume(int baseVolume, int masterVolume, int localVolume)
{
    return pure::audio_policy::combinedVolume(baseVolume, masterVolume, localVolume);
}
inline bool prefersLowLatencyEffect(const QString& file)
{
    const QByteArray bytes=file.toUtf8();
    return pure::audio_policy::prefersLowLatencyEffect(std::string_view(bytes.constData(), size_t(bytes.size())));
}

} // namespace game::audio_policy
