#include "EventEditorModel.h"

#include "core/Editor.h"

#include <QPoint>

namespace ui {
namespace {
QString closingFor(const QString& type)
{
    if (type == QLatin1String("if")) return QStringLiteral("endIf");
    if (type == QLatin1String("loop.begin")) return QStringLiteral("loop.end");
    if (type == QLatin1String("repeat.begin")) return QStringLiteral("repeat.end");
    if (type == QLatin1String("database.each")) return QStringLiteral("database.each.end");
    if (type == QLatin1String("parallel.begin")) return QStringLiteral("parallel.end");
    return {};
}

QString sectionLabel(const core::EventCommand& command)
{
    if (command.type == QLatin1String("if")) return QObject::tr("Condição");
    if (command.type == QLatin1String("loop.begin")) return QObject::tr("Loop");
    if (command.type == QLatin1String("repeat.begin")) return QObject::tr("Repetição");
    if (command.type == QLatin1String("database.each")) return QObject::tr("Para cada registro");
    if (command.type == QLatin1String("parallel.begin")) return QObject::tr("Paralelo");
    return command.type;
}
QString eventDisplayName(const core::MapEvent& event)
{
    return event.name.trimmed().isEmpty() ? QObject::tr("(evento sem nome)") : event.name;
}

QString formattedEventNumber(int number)
{
    // RPG-editor friendly presentation. IDs above 999 remain lossless instead
    // of being truncated (1000, 1001, ...).
    return QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'));
}
} // namespace

QVariantMap CommandGroup::toVariantMap() const
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("color"), color.name(QColor::HexArgb)},
            {QStringLiteral("collapsed"), collapsed}};
}

CommandGroup CommandGroup::fromVariantMap(const QVariantMap& map)
{
    CommandGroup group; group.id = map.value(QStringLiteral("id")).toString();
    group.name = map.value(QStringLiteral("name")).toString();
    const QColor color(map.value(QStringLiteral("color")).toString()); if (color.isValid()) group.color = color;
    group.collapsed = map.value(QStringLiteral("collapsed"), false).toBool(); return group;
}

QVector<CommandStructureInfo> analyzeCommandStructure(const QVector<core::EventCommand>& commands)
{
    QVector<CommandStructureInfo> result(commands.size());
    struct Open { int index; QString close; QString label; };
    QVector<Open> stack;
    for (int i = 0; i < commands.size(); ++i) {
        const QString type = commands.at(i).type;
        const bool boundary = type == QLatin1String("else");
        if (!stack.isEmpty() && (type == stack.last().close || boundary)) {
            result[i].depth = qMax(0, stack.size() - 1); result[i].endsSection = true;
            if (!boundary) { result[i].matchingIndex = stack.last().index; result[stack.last().index].matchingIndex = i; stack.removeLast(); }
        } else result[i].depth = stack.size();
        result[i].commandIndex = i;
        for (const Open& open : stack) result[i].breadcrumb.push_back(open.label);
        const QString close = closingFor(type);
        if (!close.isEmpty()) { result[i].beginsSection = true; stack.push_back({i, close, sectionLabel(commands.at(i))}); }
    }
    return result;
}

bool structurallyValidCommands(const QVector<core::EventCommand>& commands, QString* problem)
{
    QVector<QString> expected;
    for (const core::EventCommand& command : commands) {
        const QString close = closingFor(command.type);
        if (!close.isEmpty()) expected.push_back(close);
        else if (command.type == QLatin1String("else")) {
            if (expected.isEmpty() || expected.last() != QLatin1String("endIf")) { if (problem) *problem = QObject::tr("Senão fora de uma condição."); return false; }
        } else if (command.type == QLatin1String("endIf") || command.type == QLatin1String("loop.end") ||
                   command.type == QLatin1String("repeat.end") || command.type == QLatin1String("database.each.end") ||
                   command.type == QLatin1String("parallel.end")) {
            if (expected.isEmpty() || expected.last() != command.type) { if (problem) *problem = QObject::tr("Fechamento estrutural sem início correspondente: %1").arg(command.type); return false; }
            expected.removeLast();
        }
    }
    if (!expected.isEmpty()) { if (problem) *problem = QObject::tr("Existe uma seção estrutural sem fechamento."); return false; }
    return true;
}

