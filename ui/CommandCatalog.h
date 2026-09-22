#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace core { class Editor; }

namespace ui {

// Este catálogo descreve apenas a apresentação dos comandos no editor.
// A semântica de execução fica em core::CommandRegistry, evitando que UI e
// runtime compartilhem uma tabela monolítica.

/// Definição visual de um comando no-code no menu do editor. O executor e os
/// parâmetros continuam nos módulos existentes; este catálogo elimina a lista
/// duplicada de strings do construtor do widget de comandos.
struct CommandCatalogEntry {
    QStringList path;
    QString label;
    QString type;
    bool separatorBefore = false;
    QString iconPath;
    QString shortcutKey;
    QStringList tags;
};

QVector<CommandCatalogEntry> builtInCommandCatalog();
/// Catálogo efetivo do editor: built-ins + comandos dos plugins habilitados.
QVector<CommandCatalogEntry> commandCatalogForEditor(const core::Editor& editor);
QStringList commandCatalogTopLevelCategories(const QVector<CommandCatalogEntry>& entries);

/// Gate executável de paridade: compara o catálogo built-in com a política
/// declarada no CommandRegistry. Retorno vazio significa contrato sincronizado.
QStringList commandCatalogContractIssues();

} // namespace ui
