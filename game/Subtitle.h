// ============================================================================
//  Subtitle.h — Legendas (porte do SubtitleSystem.js, formato de eventos compatível).
//
//  Legenda NÃO é a caixa de mensagem: ela não bloqueia o jogo por padrão, tem
//  duração própria, aceita várias na tela ao mesmo tempo e pode seguir um
//  personagem. As duas compartilham o motor de texto (`TextBox`), e é só isso.
//
//  Como todo o runtime, esta classe não conhece interface: ela calcula estado
//  (fase, opacidade, quantas letras apareceram, deslocamento da transição) e
//  quem desenha é a janela. Dá para rodar uma cena inteira de legendas num
//  teste sem abrir tela.
// ============================================================================
#pragma once

#include "TextBox.h"
#include "core/SubtitleStyle.h"
#include "core/TextEffects.h"

#include <QFont>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <QHash>
#include <QQueue>

#include <functional>

namespace game {

/// A quem a legenda se prende (segue na tela).
enum class SubtitleAnchor { Screen, Player, Event };

/// Tudo que uma legenda precisa para nascer. O que não for preenchido vem do
/// `core::SubtitleStyle` do projeto.
struct SubtitleRequest {
    core::SubtitleTrack track = core::SubtitleTrack::Dialogue;
    QString text;
    QString speakerName;
    int     durationFrames = -1;      ///< -1 = padrão do projeto; 0 = automático pelo texto
    bool    waitForInput = false;
    bool    hasWaitForInput = false;  ///< o comando falou sobre isto?
    bool    typewriter = false;
    bool    hasTypewriter = false;
    double  typewriterSpeed = -1.0;

    core::SubtitlePosition position = core::SubtitlePosition::Bottom;
    bool    hasPosition = false;
    core::SubtitleAlign textAlign = core::SubtitleAlign::Center;
    bool    hasTextAlign = false;
    core::SubtitleAlign boxAlign = core::SubtitleAlign::Center;
    bool    hasBoxAlign = false;

    QColor  fontColor;                ///< inválida = padrão
    QColor  nameColor;
    int     fontSize = -1;
    core::TextGradientSpec gradient;
    core::TextEffectStack effects;

    core::SubtitleTransition transitionIn  = core::SubtitleTransition::None;
    bool    hasTransitionIn = false;
    core::SubtitleTransition transitionOut = core::SubtitleTransition::None;
    bool    hasTransitionOut = false;

    SubtitleAnchor anchor = SubtitleAnchor::Screen;
    QString anchorEventId;
    int     anchorOffsetY = -16;

    int     offsetX = 0, offsetY = 0;

    QString voiceFile;                ///< áudio da fala (opcional)
    int     voiceVolume = 90;
    QString soundEffect;              ///< som ao aparecer a linha
    int     soundVolume = 80;
};

/// Uma legenda viva na tela.
struct Subtitle {
    enum Phase { FadeIn, Show, FadeOut, Done };

    QString id;
    core::SubtitleTrack track = core::SubtitleTrack::Dialogue;
    core::SubtitleStyle style;
    QString speakerName;
    QVector<TextPage> pages;          ///< legenda usa sempre a página 0
    QSizeF  textSize;                 ///< tamanho do bloco de texto, em px

    Phase   phase = FadeIn;
    double  opacity = 0.0;
    double  ageSec = 0.0;             ///< tempo de vida (alimenta os efeitos)
    double  remainingSec = 3.0;
    double  fadeInSec = 0.25, fadeOutSec = 0.25;
    /// Relógio próprio da saída do Rich Text. Mantém o preset Exit vivo mesmo
    /// quando o fade visual da legenda é zero ou mais curto que o stagger.
    double  exitAgeSec = 0.0;

    bool    waitForInput = false;
    bool    waitingForInput = false;  ///< terminou de digitar e espera o OK
    bool    typewriter = false;
    double  typewriterSpeed = 45.0;
    int     revealed = 0;             ///< letras já visíveis
    QVector<double> revealTimesSec;   ///< nascimento de cada glyph para Entrada compartilhada
    double  charAcc = 0.0;

    core::SubtitlePosition position = core::SubtitlePosition::Bottom;
    core::SubtitleAlign    textAlign = core::SubtitleAlign::Center;
    core::SubtitleAlign    boxAlign = core::SubtitleAlign::Center;
    QColor  fontColor, nameColor;
    int     fontSize = 22;
    core::TextGradientSpec gradient;
    core::TextEffectStack effects;

