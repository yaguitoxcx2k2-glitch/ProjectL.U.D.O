#pragma once

namespace core {

/// Opções que o autor pode expor ao jogador. Os valores pessoais continuam em
/// QSettings no PC do jogador e não alteram o projeto compartilhado.
struct GameAccessibilitySettings {
    bool enabled = true;
    bool allowUiScale = true;
    int defaultUiScalePercent = 100; // 100..150
    bool allowReducedMotion = true;
    bool reduceShakeDefault = false;
    bool reduceFlashDefault = false;
    bool strongFocusDefault = false;
    int defaultTextSpeedPercent = 100; // 50..200
};

} // namespace core
