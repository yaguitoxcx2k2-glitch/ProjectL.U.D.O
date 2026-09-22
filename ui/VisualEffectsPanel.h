// ============================================================================
// VisualEffectsPanel.h — editor reutilizável de efeitos visuais.
//
// Não pertence mais ao sistema de Pictures: Panorama e qualquer outro objeto
// visual podem reutilizar o mesmo painel e o mesmo core::VisualEffects.
// ============================================================================
#pragma once

#include "core/VisualEffects.h"

#include <QColor>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

class VisualEffectsPanel : public QWidget
{
    Q_OBJECT
public:
    VisualEffectsPanel(core::Editor& ed, core::VisualEffects& fx, QWidget* parent = nullptr);
    void puxar();

signals:
    void changed();
    void previewTransitionIn();
    void previewTransitionOut();

private:
    core::Editor& ed;
    core::VisualEffects& m_fx;
    QCheckBox *m_borda = nullptr, *m_brilho = nullptr, *m_piscar = nullptr,
              *m_onda = nullptr, *m_desliza = nullptr, *m_mascara = nullptr,
              *m_tone = nullptr, *m_negative = nullptr, *m_brilhoPiscar = nullptr,
              *m_brilhoBorda = nullptr;
    QComboBox *m_bordaEstilo = nullptr, *m_bordaImg = nullptr, *m_mascaraImg = nullptr,
              *m_transIn = nullptr, *m_transOut = nullptr, *m_piscarModo = nullptr,
              *m_tintModo = nullptr; // legado, não exposto em novos comandos
    QSpinBox  *m_bordaSlice = nullptr, *m_bordaPadX = nullptr, *m_bordaPadY = nullptr,
              *m_transInQ = nullptr, *m_transOutQ = nullptr, *m_negativeTransicao = nullptr,
              *m_toneR = nullptr, *m_toneG = nullptr, *m_toneB = nullptr, *m_toneGray = nullptr;
    QDoubleSpinBox *m_bordaLarg = nullptr, *m_bordaEscala = nullptr, *m_bordaAlfa = nullptr,
                   *m_brilhoForca = nullptr, *m_brilhoPiscarVel = nullptr,
                   *m_piscarVel = nullptr, *m_piscarMin = nullptr, *m_piscarDelay = nullptr,
                   *m_tintForca = nullptr, *m_negativeForca = nullptr,
                   *m_ondaAmp = nullptr, *m_ondaComp = nullptr, *m_ondaVel = nullptr,
                   *m_deslizaVel = nullptr, *m_deslizaLarg = nullptr, *m_deslizaDelay = nullptr,
                   *m_mascaraX = nullptr, *m_mascaraY = nullptr, *m_mascaraEscalaX = nullptr,
                   *m_mascaraEscalaY = nullptr, *m_mascaraAngulo = nullptr;
    QCheckBox *m_bordaTileH = nullptr, *m_bordaTileV = nullptr, *m_mascaraInv = nullptr;
    QPushButton *m_bordaCor = nullptr, *m_bordaTint = nullptr, *m_brilhoCor = nullptr,
                *m_deslizaCor = nullptr, *m_tintCor = nullptr;
    QColor m_corBorda, m_corTint, m_corBrilho, m_corDesliza, m_corColorizar;
};

} // namespace ui
