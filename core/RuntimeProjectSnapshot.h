// ============================================================================
// RuntimeProjectSnapshot.h — copia em memoria do projeto para F5/F6.
//
// Antes o playtest passava por JSON -> arquivo temporario -> loadProject(),
// incluindo PNG/base64. Aqui o modelo e clonado diretamente; QImage/QVector
// continuam usando copy-on-write do Qt e as arvores de Layer sao independentes.
// ============================================================================
#pragma once

#include <memory>
#include <QString>
#include <QStringList>

namespace core {
class Editor;

std::unique_ptr<Editor> makeRuntimeEditorSnapshot(const Editor& source,
                                                  QString* error = nullptr);

/// Reaplica no Editor runtime exatamente o mesmo modelo executável usado por
/// makeRuntimeEditorSnapshot/F5/F6. O QObject e seus consumidores permanecem
/// vivos; somente o modelo de projeto é substituído.
bool refreshRuntimeEditorSnapshot(Editor& runtime, const Editor& source,
                                  QString* error = nullptr);

/// Compara somente o payload executável. Lista vazia significa que o snapshot
/// usado por F5/F6 é equivalente ao game.ludo que o exportador produz.
QStringList runtimeProjectParityIssues(const Editor& source, const Editor& runtime);

} // namespace core
