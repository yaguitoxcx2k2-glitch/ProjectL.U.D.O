#pragma once

#include "core/Editor.h"
#include "game/GamepadInput.h"
#include "game/GameState.h"
#include "game/ui/UiCanvas.h"
#include "game/ui/UiTheme.h"

#include <QWidget>
#include <QImage>
#include <QPointer>
#include <QHash>
#include <QElapsedTimer>
#include <QSet>
#include <QVector>
#include <QStringList>

class QPushButton;
class QLabel;
class QShowEvent;
class QCloseEvent;
class QPaintEvent;
class QMouseEvent;
class QKeyEvent;
class QResizeEvent;
class QWheelEvent;
class QListWidget;

namespace player {

class PlayerWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerWindow(core::Editor& project, QWidget* parent = nullptr,
                          bool quitApplicationOnClose = true);
    ~PlayerWindow() override;
    /// Somente o playtest recebe o Editor vivo. No LudoPlayer exportado fica nulo.
    void setHotReloadSource(core::Editor* source) { m_hotReloadSource = source; }

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void newGame();
    void continueGame();
    void startGame(int loadSlot=0);
    void startGpuGame(const QPointF& startPixel, int loadSlot,
                      const QStringList& backends, int backendIndex = 0);
    void attachGame(QWidget* game);
    void pollTitleGamepad();
    bool hasDesignerTitle() const;
    bool designerScreenUsesAction(const QString& screen, const QString& actionType) const;
    void setTitleUiScreen(const QString& screen);
    void rebuildDesignerTitle();
    QPointF titleLogicalPoint(const QPointF& widgetPoint, bool* inside = nullptr) const;
    bool titleWidgetContainsPoint(const QString& id, const QPointF& logicalPoint) const;
    QStringList titleFocusableWidgets() const;
    bool titleWidgetEnabled(const QString& id) const;
    bool titleWidgetVisible(const QString& id) const;
    bool focusTitleWidget(const QString& id);
    bool moveTitleFocus(core::GameAction action);
    bool handleTitleNativeDirection(core::GameAction action);
    bool handleTitleTextKey(int key, const QString& text);
    core::UiWidgetSettings titleRuntimeWidget(const QString& id) const;
    bool applyTitleNativeActivation(const QString& id, const QPointF* logicalPoint = nullptr);
    bool activateTitleWidget(const QString& id, const QPointF* logicalPoint = nullptr);
    void showBuiltInLoadPanel();
    void hideBuiltInLoadPanel();
    void refreshBuiltInTitleLocalization();
    void executeTitleEvent(const QString& id, const QString& trigger);
    void executeTitleAction(const QString& elementId, const core::UiEventActionSettings& action);
    void startTitleVisualLogic(const QString& elementId, const QString& trigger);
    void updateTitleVisualLogic(int deltaMs);
    bool titleConditionPasses(const core::UiEventConditionSettings& condition) const;
    bool hasAnySave() const;
    core::Editor& m_project;
    QPointer<core::Editor> m_hotReloadSource;
    QPointer<QWidget> m_game;
    QLabel* m_titleLabel=nullptr;
    QPushButton* m_continue=nullptr;
    QImage m_background;
    game::GamepadInput m_gamepad;
    QSet<core::GameAction> m_gamepadHeld;
    QVector<QPushButton*> m_titleButtons;
    bool m_designerTitle = false;
    bool m_hasSave = false;
    game::GameState m_titleState;
    game::ui::UiCanvas m_titleCanvas;
    game::ui::UiTheme m_titleTheme;
    QString m_titleFocusId;
    QString m_titleHoverId;
    QHash<QString, QString> m_titleRuntimeStates;
    QHash<QString, bool> m_titleRuntimeVisibility;
    QHash<QString, double> m_titleWidgetValues;
    QHash<QString, int> m_titleWidgetSelection;
    QHash<QString, bool> m_titleWidgetChecked;
    QHash<QString, QString> m_titleWidgetText;
    QSet<QString> m_titleExpandedDropdowns;
    QString m_titleEditingTextId;
    QWidget* m_builtInLoadPanel = nullptr;
    QListWidget* m_builtInLoadList = nullptr;
    QPushButton* m_builtInLoadButton = nullptr;
    struct TitleClipState { QString name; qint64 startedMs = 0; };
    QHash<QString, TitleClipState> m_titleRuntimeClips;
    struct TitleLogicRunner {
        QString elementId;
        QString graphId;
        QString nodeId;
        int waitRemainingMs = -1;
        int safetySteps = 0;
    };
    QVector<TitleLogicRunner> m_titleLogicRunners;
    QVector<TitleLogicRunner> m_titlePendingLogicRunners;
    bool m_titleUpdatingLogic = false;
    QString m_titleScreenStateId;
    QString m_titleUiScreenId = QStringLiteral("title");
    QElapsedTimer m_titleElapsed;
    qint64 m_titleLastLogicTickMs = 0;
    bool m_quitApplicationOnClose = true;
    // RC2.85: o Player nao possui mais uma etapa de Tela de Titulo. Os dados
    // antigos continuam no projeto para compatibilidade, mas a execucao abre
    // diretamente o mapa inicial e usa um fade de entrada.
    bool m_directMapStartup = true;
    bool m_titleRuleApplied = false;
    QSet<QWidget*> m_backendRetryClosing;
};

} // namespace player
