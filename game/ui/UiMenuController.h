#pragma once

#include "core/InputMap.h"

#include <QString>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QPointF>
#include <QSize>
#include <QStringList>

namespace core { class Editor; struct UiScreenStateSettings; struct UiScreenStateElementSettings; struct UiEventActionSettings; struct UiVisualLogicGraphSettings; struct UiWidgetSettings; }
namespace game { class GameSession; class GameState; }

namespace game::ui {

enum class UiMenuScreen {
    Closed,
    Main,
    Status,
    Inventory,
    InventoryTarget,
    EquipmentActor,
    EquipmentSlot,
    EquipmentItem,
    SkillsActor,
    Skills,
    SkillTarget,
    Quests,
    DialogueLog,
    Save,
    Load,
    SaveOverwrite,
    Settings
};

struct UiMenuEntry {
    QString label;
    QString value;
    bool enabled = true;
};

/// Controlador do menu grande do jogo. Não possui QWidget, não cria event loop
/// e não conhece CPU/GPU. Ele mantém foco/navegação e executa as operações do
/// RPG; GameUiLayer converte o estado em UiDrawList para ambos os renderizadores.
class UiMenuController {
public:
    explicit UiMenuController(GameSession& session);

    void open(bool standalone);
    bool openBuiltIn(const QString& screenId, bool standalone = true);
    void close();
    /// Abre uma tela livre criada no UI Designer. Diferente do menu RPG
    /// legado, esta tela não possui lista/estado de menu: todo o input e as
    /// ações vêm dos próprios Widgets No-Code da tela.
    void openCustom(const QString& screenId);
    void closeCustom();
    /// Fecha imediatamente qualquer menu/tela efêmera ao trocar a linha do tempo da partida.
    void resetForGameStateTransition();
    void update(double dt);
    bool handleAction(core::GameAction action);
    bool handleCustomAction(core::GameAction action);
    void selectIndex(int index);
    void activateSelected();
    /// Aciona o widget 2D interativo mais alto sob o ponteiro e transfere o
    /// foco para ele. Teclado e gamepad usam o mesmo GameAction/InputMap.
    bool activateWidgetAt(const QPointF& logicalPosition, const QSize& viewport);
    /// Scroll nativo para Scroll Area/listas/grids/dropdown sob o ponteiro.
    bool wheelWidgetAt(const QPointF& logicalPosition, const QSize& viewport, int steps);
    /// Arraste contínuo de Slider iniciado por activateWidgetAt().
    bool dragWidgetAt(const QPointF& logicalPosition, const QSize& viewport);
    void releasePointerWidget();
    /// Reconstrói textos/listas já materializados quando o idioma do jogador muda.
    void refreshLocalization();
    /// Entrada textual nativa do Text Input. Retorna true quando a tecla foi
    /// consumida pelo widget em edição.
    bool handleTextKey(int key, const QString& text);

    bool active() const { return m_screen != UiMenuScreen::Closed; }
    bool customActive() const { return !m_customScreenId.isEmpty(); }
    const QString& customScreenId() const { return m_customScreenId; }
    bool closing() const { return m_closing; }
    UiMenuScreen screen() const { return m_screen; }
    int selected() const { return m_selected; }
    int firstVisible(int maxRows = 9) const;
    const QVector<UiMenuEntry>& entries() const { return m_entries; }
    const QString& title() const { return m_title; }
    const QString& detail() const { return m_detail; }
    const QString& notice() const { return m_notice; }
    QString summary() const;
    QString footerHint() const;
    double transitionProgress() const;

    // Estado visual temporário produzido pelos eventos No Code do UI Designer.
    QString runtimeVisualState(const QString& elementId) const;
    QString runtimeClipName(const QString& elementId) const;
    int runtimeClipTimeMs(const QString& elementId) const;
    bool runtimeVisibility(const QString& elementId, bool fallback) const;
    bool dataBindingEnabled(const QString& elementId, bool fallback = true) const;
    /// Aplica o estado efêmero do comportamento nativo sobre a definição do
    /// projeto antes do renderer desenhar o Widget. O projeto nunca é mutado.
    void applyRuntimeWidget(const QString& elementId, core::UiWidgetSettings& widget) const;
    QString focusedElementId() const { return m_focusedElementId; }
    QString activeScreenStateId() const { return m_activeScreenStateId; }
    qint64 uiElapsedMs() const { return m_uiElapsedMs; }
    const core::Editor& editor() const;
    const GameState& state() const;

