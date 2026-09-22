#pragma once

#include "core/EventModel.h"

#include <QVector>

namespace ui {

/// Clipboard estruturado compartilhado por todas as janelas de comandos.
/// Usa MIME próprio no clipboard do sistema, portanto Map Event e Common Event
/// copiam/colam exatamente o mesmo EventCommand/params, inclusive branches.
void writeEventCommandClipboard(const QVector<core::EventCommand>& commands);
QVector<core::EventCommand> readEventCommandClipboard();
bool eventCommandClipboardHasCommands();
QString eventCommandClipboardMimeType();

} // namespace ui
