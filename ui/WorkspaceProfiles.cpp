#include "WorkspaceProfiles.h"

#include <QSettings>
#include <QUuid>
#include <QVariantList>
#include <QObject>

namespace ui {
namespace {

constexpr auto kProfilesKey = "workspace/profiles";
constexpr auto kCurrentKey = "workspace/currentProfile";
constexpr auto kLastStateKey = "workspace/lastSessionState";

QSettings settings()
{
    return QSettings(QStringLiteral("LudoEngine"), QStringLiteral("Ludo Engine"));
}

QVector<WorkspaceProfileDescriptor> builtIns()
{
    return {
        {QStringLiteral("builtin/default"), QObject::tr("Padrão"),
         QObject::tr("Mapas e tilesets à esquerda; camadas, propriedades e minimapa à direita."), true},
        {QStringLiteral("builtin/mapping"), QObject::tr("Mapeamento"),
         QObject::tr("Prioriza tilesets e canvas, mantendo as ferramentas de mapa sempre visíveis."), true},
        {QStringLiteral("builtin/focus"), QObject::tr("Foco no mapa"),
         QObject::tr("Oculta os docks laterais para maximizar a área do mapa."), true}
    };
}

QVariantList profileMaps()
{
    return settings().value(QString::fromLatin1(kProfilesKey)).toList();
}

void saveProfileMaps(const QVariantList& list)
{
    settings().setValue(QString::fromLatin1(kProfilesKey), list);
}

QString normalizedCustomName(const QString& name)
{
    return name.simplified();
}

} // namespace

QVector<WorkspaceProfileDescriptor> WorkspaceProfiles::catalog()
{
    QVector<WorkspaceProfileDescriptor> out = builtIns();
    for (const QVariant& v : profileMaps()) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        const QString name = m.value(QStringLiteral("name")).toString();
        if (id.isEmpty() || name.isEmpty()) continue;
        out.push_back({id, name, QObject::tr("Perfil personalizado salvo neste computador."), false});
    }
    return out;
}

QString WorkspaceProfiles::currentProfileId()
{
    const QString id = settings().value(QString::fromLatin1(kCurrentKey),
                                        QStringLiteral("builtin/default")).toString();
    for (const auto& profile : catalog())
        if (profile.id == id) return id;
    return QStringLiteral("builtin/default");
}

void WorkspaceProfiles::setCurrentProfileId(const QString& id)
{
    settings().setValue(QString::fromLatin1(kCurrentKey), id);
}

QByteArray WorkspaceProfiles::lastSessionState()
{
    return settings().value(QString::fromLatin1(kLastStateKey)).toByteArray();
}

void WorkspaceProfiles::setLastSessionState(const QByteArray& state)
{
    settings().setValue(QString::fromLatin1(kLastStateKey), state);
}

QByteArray WorkspaceProfiles::customProfileState(const QString& id)
{
    for (const QVariant& v : profileMaps()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m.value(QStringLiteral("state")).toByteArray();
    }
    return {};
}

QString WorkspaceProfiles::saveCustomProfile(const QString& name, const QByteArray& state,
                                               QString* error)
{
    if (error) error->clear();
    const QString cleanName = normalizedCustomName(name);
    if (cleanName.isEmpty()) {
        if (error) *error = QObject::tr("Informe um nome para o espaço de trabalho.");
        return {};
    }
    if (state.isEmpty()) {
        if (error) *error = QObject::tr("O layout atual não pôde ser capturado.");
        return {};
    }

    QVariantList list = profileMaps();
    // Salvar novamente com o mesmo nome atualiza o perfil em vez de duplicá-lo.
    for (QVariant& v : list) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString().compare(cleanName, Qt::CaseInsensitive) != 0)
            continue;
        m.insert(QStringLiteral("state"), state);
        v = m;
        saveProfileMaps(list);
        return m.value(QStringLiteral("id")).toString();
    }

    QVariantMap m;
    const QString id = QStringLiteral("custom/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("name"), cleanName);
    m.insert(QStringLiteral("state"), state);
    list.push_back(m);
    saveProfileMaps(list);
    return id;
}

bool WorkspaceProfiles::removeCustomProfile(const QString& id, QString* error)
{
    if (error) error->clear();
    if (isBuiltIn(id)) {
        if (error) *error = QObject::tr("Perfis internos não podem ser removidos.");
        return false;
    }
    QVariantList list = profileMaps();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("id")).toString() != id) continue;
        list.removeAt(i);
        saveProfileMaps(list);
        if (currentProfileId() == id) setCurrentProfileId(QStringLiteral("builtin/default"));
        return true;
    }
    if (error) *error = QObject::tr("Espaço de trabalho não encontrado.");
    return false;
}

bool WorkspaceProfiles::isBuiltIn(const QString& id)
{
    return id.startsWith(QStringLiteral("builtin/"));
}

} // namespace ui
