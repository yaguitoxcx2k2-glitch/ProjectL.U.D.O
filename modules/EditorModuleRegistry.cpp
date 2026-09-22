#include "EditorModuleRegistry.h"

#include <QSettings>

namespace modules {
namespace {

QSettings moduleSettings()
{
    // Mantém o mesmo armazenamento das versões anteriores para não perder
    // preferências do usuário durante a migração de Engine -> Map Editor.
    return QSettings(QStringLiteral("LudoEngine"), QStringLiteral("Ludo Engine"));
}

QString settingKey(const EditorModuleDescriptor& descriptor)
{
    return QStringLiteral("editor/modules/%1/enabled").arg(descriptor.key);
}

} // namespace

EditorModuleRegistry& EditorModuleRegistry::instance()
{
    static EditorModuleRegistry registry;
    return registry;
}

EditorModuleRegistry::EditorModuleRegistry()
{
    m_descriptors = {
        {EditorModuleId::MapCore,
         QStringLiteral("map-core"),
         QStringLiteral("Editor de Mapas"),
         QStringLiteral("Canvas, camadas, pintura, seleção, grade, zoom e histórico."),
         true, true},
        {EditorModuleId::Tilesets,
         QStringLiteral("tilesets"),
         QStringLiteral("Tilesets"),
         QStringLiteral("Paleta, autotiles, animações de tiles e gerenciamento de tilesets."),
         true, true},
        {EditorModuleId::ImageTools,
         QStringLiteral("image-tools"),
         QStringLiteral("Ferramentas de Imagem"),
         QStringLiteral("Exportação PNG, imagens de referência e utilitários visuais."),
         false, true},
        {EditorModuleId::Collision,
         QStringLiteral("collision"),
         QStringLiteral("Colisão"),
         QStringLiteral("Marcação e edição das colisões exportadas para o RPG Maker MV/MZ."),
         false, true},
        {EditorModuleId::Priority,
         QStringLiteral("priority"),
         QStringLiteral("Prioridades"),
         QStringLiteral("Prioridades visuais 0–5 usadas pelo LudoMapSystem no RPG Maker MV/MZ."),
         false, true},
        {EditorModuleId::Regions,
         QStringLiteral("regions-rpg-maker"),
         QStringLiteral("Regiões RPG Maker"),
         QStringLiteral("Pintura dos IDs de Região 0–255 e exportação para a camada nativa de regiões do MapXXX."),
         false, true},
        {EditorModuleId::RpgMakerIntegration,
         QStringLiteral("rpg-maker-integration"),
         QStringLiteral("RPG Maker MV/MZ"),
         QStringLiteral("Sincronização bidirecional com RPG Maker MV/MZ, Map IDs automáticos, MapXXX e panorama de referência."),
         true, true}
    };
}

const EditorModuleDescriptor* EditorModuleRegistry::descriptor(EditorModuleId id) const
{
    for (const auto& item : m_descriptors)
        if (item.id == id) return &item;
    return nullptr;
}

bool EditorModuleRegistry::isEnabled(EditorModuleId id) const
{
    const auto* item = descriptor(id);
    if (!item) return false;
    if (item->required) return true;

    QSettings settings = moduleSettings();
    const QString key = settingKey(*item);
    if (settings.contains(key)) return settings.value(key, item->defaultEnabled).toBool();

    // Migração silenciosa das chaves locais usadas quando a integração ainda
    // era nomeada apenas como MZ. O .ludo não é afetado por esta preferência.
    if (id == EditorModuleId::Regions) {
        const QString legacy = QStringLiteral("editor/modules/regions-mz/enabled");
        if (settings.contains(legacy)) {
            const bool enabled = settings.value(legacy, item->defaultEnabled).toBool();
            settings.setValue(key, enabled);
            return enabled;
        }
    }
    return item->defaultEnabled;
}

bool EditorModuleRegistry::setEnabled(EditorModuleId id, bool enabled)
{
    const auto* item = descriptor(id);
    if (!item || item->required) return false;

    QSettings settings = moduleSettings();
    settings.setValue(settingKey(*item), enabled);
    return true;
}

void EditorModuleRegistry::resetDefaults()
{
    QSettings settings = moduleSettings();
    for (const auto& item : m_descriptors) {
        if (!item.required) settings.remove(settingKey(item));
    }
    settings.remove(QStringLiteral("editor/modules/regions-mz/enabled"));
}

} // namespace modules
