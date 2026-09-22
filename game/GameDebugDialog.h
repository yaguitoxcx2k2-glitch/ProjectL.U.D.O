#pragma once

#include <QDialog>

class QLabel;
class QCheckBox;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace game { class GameSession; class LiveEventInspector; class RuntimeProfilerChart; }

namespace game {

/// Debugger visual do playtest (F8). Não entra como atalho no jogo exportado.
/// Pause/Step/Continue atuam no Interpreter real, inclusive Common Events.
class GameDebugDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GameDebugDialog(GameSession& session, QWidget* parent = nullptr);

private:
    void refresh();
    void updateBreakpointButton();
    GameSession& m_session;
    QLabel* m_overview = nullptr;
    QLabel* m_execution = nullptr;
    QLabel* m_profiler = nullptr;
    RuntimeProfilerChart* m_profilerChart = nullptr;
    QLabel* m_uiState = nullptr;
    QTableWidget* m_state = nullptr;
    QTableWidget* m_trace = nullptr;
    QTableWidget* m_callStack = nullptr;
    QTableWidget* m_customDatabases = nullptr;
    QTableWidget* m_runtimeMaps = nullptr;
    QTableWidget* m_scheduler = nullptr;
    QTableWidget* m_input = nullptr;
    QLabel* m_inputAnalog = nullptr;
    QTableWidget* m_routes = nullptr;
    QListWidget* m_commonEvents = nullptr;
    QPushButton* m_breakpoint = nullptr;
    QPushButton* m_conditionalBreakpoint = nullptr;
    QCheckBox* m_routeOverlay = nullptr;
    LiveEventInspector* m_liveInspector = nullptr;
};

} // namespace game
