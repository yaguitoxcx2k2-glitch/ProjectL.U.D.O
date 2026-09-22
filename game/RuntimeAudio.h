// ============================================================================
// RuntimeAudio.h — áudio compartilhado pelas janelas CPU e GPU.
//
// A sessão continua comunicando por hooks. Esta classe implementa a parte
// dependente de Qt Multimedia uma única vez. O8 mantém um pool de SEs
// reutilizável, cacheia preferências de volume e reaproveita URLs de assets.
// ============================================================================
#pragma once

#include <QtGlobal>
#include <QHash>
#include <QQueue>
#include <QString>
#include <QUrl>
#include <QVector>
#include <memory>

QT_BEGIN_NAMESPACE
class QObject;
class QMediaPlayer;
class QBuffer;
class QAudioOutput;
class QSoundEffect;
QT_END_NAMESPACE

namespace core { class RuntimePreloadCache; }

namespace game {

class RuntimeAudio
{
public:
    explicit RuntimeAudio(QObject* owner = nullptr) : m_owner(owner) {}

    void setOwner(QObject* owner) { m_owner = owner; }
    /// Anexa o cache transiente criado pelo preload. O runtime usa bytes já
    /// lidos quando disponíveis e cai para arquivo/URL normalmente caso não.
    void setPreloadCache(std::shared_ptr<core::RuntimePreloadCache> cache,
                         const QString& projectRoot);
    void playEffect(const QString& file, int volume, int pitch = 100, int pan = 0);
    void playVoice(const QString& file, int volume, int pitch = 100, int pan = 0);
    void stopVoice();
    bool voicePlaying() const;
    void stopEffects();
    void playChannel(const QString& channel, const QString& file, int volume, bool loop,
                     int fadeInMs = 0, int transitionMs = 0, int pitch = 100, int pan = 0);
    void stopChannel(const QString& channel, int fadeOutMs = 0);
    /// Relê QSettings uma única vez e reaplica volumes nos canais ativos.
    /// Reprodução normal nunca consulta o registro/disco depois do warmup.
    void refreshSettings();

private:
    QObject* m_owner = nullptr;
#ifdef TES_HAS_AUDIO
    enum class EffectMode { None, LowLatency, Media };
    struct EffectChannel {
        QSoundEffect* sound = nullptr;
        QMediaPlayer* player = nullptr;
        QAudioOutput* output = nullptr;
        QBuffer* buffer = nullptr;
        QString soundFile;
        QString mediaFile;
        quint64 lastUseSerial = 0;
        int baseVolume = 100;
        EffectMode mode = EffectMode::None;
    };

    void ensureSettingsCache();
    void loadSettingsCache();
    double channelScale(const char* channel) const;
    double outputVolume(int baseVolume, const char* channel) const;
    QUrl cachedSourceUrl(const QString& file);
    EffectChannel* acquireEffectChannel(const QString& file, bool lowLatency);
    bool effectChannelIdle(const EffectChannel& channel) const;
    void stopEffectChannel(EffectChannel& channel);
    void configureMediaSource(QMediaPlayer* player, QBuffer*& buffer, const QString& file);
    static void applyPlaybackControls(QMediaPlayer* player,QAudioOutput* output,int pitch,int pan);
    struct VoiceEntry { QString file; int volume=90,pitch=100,pan=0; };
    void playNextVoice();

    QMediaPlayer* m_voicePlayer = nullptr;
    QAudioOutput* m_voiceOut = nullptr;
    QBuffer* m_voiceBuffer = nullptr;
    QQueue<VoiceEntry> m_voiceQueue;
    bool m_voiceActive = false;
    QMediaPlayer* m_bgmPlayer = nullptr;
    QAudioOutput* m_bgmOut = nullptr;
    QBuffer* m_bgmBuffer = nullptr;
    QMediaPlayer* m_bgsPlayer = nullptr;
    QAudioOutput* m_bgsOut = nullptr;
    QBuffer* m_bgsBuffer = nullptr;
    QMediaPlayer* m_mePlayer = nullptr;
    QAudioOutput* m_meOut = nullptr;
    QBuffer* m_meBuffer = nullptr;
    QVector<EffectChannel> m_seChannels;
    QHash<QString, QUrl> m_sourceUrlCache;
    quint64 m_seSerial = 0;
    std::shared_ptr<core::RuntimePreloadCache> m_preloadCache;
    QString m_projectRoot;

    bool m_settingsLoaded = false;
    int m_masterVolume = 100;
    int m_voiceVolume = 100;
    int m_bgmVolume = 100;
    int m_bgsVolume = 100;
    int m_meVolume = 100;
    int m_seVolume = 100;

    int m_voiceBaseVolume = 100;
    int m_bgmBaseVolume = 100;
    int m_bgsBaseVolume = 100;
    int m_meBaseVolume = 100;
#endif
};

} // namespace game
