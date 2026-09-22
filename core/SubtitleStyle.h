// ============================================================================
//  SubtitleStyle.h — Ajustes globais das legendas, gravados no projeto.
//
//  Porte do bloco de parametros do plugin SubtitleSystem.js (formato de eventos compatível):
//  cada legenda pode sobrescrever o que quiser, e o que ela nao disser vem
//  daqui. Fica no nucleo (e nao no runtime) porque e dado de projeto: o editor
//  edita, o ProjectIO grava, o runtime so le.
// ============================================================================
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QHash>
#include <QString>

namespace core {

/// Onde a legenda aparece na tela.
enum class SubtitlePosition { Top, Middle, Bottom };

/// Alinhamento (do texto dentro da caixa e da caixa dentro da tela).
enum class SubtitleAlign { Left, Center, Right };

/// Fundo da caixa de legenda.
enum class SubtitleBg { None, Solid, Gradient, Glass };

/// Transicoes de entrada/saida (\TI[..] e \TO[..] no texto).
enum class SubtitleTransition {
    None, SlideUp, SlideDown, SlideLeft, SlideRight,
    ZoomIn, ZoomOut, Bounce, FlipX, FlipY
};

enum class SubtitleTrack { Dialogue, Notification, System, Custom1, Custom2 };
QString subtitleTrackId(SubtitleTrack track);
SubtitleTrack subtitleTrackFromId(const QString& id);

struct SubtitleTrackSettings {
    int maxSimultaneous = 3;
    int minDurationFrames = 90;
    double charsPerSecond = 18.0;
    QJsonObject styleOverrides;
    QJsonObject toJson() const;
    static SubtitleTrackSettings fromJson(const QJsonObject& object);
};

QString            subtitlePositionId(SubtitlePosition p);
SubtitlePosition   subtitlePositionFromId(const QString& id);
QString            subtitleAlignId(SubtitleAlign a);
SubtitleAlign      subtitleAlignFromId(const QString& id);
QString            subtitleBgId(SubtitleBg b);
SubtitleBg         subtitleBgFromId(const QString& id);
QString            subtitleTransitionId(SubtitleTransition t);
SubtitleTransition subtitleTransitionFromId(const QString& id);

/// Valores padrao das legendas (equivalente ao SubtitleSystem.DEFAULTS).
struct SubtitleStyle {
    // ---- tempo (em quadros de 60 fps, como no plugin) --------------------
    int    defaultDuration = 180;
    int    fadeInFrames    = 15;
    int    fadeOutFrames   = 15;

    // ---- texto ------------------------------------------------------------
    QString fontFamily;              ///< vazio = fonte principal do projeto
    int    fontSize      = 22;
    QColor fontColor     = QColor("#FFFFFF");
    bool   fontBold      = false;
    bool   fontItalic    = false;
    QColor outlineColor  = QColor(0, 0, 0, 230);
    int    outlineWidth  = 3;
    bool   shadow        = false;
    QColor shadowColor   = QColor(0, 0, 0, 180);
    int    shadowOffsetX = 2;
    int    shadowOffsetY = 2;

    // ---- nome de quem fala -------------------------------------------------
    int    nameFontSize = 18;
    QColor nameColor    = QColor("#FFD700");
    SubtitleAlign nameAlign = SubtitleAlign::Left; ///< alinhamento do nome dentro da caixa

    // ---- caixa -------------------------------------------------------------
    SubtitleBg    bgStyle    = SubtitleBg::Solid;
    QColor        bgColor    = QColor(0, 0, 0, 180);
    int           bgRadius   = 10;
    int           paddingH   = 20;
    int           paddingV   = 10;
    int           maxWidth   = 700;
    int           marginBottom     = 40;
    int           marginHorizontal = 20;
    SubtitlePosition position  = SubtitlePosition::Bottom;
    SubtitleAlign    textAlign = SubtitleAlign::Center;
    SubtitleAlign    boxAlign  = SubtitleAlign::Center;

    // ---- comportamento ------------------------------------------------------
    int  maxSimultaneous = 3;    ///< quantas legendas podem coexistir
    bool waitForInput    = false;///< por padrao, espera o jogador confirmar?
    bool freezeMap       = false;///< congela jogador/eventos enquanto exibe
    bool typewriter      = false;///< texto letra a letra por padrao
    double typewriterSpeed = 45.0; ///< letras por segundo
    bool inputIndicator  = true; ///< mostra o ▼ quando espera confirmar
    /// Transicoes padrao (podem ser trocadas por \TI[..] / \TO[..]).
    SubtitleTransition transitionIn  = SubtitleTransition::None;
    SubtitleTransition transitionOut = SubtitleTransition::None;

    // ---- deslocamento global -------------------------------------------------
    int offsetX = 0, offsetY = 0;

    QHash<QString, SubtitleTrackSettings> tracks;
    SubtitleTrackSettings trackSettings(SubtitleTrack track) const;
    SubtitleStyle resolvedForTrack(SubtitleTrack track) const;

    QJsonObject toJson() const;
    static SubtitleStyle fromJson(const QJsonObject& o);
};

} // namespace core
