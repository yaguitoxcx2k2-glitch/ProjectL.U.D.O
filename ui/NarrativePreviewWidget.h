#pragma once

#include "core/EventModel.h"

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

namespace core { class Editor; }

namespace ui {

/// Preview narrativo do Event Editor. Mensagens, legendas e escolhas passam
/// pelo mesmo TextBox/TextDraw do Player; o widget só fornece o palco.
class NarrativePreviewWidget final : public QWidget
{
public:
    explicit NarrativePreviewWidget(core::Editor& editor, QWidget* parent = nullptr);

    void setCommand(const core::EventCommand& command);
    void clearCommand();
    void play();
    void pause();
    bool isPlayable() const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString resolvedText() const;
    QString resolvedSpeaker() const;
    int revealedGlyphs(int total) const;

    core::Editor& m_editor;
    core::EventCommand m_command;
    QElapsedTimer m_clock;
    QTimer* m_timer = nullptr;
    qint64 m_pausedMs = 0;
    bool m_playing = false;
};

} // namespace ui
