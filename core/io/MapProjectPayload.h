#pragma once

#include <QJsonObject>

namespace core::io::mapproject {

// Mantém somente dados de autoria que pertencem ao LUDO Map Editor.
// Gameplay, eventos e configurações da antiga LUDO Engine ficam sob autoridade
// do RPG Maker MV/MZ e nunca são persistidos em ProjectFormat 3.
QJsonObject sanitize(QJsonObject root, bool* changed = nullptr);

} // namespace core::io::mapproject