    core::SubtitleTransition transitionIn = core::SubtitleTransition::None;
    core::SubtitleTransition transitionOut = core::SubtitleTransition::None;
    double  transitionProgress = 1.0; ///< 0..1 (1 = totalmente no lugar)

    SubtitleAnchor anchor = SubtitleAnchor::Screen;
    QString anchorEventId;
    int     anchorOffsetY = -16;
    int     offsetX = 0, offsetY = 0;

    QString voiceFile;
    int     voiceVolume = 90;

    int  totalChars() const { return pages.isEmpty() ? 0 : pages[0].drawableCount(); }
    bool finishedTyping() const { return revealed >= totalChars(); }
};

/// Gerencia a lista de legendas vivas. Sem interface, testável sem tela.
class SubtitleManager
{
public:
    /// A fonte é necessária para quebrar o texto (mesma métrica do desenho).
    void setFont(const QFont& f) { m_font = f; }
    QFont font() const { return m_font; }
    void setStyle(const core::SubtitleStyle& s) { m_style = s; }
    const core::SubtitleStyle& style() const { return m_style; }
    /// Largura da tela em pixels lógicos (limita a caixa).
    void setScreenWidth(double w) { m_screenW = qMax(80.0, w); }

    /// Toca a voz/SE da legenda. Quem liga isso é a janela (o runtime puro
    /// não sabe tocar som). Se ninguém ligar, a legenda funciona muda.
    using SoundHook = std::function<void(const QString& arquivo, int volume)>;
    void setVoiceHook(SoundHook h) { m_voice = std::move(h); }
    void setSeHook(SoundHook h) { m_se = std::move(h); }
    void setStopVoiceHook(std::function<void()> h) { m_stopVoice = std::move(h); }
    /// Resolve \v[n] no texto da legenda (liga na memória da partida).
    void setVariableResolver(std::function<QString(int)> r) { m_varResolver = std::move(r); }

    /// Cria uma legenda e devolve o id.
    QString show(const SubtitleRequest& req);
    QString enqueue(const SubtitleRequest& req);
    void clearQueue(core::SubtitleTrack track);
    void clearTrack(core::SubtitleTrack track, bool withFade=false);
    /// Remove tudo agora (usado pelo "Limpar Imediato" e pelo pulo de cutscene).
    void clearAll();
    /// Manda todas para o fade de saída.
    void clearWithFade();

    void update(double dt);
    /// O jogador apertou confirmar: avança a legenda que estiver esperando.
    /// Devolve true se alguma legenda consumiu a tecla.
    bool confirm();

    const QVector<Subtitle>& subtitles() const { return m_subs; }
    bool empty() const { return m_subs.isEmpty(); }
    /// Alguma legenda ainda está na tela? (usado pelo comando "esperar")
    bool busy() const;
    bool busy(core::SubtitleTrack track) const;
    /// Alguma legenda pede que o mapa fique congelado?
    bool freezesMap() const;

private:
    void layout(Subtitle& s, const QString& texto);
    QString showNow(const SubtitleRequest& req);
    void startQueuedTracks();

    core::SubtitleStyle m_style;
    QFont   m_font;
    double  m_screenW = 800.0;
    QVector<Subtitle> m_subs;
    QHash<int,QQueue<SubtitleRequest>> m_queues;
    SoundHook m_voice, m_se;
    std::function<void()> m_stopVoice;
    std::function<QString(int)> m_varResolver;
};

/// Lê e REMOVE do texto os códigos que valem para a legenda inteira:
/// \TI[tipo], \TO[tipo] e \TY (som de digitação). Os demais códigos seguem
/// para o formatador normal.
struct SubtitleCodes {
    core::SubtitleTransition transitionIn  = core::SubtitleTransition::None;
    bool hasIn = false;
    core::SubtitleTransition transitionOut = core::SubtitleTransition::None;
    bool hasOut = false;
    bool typewriterSound = false;
};
SubtitleCodes extractSubtitleCodes(QString& text);

/// Deslocamento e escala de uma transição, dado o progresso 0..1.
/// Devolve o deslocamento em px e a escala (1 = tamanho normal).
void transitionState(core::SubtitleTransition t, double progress, bool entering,
                     QPointF* offset, QPointF* scale);

} // namespace game
