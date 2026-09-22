#pragma once

#include "core/Editor.h"
#include "core/EventModel.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ui {

bool isScreenToneCommand(const QString& type);
bool editScreenToneCommand(core::Editor& editor, core::EventCommand& command,
                           QWidget* parent = nullptr);

} // namespace ui
