#pragma once

#include <QJsonObject>

namespace core {
class Editor;
}

namespace core::serialization {

/// Serializa apenas o envelope/identidade do projeto. Mapas, banco, UI e
/// assets permanecem em serializers separados ou no adaptador legado durante
/// a migração incremental do ProjectIO.
QJsonObject writeProjectHeader(const Editor& editor);
void applyProjectIdentity(Editor& editor, const QJsonObject& root);

} // namespace core::serialization