bool structurallyValidMove(const QVector<core::EventCommand>& commands, int first, int last, int insertionIndex, QString* problem)
{
    if (first < 0 || last < first || last >= commands.size() || insertionIndex < 0 || insertionIndex > commands.size()) {
        if (problem) *problem = QObject::tr("Faixa de movimento inválida."); return false;
    }
    if (insertionIndex >= first && insertionIndex <= last + 1) { if (problem) *problem = QObject::tr("O destino está dentro do próprio bloco."); return false; }
    QVector<core::EventCommand> copy = commands, block;
    for (int i = first; i <= last; ++i) block.push_back(copy.at(i));
    for (int i = last; i >= first; --i) copy.removeAt(i);
    if (insertionIndex > last) insertionIndex -= block.size();
    insertionIndex = qBound(0, insertionIndex, copy.size());
    for (int i = 0; i < block.size(); ++i) copy.insert(insertionIndex + i, block.at(i));
    return structurallyValidCommands(copy, problem);
}

MapEventListModel::MapEventListModel(core::Editor& editor, QObject* parent)
    : QAbstractListModel(parent), m_editor(editor)
{
    connect(&m_editor, &core::Editor::mapChanged, this, &MapEventListModel::refresh);
    connect(&m_editor, &core::Editor::docsChanged, this, &MapEventListModel::refresh);
}

int MapEventListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    const core::MapDoc* doc = m_editor.doc();
    return doc ? doc->events.size() : 0;
}

QVariant MapEventListModel::data(const QModelIndex& index, int role) const
{
    const core::MapDoc* doc = m_editor.doc();
    if (!doc || !index.isValid() || index.row() < 0 || index.row() >= doc->events.size())
        return {};

    const core::MapEvent& event = doc->events.at(index.row());
    const int eventNumber = index.row() + 1;
    const QString visibleNumber = formattedEventNumber(eventNumber);
    switch (role) {
    case Qt::DisplayRole:
        // Friendly sequential number for the editor; the stable UUID is never
        // replaced and remains the identity used by navigation and runtime.
        return QObject::tr("%1 — %2").arg(visibleNumber, eventDisplayName(event));
    case Qt::ToolTipRole:
        return QObject::tr("Evento %1\nPosição: (%2, %3)\nID interno: %4")
            .arg(visibleNumber).arg(event.cell.x()).arg(event.cell.y()).arg(event.id);
    case EventIdRole:
    case StableIdRole:
        return event.id;
    case EventNameRole:
        return event.name;
    case EventCellRole:
        return QVariant::fromValue(event.cell);
    case EventNumberRole:
        return eventNumber;
    default:
        return {};
    }
}

QHash<int, QByteArray> MapEventListModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[EventIdRole] = "eventId";
    roles[EventNameRole] = "eventName";
    roles[EventCellRole] = "eventCell";
    roles[StableIdRole] = "stableId";
    roles[EventNumberRole] = "eventNumber";
    return roles;
}

QString MapEventListModel::eventIdAt(int row) const
{
    const QModelIndex index = this->index(row, 0);
    return index.isValid() ? data(index, EventIdRole).toString() : QString();
}

int MapEventListModel::eventNumberAt(int row) const
{
    const QModelIndex index = this->index(row, 0);
    return index.isValid() ? data(index, EventNumberRole).toInt() : 0;
}

int MapEventListModel::rowForEventId(const QString& eventId) const
{
    const core::MapDoc* doc = m_editor.doc();
    if (!doc || eventId.isEmpty()) return -1;
    for (int row = 0; row < doc->events.size(); ++row)
        if (doc->events.at(row).id == eventId) return row;
    return -1;
}

void MapEventListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

void MapEventListModel::refreshEvent(const QString& eventId)
{
    const int row = rowForEventId(eventId);
    if (row < 0) return;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {Qt::DisplayRole, Qt::ToolTipRole,
                                       EventNameRole, EventCellRole, EventNumberRole});
}

} // namespace ui
