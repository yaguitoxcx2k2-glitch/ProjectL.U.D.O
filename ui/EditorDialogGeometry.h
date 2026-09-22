// ============================================================================
// EditorDialogGeometry.h — geometria persistente e segura para dialogs do Editor.
//
// RC2.52: janelas grandes não podem reaparecer atrás da barra de tarefas nem
// fora de monitores que deixaram de existir. A regra usa availableGeometry e
// guarda apenas QRect normalizado em QSettings (estado de UI, nunca projeto).
// ============================================================================
#pragma once

#include <QRect>
#include <QSize>
#include <QString>

class QDialog;

namespace ui {

QRect fitEditorDialogRect(const QRect& available, const QRect& requested,
                          const QSize& minimum = QSize(640, 480));

void restoreEditorDialogGeometry(QDialog& dialog, const QString& settingsKey,
                                 const QSize& preferred, const QSize& minimum);
void saveEditorDialogGeometry(const QDialog& dialog, const QString& settingsKey);

} // namespace ui
