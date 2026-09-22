#pragma once

#include "core/Editor.h"
#include "core/EventModel.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QSpinBox;
QT_END_NAMESPACE

namespace ui {

class SpriteSheetView;

/// Seleciona personagem, direção e frame em folhas com vários personagens.
class CharacterGraphicDialog : public QDialog
{
public:
    CharacterGraphicDialog(core::Editor& ed, const core::EventGraphic& initial,
                           QWidget* parent = nullptr);

    core::EventGraphic graphic() const { return m_graphic; }
    static QVector<int> detectedLayout(const QSize& imageSize); // frames, dirs, charsX, charsY

private:
    void chooseAsset();
    void autoDetect();
    void syncFromControls();
    void syncControls();
    void updateInfo();

    core::Editor& m_ed;
    core::EventGraphic m_graphic;
    SpriteSheetView* m_view = nullptr;
    QLabel* m_info = nullptr;
    QSpinBox *m_frames = nullptr, *m_dirs = nullptr;
    QSpinBox *m_charsX = nullptr, *m_charsY = nullptr;
};

} // namespace ui
