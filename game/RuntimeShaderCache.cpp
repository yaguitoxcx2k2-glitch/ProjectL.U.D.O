#include "RuntimeShaderCache.h"

#include <QFile>
#include <QHash>
#include <rhi/qshader.h>

namespace game {
namespace {
QHash<QString, QShader>& cache()
{
    static QHash<QString, QShader> value;
    return value;
}
}

QShader runtimeShader(const QString& resourcePath)
{
    auto& c = cache();
    const auto it = c.constFind(resourcePath);
    if (it != c.cend()) return it.value();
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QShader shader = QShader::fromSerialized(file.readAll());
    if (shader.isValid()) c.insert(resourcePath, shader);
    return shader;
}

bool warmRuntimeShaders(QStringList* errors)
{
    if (errors) errors->clear();
    bool ok = true;
    for (const QString& path : {QStringLiteral(":/shaders/sprite.vert.qsb"),
                                QStringLiteral(":/shaders/sprite.frag.qsb"),
                                QStringLiteral(":/shaders/filter.frag.qsb")}) {
        if (!runtimeShader(path).isValid()) {
            ok = false;
            if (errors) errors->push_back(path);
        }
    }
    return ok;
}

void clearRuntimeShaderCache()
{
    cache().clear();
}

} // namespace game
