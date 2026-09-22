// ============================================================================
// AudioPicker.h — facade compartilhada para o Universal Asset Picker.
//
// RC2.63: não existe mais uma segunda biblioteca de áudio. BGM/BGS/ME/SE/Voice
// entram pelo mesmo picker contextual, com preview e volume.
// ============================================================================
#pragma once

#include "core/Editor.h"

#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ui {

/// Abre diretamente a categoria de áudio adequada no Universal Asset Picker.
/// `context` aceita BGM/BGS/ME/SE/Voice ou Audio (agregado).
/// `source` entra/sai relativo ao projeto; cancelar não altera nada.
bool chooseGameAudio(core::Editor& ed, QWidget* parent, const QString& title,
                     QString& source, int& volume,
                     const QString& context = QStringLiteral("Audio"));

} // namespace ui
