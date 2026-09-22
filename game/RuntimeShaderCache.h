// ============================================================================
// RuntimeShaderCache.h — cache compartilhado de pacotes QShader.
// RC2.53.1 mantém QShader como detalhe da fronteira QRhi/GuiPrivate;
// a criação de pipelines continua no contexto GPU real, onde ela pertence.
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>

class QShader;

namespace game {

QShader runtimeShader(const QString& resourcePath);
bool warmRuntimeShaders(QStringList* errors = nullptr);
void clearRuntimeShaderCache();

} // namespace game
