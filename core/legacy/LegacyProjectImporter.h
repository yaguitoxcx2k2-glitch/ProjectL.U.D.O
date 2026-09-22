#pragma once

#include <QJsonObject>

namespace core::legacy {

/// Retorna true para projetos produzidos pela antiga LUDO Game Engine.
bool isLegacyEngineProject(const QJsonObject& root);

/// Extrai SOMENTE a parte autoral que o LUDO Map Editor ainda entende.
/// O resultado não instancia Player/Database/Common Events/UI antiga: esses
/// blocos são descartados diretamente no JSON antes do ProjectIO moderno ler.
QJsonObject extractMapAuthoringPayload(const QJsonObject& root, bool* changed = nullptr);

} // namespace core::legacy
