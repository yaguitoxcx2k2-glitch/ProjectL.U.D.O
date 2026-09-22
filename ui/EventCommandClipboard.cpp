#include "EventCommandClipboard.h"

#include "core/EventCommandCodec.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace ui {
namespace {
constexpr auto kMime = "application/x-ludo-event-commands+json";
constexpr int kClipboardVersion = 1;
}

QString eventCommandClipboardMimeType()
{
    return QString::fromLatin1(kMime);
}

void writeEventCommandClipboard(const QVector<core::EventCommand>& commands)
{
    if (!QApplication::clipboard()) return;
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("LUDO_EVENT_COMMANDS");
    root[QStringLiteral("version")] = kClipboardVersion;
    root[QStringLiteral("commands")] = core::eventCommandsToJson(commands);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);

    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMime), bytes);
    mime->setText(QObject::tr("Comandos de evento LUDO: %1").arg(commands.size()));
    QApplication::clipboard()->setMimeData(mime);
}

QVector<core::EventCommand> readEventCommandClipboard()
{
    if (!QApplication::clipboard()) return {};
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QString::fromLatin1(kMime))) return {};
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(mime->data(QString::fromLatin1(kMime)), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("LUDO_EVENT_COMMANDS")) return {};
    if (root.value(QStringLiteral("version")).toInt() != kClipboardVersion) return {};
    return core::eventCommandsFromJson(root.value(QStringLiteral("commands")).toArray(), 100000);
}

bool eventCommandClipboardHasCommands()
{
    if (!QApplication::clipboard()) return false;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(QString::fromLatin1(kMime));
}

} // namespace ui
