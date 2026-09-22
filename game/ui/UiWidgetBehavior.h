#pragma once

#include "core/InputMap.h"

#include <QPointF>
#include <QRectF>
#include <QString>

namespace core { struct UiWidgetSettings; }

namespace game::ui::behavior {

/// Regras puras do comportamento nativo dos Widgets 2D. Não dependem de
/// QWidget, GameSession ou renderer; Run UI e runtime compartilham estas
/// funções para que mouse/teclado/gamepad tenham a mesma semântica.
bool isToggle(const QString& type);
bool isRadio(const QString& type);
bool isSlider(const QString& type);
bool isStepper(const QString& type);
bool isTabs(const QString& type);
bool isDropdown(const QString& type);
bool isTextInput(const QString& type);
bool isScrollable(const QString& type);
bool isGrid(const QString& type);
bool isListLike(const QString& type);

int itemCount(const core::UiWidgetSettings& widget);
int boundedIndex(const core::UiWidgetSettings& widget, int index);
int movedIndex(const core::UiWidgetSettings& widget, int current, core::GameAction action);
double steppedValue(const core::UiWidgetSettings& widget, double current, int direction);
double pointerValue(const core::UiWidgetSettings& widget, const QPointF& point, const QRectF& rect);
int pointerItemIndex(const core::UiWidgetSettings& widget, const QPointF& point, const QRectF& rect);
int pointerGridIndex(const core::UiWidgetSettings& widget, const QPointF& point, const QRectF& rect);
bool directionAdjustsValue(const core::UiWidgetSettings& widget, core::GameAction action);
bool directionAdjustsSelection(const core::UiWidgetSettings& widget, core::GameAction action, bool dropdownExpanded);

} // namespace game::ui::behavior
