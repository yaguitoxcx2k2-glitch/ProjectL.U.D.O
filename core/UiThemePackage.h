#pragma once

#include "core/Editor.h"

#include <QString>

namespace core::io {

/// Extensão portátil dos temas da Interface In-Game 2.0.
QString uiThemePackageExtension();

/// Salva APENAS aparência/estilos/comportamento visual. Layouts, Widgets,
/// Components e Screen States não fazem parte de um tema e nunca são apagados.
bool saveUiThemePackage(const GameUiSettings& settings, const QString& path, QString* error = nullptr);

/// Carrega um tema sobre `base`, preservando tudo que pertence a layout/telas.
bool loadUiThemePackage(const GameUiSettings& base, const QString& path,
                        GameUiSettings* out, QString* error = nullptr);

} // namespace core::io
