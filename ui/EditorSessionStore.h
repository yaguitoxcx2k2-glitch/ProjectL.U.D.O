#pragma once

#include "core/Editor.h"
#include <QString>

namespace ui {

/// Persistência LOCAL da sessão de autoria por projeto. Não toca no .ludo:
/// abas abertas, mapa ativo e viewports pertencem ao computador/editor.
class EditorSessionStore {
public:
    static QString projectKey(const core::Editor& editor);
    static void save(const core::Editor& editor);
    static bool restore(core::Editor& editor);
};

} // namespace ui
