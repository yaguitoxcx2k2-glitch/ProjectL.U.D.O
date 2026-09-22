#include "RuntimeAudio.h"
#include "RuntimeAudioPolicy.h"
#include "core/RuntimePreloadCache.h"

#include <QtGlobal>
#include <utility>

#ifdef TES_HAS_AUDIO
#include <QAudioOutput>
#include <QBuffer>
#include <QEasingCurve>
#include <QMediaPlayer>
#include <QObject>
#include <QPropertyAnimation>
#include <QSettings>
#include <QSoundEffect>
#include <QUrl>
#include <QVariant>
#endif

namespace game {

void RuntimeAudio::setPreloadCache(std::shared_ptr<core::RuntimePreloadCache> cache,
                                   const QString& projectRoot)
{
#ifdef TES_HAS_AUDIO
    m_preloadCache = std::move(cache);
    m_projectRoot = projectRoot;
#else
    Q_UNUSED(cache); Q_UNUSED(projectRoot);
#endif
}

#ifdef TES_HAS_AUDIO
void RuntimeAudio::configureMediaSource(QMediaPlayer* player, QBuffer*& buffer,
                                        const QString& file)
{
    if (!player) return;
    if (buffer) {
        player->setSource(QUrl());
        buffer->deleteLater();
        buffer = nullptr;
    }
    const QByteArray bytes = m_preloadCache
        ? m_preloadCache->audioData(m_projectRoot, file) : QByteArray();
    if (!bytes.isEmpty()) {
        buffer = new QBuffer(player);
        buffer->setData(bytes);
        buffer->open(QIODevice::ReadOnly);
        player->setSourceDevice(buffer, cachedSourceUrl(file));
    } else {
        player->setSource(cachedSourceUrl(file));
    }
}

void RuntimeAudio::ensureSettingsCache()
{
    if (!m_settingsLoaded) loadSettingsCache();
}

void RuntimeAudio::loadSettingsCache()
{
    // O8: QSettings sai do hot path. Só esta função toca no armazenamento
    // persistente; playEffect/playChannel usam os inteiros já cacheados.
    QSettings settings;
    m_masterVolume = qBound(0, settings.value(QStringLiteral("audio/master"), 100).toInt(), 100);
    m_bgmVolume = qBound(0, settings.value(QStringLiteral("audio/bgm"), 100).toInt(), 100);
    m_bgsVolume = qBound(0, settings.value(QStringLiteral("audio/bgs"), 100).toInt(), 100);
    m_meVolume = qBound(0, settings.value(QStringLiteral("audio/me"), 100).toInt(), 100);
    m_seVolume = qBound(0, settings.value(QStringLiteral("audio/se"), 100).toInt(), 100);
    m_voiceVolume = qBound(0, settings.value(QStringLiteral("audio/voice"), 100).toInt(), 100);
    m_settingsLoaded = true;
}

double RuntimeAudio::channelScale(const char* channel) const
{
    const int local = channel == nullptr ? 100
        : qstrcmp(channel, "bgm") == 0 ? m_bgmVolume
        : qstrcmp(channel, "bgs") == 0 ? m_bgsVolume
        : qstrcmp(channel, "me") == 0 ? m_meVolume
        : qstrcmp(channel, "voice") == 0 ? m_voiceVolume
        : m_seVolume;
    return (m_masterVolume / 100.0) * (local / 100.0);
}

double RuntimeAudio::outputVolume(int baseVolume, const char* channel) const
{
    return qBound(0.0, baseVolume / 100.0, 1.0) * channelScale(channel);
}

QUrl RuntimeAudio::cachedSourceUrl(const QString& file)
{
    const auto found = m_sourceUrlCache.constFind(file);
    if (found != m_sourceUrlCache.cend()) return found.value();

    // O cache é propositalmente pequeno e de política simples. URLs locais são
    // baratos de reconstruir; ao atingir o teto, limpar tudo evita um LRU caro
    // no próprio hot path e impede crescimento sem limite em jogos longos.
    if (m_sourceUrlCache.size() >= audio_policy::SourceUrlCacheEntries)
        m_sourceUrlCache.clear();
    const QUrl url = QUrl::fromLocalFile(file);
    m_sourceUrlCache.insert(file, url);
    return url;
}

bool RuntimeAudio::effectChannelIdle(const EffectChannel& channel) const
{
    if (channel.mode == EffectMode::LowLatency)
        return !channel.sound || (!channel.sound->isPlaying() &&
               !channel.sound->property("ludoPendingPlay").toBool());
    if (channel.mode == EffectMode::Media)
        return !channel.player || channel.player->playbackState() == QMediaPlayer::StoppedState;
    return true;
}

void RuntimeAudio::stopEffectChannel(EffectChannel& channel)
{
    if (channel.sound) channel.sound->setProperty("ludoPendingPlay", false);
    if (channel.sound && channel.sound->isPlaying()) channel.sound->stop();
    if (channel.player && channel.player->playbackState() != QMediaPlayer::StoppedState)
        channel.player->stop();
    channel.mode = EffectMode::None;
}

RuntimeAudio::EffectChannel* RuntimeAudio::acquireEffectChannel(const QString& file, bool lowLatency)
{
    EffectChannel* firstIdle = nullptr;
    EffectChannel* oldest = nullptr;

    // Primeiro prefira um canal ocioso que já tenha este mesmo asset carregado.
    // É o caso que evita decode/preparação repetida de passos, cursor, hit etc.
    for (EffectChannel& channel : m_seChannels) {
        const bool idle = effectChannelIdle(channel);
        const bool sameSource = lowLatency ? channel.soundFile == file : channel.mediaFile == file;
        if (idle && sameSource) return &channel;
        if (idle && !firstIdle) firstIdle = &channel;
        if (!oldest || channel.lastUseSerial < oldest->lastUseSerial) oldest = &channel;
    }

    if (firstIdle) return firstIdle;
    if (m_seChannels.size() < audio_policy::MaxEffectChannels) {
        m_seChannels.push_back(EffectChannel{});
        return &m_seChannels.last();
    }

    // Compatível com o teto pré-O8: no máximo 24 SEs simultâneos. Se todos os
    // canais estiverem ocupados, o mais antigo é roubado em vez de alocar o 25º.
    if (oldest) {
        stopEffectChannel(*oldest);
        return oldest;
    }
    return nullptr;
}
#endif

#ifdef TES_HAS_AUDIO
void RuntimeAudio::applyPlaybackControls(QMediaPlayer* player,QAudioOutput* output,int pitch,int pan)
{
    if(player)player->setPlaybackRate(qBound(50,pitch,200)/100.0);
    if(output){const double balance=qBound(-100,pan,100)/100.0;
        // Propriedade neutra para backends que oferecem balance estéreo.
        // Também fica registrada para um mixer PCM substituir o backend sem
        // mudar o contrato dos comandos/saves.
        output->setProperty("balance",balance);output->setProperty("ludoPan",balance);}
}
#endif

void RuntimeAudio::playEffect(const QString& file, int volume, int pitch, int pan)
{
#ifdef TES_HAS_AUDIO
    if (file.isEmpty() || !m_owner) return;
    ensureSettingsCache();

    const bool lowLatency = audio_policy::prefersLowLatencyEffect(file)&&pitch==100&&pan==0;
    EffectChannel* channel = acquireEffectChannel(file, lowLatency);
    if (!channel) return;

    stopEffectChannel(*channel);
    channel->baseVolume = qBound(0, volume, 100);
    channel->lastUseSerial = ++m_seSerial;

    if (lowLatency) {
        if (!channel->sound) {
            channel->sound = new QSoundEffect(m_owner);
            QObject::connect(channel->sound, &QSoundEffect::statusChanged, channel->sound,
                             [sound = channel->sound] {
                if (sound->status() == QSoundEffect::Ready &&
                    sound->property("ludoPendingPlay").toBool()) {
                    sound->setProperty("ludoPendingPlay", false);
                    sound->play();
                }
            });
        }
        if (channel->soundFile != file) {
            channel->sound->setSource(cachedSourceUrl(file));
            channel->soundFile = file;
        }
        channel->sound->setVolume(outputVolume(channel->baseVolume, "se"));
        channel->mode = EffectMode::LowLatency;
        if (channel->sound->status() == QSoundEffect::Ready) {
            channel->sound->setProperty("ludoPendingPlay", false);
            channel->sound->play();
        } else {
            // QSoundEffect carrega WAV de forma assíncrona. O primeiro play()
            // antes de Ready podia cortar ou perder o início do SE de legenda.
            channel->sound->setProperty("ludoPendingPlay", true);
        }
        return;
    }

    if (!channel->player) {
        channel->player = new QMediaPlayer(m_owner);
        channel->output = new QAudioOutput(channel->player);
        channel->player->setAudioOutput(channel->output);
    }
    if (channel->mediaFile != file) {
        configureMediaSource(channel->player, channel->buffer, file);
        channel->mediaFile = file;
    }
    channel->output->setVolume(outputVolume(channel->baseVolume, "se"));
    applyPlaybackControls(channel->player,channel->output,pitch,pan);
    channel->mode = EffectMode::Media;
    channel->player->play();
#else
    Q_UNUSED(file); Q_UNUSED(volume); Q_UNUSED(pitch); Q_UNUSED(pan);
#endif
}

void RuntimeAudio::playVoice(const QString& file, int volume, int pitch, int pan)
{
#ifdef TES_HAS_AUDIO
    if (file.isEmpty() || !m_owner) return;
    m_voiceQueue.enqueue({file,qBound(0,volume,100),qBound(50,pitch,200),qBound(-100,pan,100)});
    if(m_voiceActive)return;
    playNextVoice();
#else
    Q_UNUSED(file); Q_UNUSED(volume); Q_UNUSED(pitch); Q_UNUSED(pan);
#endif
}

#ifdef TES_HAS_AUDIO
void RuntimeAudio::playNextVoice()
{
    if(m_voiceQueue.isEmpty()){m_voiceActive=false;return;}
    ensureSettingsCache();
    if (!m_voicePlayer) {
        m_voicePlayer = new QMediaPlayer(m_owner);
        m_voiceOut = new QAudioOutput(m_owner);
        m_voicePlayer->setAudioOutput(m_voiceOut);
        QObject::connect(m_voicePlayer,&QMediaPlayer::mediaStatusChanged,m_voicePlayer,[this](QMediaPlayer::MediaStatus status){if(status==QMediaPlayer::EndOfMedia||status==QMediaPlayer::InvalidMedia){m_voiceActive=false;playNextVoice();}});
    }
    const VoiceEntry entry=m_voiceQueue.dequeue();m_voiceActive=true;
    m_voiceBaseVolume = entry.volume;
    m_voiceOut->setVolume(outputVolume(m_voiceBaseVolume, "voice"));
    applyPlaybackControls(m_voicePlayer,m_voiceOut,entry.pitch,entry.pan);
    configureMediaSource(m_voicePlayer, m_voiceBuffer, entry.file);
    m_voicePlayer->play();
}
#endif

void RuntimeAudio::stopVoice()
{
#ifdef TES_HAS_AUDIO
    m_voiceQueue.clear();m_voiceActive=false;
    if (m_voicePlayer) m_voicePlayer->stop();
#endif
}

bool RuntimeAudio::voicePlaying() const
{
#ifdef TES_HAS_AUDIO
    return m_voiceActive||!m_voiceQueue.isEmpty();
#else
    return false;
#endif
}

void RuntimeAudio::stopEffects()
{
#ifdef TES_HAS_AUDIO
    // O8: parar não destrói o pool. Os mesmos objetos permanecem prontos para
    // o próximo SE, evitando new/delete e setup de áudio em gameplay normal.
    for (EffectChannel& channel : m_seChannels) stopEffectChannel(channel);
#endif
}

void RuntimeAudio::playChannel(const QString& channel, const QString& file, int volume, bool loop,
                               int fadeInMs, int transitionMs, int pitch, int pan)
{
#ifdef TES_HAS_AUDIO
    if (file.isEmpty() || !m_owner) return;
    ensureSettingsCache();
    if (channel == QLatin1String("se")) { playEffect(file, volume,pitch,pan); return; }
    if (channel == QLatin1String("voice")) { playVoice(file, volume,pitch,pan); return; }

    QMediaPlayer** player = &m_mePlayer;
    QAudioOutput** output = &m_meOut;
    QBuffer** buffer = &m_meBuffer;
    int* baseVolume = &m_meBaseVolume;
    const char* settingsChannel = "me";
    if (channel == QLatin1String("bgm")) {
        player = &m_bgmPlayer; output = &m_bgmOut; buffer = &m_bgmBuffer;
        baseVolume = &m_bgmBaseVolume; settingsChannel = "bgm";
    } else if (channel == QLatin1String("bgs")) {
        player = &m_bgsPlayer; output = &m_bgsOut; buffer = &m_bgsBuffer;
        baseVolume = &m_bgsBaseVolume; settingsChannel = "bgs";
    }

    const int boundedVolume = qBound(0, volume, 100);
    const double targetVolume = outputVolume(boundedVolume, settingsChannel);
    const int fadeMs = qBound(0, fadeInMs, 60000);
    const int crossMs = (channel == QLatin1String("bgm") || channel == QLatin1String("bgs"))
                            ? qBound(0, transitionMs, 60000) : 0;

    QMediaPlayer* oldPlayer = *player;
    QAudioOutput* oldOutput = *output;
    const bool crossfade = crossMs > 0 && oldPlayer && oldOutput &&
                           oldPlayer->playbackState() != QMediaPlayer::StoppedState;

    if (crossfade) {
        // Crossfade precisa legitimamente de dois players simultâneos. Este é
        // um caminho raro/de transição e não o hot path de SE atacado pelo O8.
        auto* incomingPlayer = new QMediaPlayer(m_owner);
        auto* incomingOutput = new QAudioOutput(incomingPlayer);
        incomingPlayer->setAudioOutput(incomingOutput);
        *player = incomingPlayer;
        *output = incomingOutput;
        *baseVolume = boundedVolume;
        incomingPlayer->setLoops(loop ? QMediaPlayer::Infinite : 1);
        incomingOutput->setVolume(0.0);
        QBuffer* incomingBuffer = nullptr;
        configureMediaSource(incomingPlayer, incomingBuffer, file);
        *buffer = incomingBuffer;
        applyPlaybackControls(incomingPlayer,incomingOutput,pitch,pan);
        incomingPlayer->play();

        auto* fadeOld = new QPropertyAnimation(oldOutput, "volume", oldPlayer);
        fadeOld->setDuration(crossMs);
        fadeOld->setStartValue(oldOutput->volume());
        fadeOld->setEndValue(0.0);
        fadeOld->setEasingCurve(QEasingCurve::InOutQuad);
        QObject::connect(fadeOld, &QPropertyAnimation::finished, oldPlayer, [oldPlayer] {
            oldPlayer->stop();
            oldPlayer->deleteLater();
        });
        fadeOld->start(QAbstractAnimation::DeleteWhenStopped);

        auto* fadeNew = new QPropertyAnimation(incomingOutput, "volume", incomingPlayer);
        fadeNew->setDuration(crossMs);
        fadeNew->setStartValue(0.0);
        fadeNew->setEndValue(targetVolume);
        fadeNew->setEasingCurve(QEasingCurve::InOutQuad);
        fadeNew->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }

    if (!*player) {
        *player = new QMediaPlayer(m_owner);
        *output = new QAudioOutput(*player);
        (*player)->setAudioOutput(*output);
    }
    *baseVolume = boundedVolume;
    (*player)->setLoops(loop ? QMediaPlayer::Infinite : 1);
    configureMediaSource(*player, *buffer, file);
    applyPlaybackControls(*player,*output,pitch,pan);

    if (fadeMs > 0) {
        (*output)->setVolume(0.0);
        (*player)->play();
        auto* fade = new QPropertyAnimation(*output, "volume", *player);
        fade->setDuration(fadeMs);
        fade->setStartValue(0.0);
        fade->setEndValue(targetVolume);
        fade->setEasingCurve(QEasingCurve::InOutQuad);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        (*output)->setVolume(targetVolume);
        (*player)->play();
    }
#else
    Q_UNUSED(channel); Q_UNUSED(file); Q_UNUSED(volume); Q_UNUSED(loop); Q_UNUSED(fadeInMs); Q_UNUSED(transitionMs); Q_UNUSED(pitch); Q_UNUSED(pan);
#endif
}

void RuntimeAudio::stopChannel(const QString& channel, int fadeOutMs)
{
#ifdef TES_HAS_AUDIO
    if (channel == QLatin1String("se")) { stopEffects(); return; }
    if (channel == QLatin1String("voice")) { stopVoice(); return; }
    QMediaPlayer* player = channel == QLatin1String("bgm") ? m_bgmPlayer
                         : channel == QLatin1String("bgs") ? m_bgsPlayer
                         : channel == QLatin1String("me") ? m_mePlayer : nullptr;
    QAudioOutput* output = channel == QLatin1String("bgm") ? m_bgmOut
                         : channel == QLatin1String("bgs") ? m_bgsOut
                         : channel == QLatin1String("me") ? m_meOut : nullptr;
    if (!player) return;
    const int duration = qBound(0, fadeOutMs, 60000);
    if (duration <= 0 || !output || player->playbackState() == QMediaPlayer::StoppedState) {
        player->stop();
        return;
    }
    const double restore = output->volume();
    auto* fade = new QPropertyAnimation(output, "volume", player);
    fade->setDuration(duration);
    fade->setStartValue(output->volume());
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InOutQuad);
    QObject::connect(fade, &QPropertyAnimation::finished, player, [player, output, restore] {
        player->stop();
        if (output) output->setVolume(restore);
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
#else
    Q_UNUSED(channel); Q_UNUSED(fadeOutMs);
#endif
}

void RuntimeAudio::refreshSettings()
{
#ifdef TES_HAS_AUDIO
    loadSettingsCache();
    if (m_voiceOut) m_voiceOut->setVolume(outputVolume(m_voiceBaseVolume, "voice"));
    if (m_bgmOut) m_bgmOut->setVolume(outputVolume(m_bgmBaseVolume, "bgm"));
    if (m_bgsOut) m_bgsOut->setVolume(outputVolume(m_bgsBaseVolume, "bgs"));
    if (m_meOut) m_meOut->setVolume(outputVolume(m_meBaseVolume, "me"));
    for (EffectChannel& channel : m_seChannels) {
        if (channel.mode == EffectMode::LowLatency && channel.sound)
            channel.sound->setVolume(outputVolume(channel.baseVolume, "se"));
        else if (channel.mode == EffectMode::Media && channel.output)
            channel.output->setVolume(outputVolume(channel.baseVolume, "se"));
    }
#endif
}

} // namespace game
