#pragma once

#include <QDockWidget>

class QLabel;
class QLineEdit;
class QSlider;
class QTableWidget;
class QTimer;

namespace game { class GameSession; }

namespace ui {

/// Inspector do playtest conectado à GameSession real. Pode ficar dockado no
/// workbench F8 ou flutuar sobre o jogo via F9; nunca é empacotado como UI do jogo.
class LiveEventInspector : public QDockWidget
{
    Q_OBJECT
public:
    explicit LiveEventInspector(game::GameSession& session, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void addWatch();
    void removeWatch();
    void toggleBreakOnChange();
    void configureWatchCondition();
    void exportTrace(bool json);
    quint64 selectedWatchSerial() const;

    game::GameSession& m_session;
    QLabel* m_current = nullptr;
    QLabel* m_runtime = nullptr;
    QTableWidget* m_stack = nullptr;
    QTableWidget* m_watches = nullptr;
    QLineEdit* m_filter = nullptr;
    QTableWidget* m_trace = nullptr;
    QSlider* m_playback = nullptr;
    QLabel* m_playbackLabel = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace ui
