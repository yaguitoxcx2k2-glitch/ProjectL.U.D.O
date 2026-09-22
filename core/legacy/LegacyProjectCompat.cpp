#include "LegacyProjectCompat.h"

namespace core {

void LegacyProjectCompatState::resetLegacyProjectState()
{
    *this = LegacyProjectCompatState{};
}

QString LegacyProjectCompatState::switchName(int id) const
{
    for (const SwitchDef& s : switches)
        if (s.id == id) return s.name;
    return QString();
}

QString LegacyProjectCompatState::variableName(int id) const
{
    for (const VariableDef& v : variables)
        if (v.id == id) return v.name;
    return QString();
}

QString LegacyProjectCompatState::stringName(int id) const
{
    for (const StringDef& v : strings)
        if (v.id == id) return v.name;
    return QString();
}

bool LegacyProjectCompatState::switchInitial(int id) const
{
    for (const SwitchDef& s : switches)
        if (s.id == id) return s.initial;
    return false;
}

int LegacyProjectCompatState::variableInitial(int id) const
{
    for (const VariableDef& v : variables)
        if (v.id == id) return v.initial;
    return 0;
}

QString LegacyProjectCompatState::stringInitial(int id) const
{
    for (const StringDef& v : strings)
        if (v.id == id) return v.initial;
    return QString();
}

const CommonEvent* LegacyProjectCompatState::commonEventByNumber(int numero) const
{
    for (const CommonEvent& c : commonEvents)
        if (c.number == numero) return &c;
    return nullptr;
}

const CommonEvent* LegacyProjectCompatState::commonEventById(const QString& id) const
{
    if (id.isEmpty()) return nullptr;
    for (const CommonEvent& c : commonEvents)
        if (c.id == id) return &c;
    return nullptr;
}

CommonEvent* LegacyProjectCompatState::commonEventById(const QString& id)
{
    if (id.isEmpty()) return nullptr;
    for (CommonEvent& c : commonEvents)
        if (c.id == id) return &c;
    return nullptr;
}
const FootstepSurface* LegacyProjectCompatState::footstepSurfaceById(const QString& id) const
{
    if (id.trimmed().isEmpty()) return nullptr;
    for (const FootstepSurface& surface : footstepSurfaces)
        if (surface.id == id) return &surface;
    return nullptr;
}

FootstepSurface* LegacyProjectCompatState::footstepSurfaceById(const QString& id)
{
    if (id.trimmed().isEmpty()) return nullptr;
    for (FootstepSurface& surface : footstepSurfaces)
        if (surface.id == id) return &surface;
    return nullptr;
}

const CustomDatabaseDefinition* LegacyProjectCompatState::customDatabase(const QString& id) const
{
    return customDatabaseById(customDatabases, id);
}

CustomDatabaseDefinition* LegacyProjectCompatState::customDatabase(const QString& id)
{
    return customDatabaseById(customDatabases, id);
}


const PictureAsset* LegacyProjectCompatState::pictureById(const QString& id) const
{
    if (id.isEmpty()) return nullptr;
    for (const PictureAsset& a : pictures)
        if (a.id == id) return &a;
    return nullptr;
}

const PictureAsset* LegacyProjectCompatState::pictureByName(const QString& nome) const
{
    if (nome.isEmpty()) return nullptr;
    for (const PictureAsset& a : pictures)
        if (a.name.compare(nome, Qt::CaseInsensitive) == 0) return &a;
    return nullptr;
}

const PictureAsset* LegacyProjectCompatState::pictureFor(const QString& id, const QString& nome) const
{
    if (const PictureAsset* a = pictureById(id)) return a;
    return pictureByName(nome);
}

int LegacyProjectCompatState::pictureIndexById(const QString& id) const
{
    for (int i = 0; i < pictures.size(); ++i)
        if (pictures[i].id == id) return i;
    return -1;
}

} // namespace core
