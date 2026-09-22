#pragma once

#include <QByteArray>
#include <QString>

namespace core::serialization {

/// Escrita atômica reutilizável para projeto, saves e futuros documentos.
/// QSaveFile garante commit por rename na mesma filesystem.
bool writeAtomically(const QString& path, const QByteArray& bytes, QString* error = nullptr);

} // namespace core::serialization
