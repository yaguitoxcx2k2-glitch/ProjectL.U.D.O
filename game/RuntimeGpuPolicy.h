// ============================================================================
// RuntimeGpuPolicy.h — política única do runtime GPU-first da LUDO.
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>

namespace game {

inline constexpr int RuntimeGpuContractVersion = 1;

/// Normaliza IDs públicos. "auto" escolhe o backend recomendado da plataforma.
QString normalizeRuntimeGpuBackend(const QString& backend);
QString runtimeGpuBackendDisplayName(const QString& backend);

/// Ordem de tentativas sem CPU. No Windows: D3D11 e Vulkan; nas demais
/// plataformas, o backend nativo vem antes do fallback suportado pelo QRhi.
QStringList runtimeGpuBackendFallbackChain(const QString& requested = QString());

/// Preferência do jogador/editor, já migrada de configurações CPU antigas.
QString configuredRuntimeGpuBackend();
void migrateLegacyRuntimeRendererSettings();

} // namespace game