    bool takeReturnToTitleRequest();
    bool takeFullscreenToggleRequest();
    bool takeAudioSettingsChangedRequest();

private:
    void setScreen(UiMenuScreen screen, bool rememberParent = true);
    void finishClose();
    void back();
    void move(int delta);
    void adjust(int delta);
    void rebuild();
    void rebuildMain();
    void rebuildStatus();
    void rebuildInventory();
    void rebuildInventoryTargets();
    void rebuildEquipmentActors();
    void rebuildEquipmentSlots();
    void rebuildEquipmentItems();
    void rebuildSkillsActors();
    void rebuildSkills();
    void rebuildSkillTargets();
    void rebuildQuests();
    void rebuildDialogueLog();
    void rebuildSaves(bool loadMode);
    void rebuildOverwriteConfirm();
    void rebuildSettings();
    void refreshDetail();
    void executeUiEvent(const QString& elementId, const QString& trigger);
    void executeUiAction(const QString& elementId, const QString& trigger, const core::UiEventActionSettings& action);
    void startVisualLogic(const QString& elementId, const QString& trigger);
    void updateVisualLogic(int deltaMs);
    void updateRuntimeAnimations(double dt);
    QString currentUiScreenId() const;
    void resetRuntimeUiState();
    QStringList focusableWidgetIds() const;
    bool setFocusedElement(const QString& id);
    bool moveWidgetFocus(core::GameAction action);
    bool activateFocusedWidget();
    bool handleFocusedNativeDirection(core::GameAction action);
    bool applyNativeActivation(const QString& id, const QPointF* point = nullptr, const QSize* viewport = nullptr);
    void setRuntimeSelectedIndex(const QString& id, int index, bool fireEvent = true);
    void setRuntimeValue(const QString& id, double value, bool fireEvent = true);
    void applyTabScreenState(const QString& id, int index);
    double runtimeValue(const QString& id) const;
    int runtimeSelectedIndex(const QString& id) const;
    bool runtimeChecked(const QString& id) const;
    QString runtimeText(const QString& id) const;
    QString initialFocusWidgetId() const;
    const core::UiScreenStateSettings* activeScreenState() const;
    const core::UiScreenStateElementSettings* screenStateOverride(const QString& elementId) const;
    bool setActiveScreenState(const QString& stateId, bool playAnimations = true);
    QString initialScreenStateId() const;

    void useInventoryItem(const QString& itemId, const QString& targetActorId = QString());
    void useSkill(const QString& actorId, const QString& skillId,
                  const QString& targetActorId = QString());
    void applyEquipment(const QString& itemId);
    void performSave(int slot);
    void performLoad(int slot);

    GameSession& m_session;
    UiMenuScreen m_screen = UiMenuScreen::Closed;
    UiMenuScreen m_parent = UiMenuScreen::Main;
    QVector<UiMenuEntry> m_entries;
    QString m_title;
    QString m_detail;
    QString m_notice;
    int m_selected = 0;
    bool m_standalone = false;
    bool m_dialogueUnreadOnly = false;

    QString m_pendingItemId;
    QString m_pendingActorId;
    QString m_pendingSkillId;
    QString m_pendingEquipSlot;
    int m_pendingSaveSlot = 1;

    bool m_returnToTitleRequested = false;
    bool m_fullscreenToggleRequested = false;
    bool m_audioSettingsChanged = false;
    bool m_closing = false;
    double m_transitionElapsed = 0.0;
    double m_transitionDuration = 0.12;
    qint64 m_uiElapsedMs = 0;

    struct RuntimeClipState { QString name; int timeMs = 0; };
    struct LogicRunner {
        QString elementId;
        QString graphId;
        QString nodeId;
        int waitRemainingMs = -1; // -1 = Delay ainda não armado
        int safetySteps = 0;
    };
    QVector<LogicRunner> m_logicRunners;
    QVector<LogicRunner> m_pendingLogicRunners;
    bool m_updatingLogic = false;
    QHash<QString, QString> m_runtimeStates;
    QHash<QString, RuntimeClipState> m_runtimeClips;
    QHash<QString, bool> m_runtimeVisibility;
    QString m_focusedElementId;
    QString m_activeScreenStateId;
    QString m_customScreenId;

    // Bloco C — estado nativo dos controles. É deliberadamente efêmero: os
    // defaults continuam no projeto e Data Bindings/Eventos continuam sendo a
    // forma No-Code de persistir o resultado em Switches/Variáveis.
    QHash<QString, double> m_runtimeWidgetValues;
    QHash<QString, int> m_runtimeWidgetSelection;
    QHash<QString, bool> m_runtimeWidgetChecked;
    QHash<QString, QString> m_runtimeWidgetText;
    QSet<QString> m_expandedDropdowns;
    QString m_editingTextElementId;
    QString m_pointerDragElementId;
};

} // namespace game::ui
