#pragma once

#include "core/Editor.h"
#include "core/TextEffects.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
QT_END_NAMESPACE

namespace ui {

/// Editor No-Code compartilhado de Rich Text: gradiente + Entrada/Loop/Saída.
/// Os presets apenas preenchem TextEffectPhaseSpec; o runtime nunca depende do
/// nome do preset. Mensagens, Legendas, Pictures e Choices persistem o mesmo
/// contrato core::TextEffectStack.
class TextEffectsEditorWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit TextEffectsEditorWidget(core::Editor& editor,
                                     const core::TextEffectStack& effects = {},
                                     const core::TextGradientSpec& gradient = {},
                                     QWidget* parent = nullptr);

    core::TextEffectStack effects() const { return m_effects; }
    core::TextGradientSpec gradient() const;
    void setSampleText(const QString& text);

signals:
    void changed();

private:
    core::TextEffectPhaseSpec* currentPhase();
    const core::TextEffectPhaseSpec* currentPhase() const;
    void loadPhase();
    void storePhase();
    void refreshGradientList();
    void refreshPreview();
    void applyPreset();
    void saveCustomPreset();

    core::Editor& ed;
    core::TextEffectStack m_effects;
    core::TextGradientSpec m_gradient;
    QString m_sampleText = QStringLiteral("LUDO Game Engine");
    bool m_loading = false;

    QCheckBox* m_gradientEnabled = nullptr;
    QComboBox* m_gradientDirection = nullptr;
    QListWidget* m_gradientColors = nullptr;

    QComboBox* m_phase = nullptr;
    QCheckBox* m_enabled = nullptr;
    QCheckBox* m_continuous = nullptr;
    QCheckBox* m_fadeReveal = nullptr;
    QSpinBox* m_fadeRevealMs = nullptr;
    QComboBox* m_preset = nullptr;
    QComboBox* m_target = nullptr;
    QComboBox* m_motion = nullptr;
    QComboBox* m_easing = nullptr;
    QComboBox* m_loopMode = nullptr;
    QSpinBox* m_loopCount = nullptr;
    QSpinBox* m_duration = nullptr;
    QSpinBox* m_delay = nullptr;
    QSpinBox* m_stagger = nullptr;
    QDoubleSpinBox* m_opacityFrom = nullptr;
    QDoubleSpinBox* m_opacityTo = nullptr;
    QDoubleSpinBox* m_xFrom = nullptr;
    QDoubleSpinBox* m_xTo = nullptr;
    QDoubleSpinBox* m_yFrom = nullptr;
    QDoubleSpinBox* m_yTo = nullptr;
    QDoubleSpinBox* m_scaleXFrom = nullptr;
    QDoubleSpinBox* m_scaleXTo = nullptr;
    QDoubleSpinBox* m_scaleYFrom = nullptr;
    QDoubleSpinBox* m_scaleYTo = nullptr;
    QDoubleSpinBox* m_rotationFrom = nullptr;
    QDoubleSpinBox* m_rotationTo = nullptr;
    QDoubleSpinBox* m_amount = nullptr;
    QDoubleSpinBox* m_frequency = nullptr;
    QWidget* m_preview = nullptr;
};

} // namespace ui
