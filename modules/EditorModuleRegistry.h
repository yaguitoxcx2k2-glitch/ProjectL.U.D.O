#pragma once

#include <QString>
#include <QVector>

namespace modules {

enum class EditorModuleId
{
    MapCore,
    Tilesets,
    ImageTools,
    Collision,
    Priority,
    Regions,
    RpgMakerIntegration
};

struct EditorModuleDescriptor
{
    EditorModuleId id;
    QString key;
    QString name;
    QString description;
    bool required = false;
    bool defaultEnabled = true;
};

/// Registro central dos módulos do LUDO Map Editor.
///
/// Os módulos são internos ao executável nesta série 0.5, mas a UI já é
/// organizada por capacidades independentes. Módulos opcionais podem ser
/// ligados/desligados sem tocar no projeto .ludo; a preferência é local.
class EditorModuleRegistry final
{
public:
    static EditorModuleRegistry& instance();

    const QVector<EditorModuleDescriptor>& descriptors() const { return m_descriptors; }
    bool isEnabled(EditorModuleId id) const;
    bool setEnabled(EditorModuleId id, bool enabled);
    void resetDefaults();

private:
    EditorModuleRegistry();
    const EditorModuleDescriptor* descriptor(EditorModuleId id) const;

    QVector<EditorModuleDescriptor> m_descriptors;
};

} // namespace modules
