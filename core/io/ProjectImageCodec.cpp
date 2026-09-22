// ============================================================================
// ProjectImageCodec.cpp — conversão imagem <-> data URI usada pelo documento.
// ============================================================================
#include "core/ProjectIO.h"

#include <QBuffer>
#include <QImage>

namespace core { namespace io {

QString imageToDataUri(const QImage& img)
{
    if (img.isNull()) return QString();
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QImage dataUriToImage(const QString& uri)
{
    if (uri.isEmpty()) return QImage();
    const int comma = uri.indexOf(QLatin1Char(','));
    if (uri.startsWith(QLatin1String("data:")) && comma > 0) {
        const QByteArray raw = QByteArray::fromBase64(uri.mid(comma + 1).toLatin1());
        QImage img;
        img.loadFromData(raw, "PNG");
        return img;
    }
    QImage img;                       // caminho de arquivo (projetos externos)
    img.load(uri);
    return img;
}


}} // namespace core::io
