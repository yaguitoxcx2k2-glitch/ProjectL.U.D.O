#include "AtomicProjectFile.h"

#include <QObject>
#include <QSaveFile>

namespace core::serialization {

bool writeAtomically(const QString& path, const QByteArray& bytes, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QObject::tr("Não foi possível escrever em %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QObject::tr("Não foi possível concluir a gravação de %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace core::serialization
