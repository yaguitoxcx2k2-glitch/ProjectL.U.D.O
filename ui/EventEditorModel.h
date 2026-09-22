#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QColor>
#include <QStringList>
#include <QVector>

#include "core/EventModel.h"

namespace core { class Editor; }

namespace ui {

struct CommandGroup {
    QString id;
    QString name;
    QColor color = QColor(QStringLiteral("#6aa9ff"));
    bool collapsed = false;

    QVariantMap toVariantMap() const;
    static CommandGroup fromVariantMap(const QVariantMap& map);
};

struct CommandStructureInfo {
    int commandIndex = -1;
    int depth = 0;
    int matchingIndex = -1;
    bool beginsSection = false;
    bool endsSection = false;
    QStringList breadcrumb;
};

QVector<CommandStructureInfo> analyzeCommandStructure(const QVector<core::EventCommand>& commands);
bool structurallyValidCommands(const QVector<core::EventCommand>& commands, QString* problem = nullptr);
bool structurallyValidMove(const QVector<core::EventCommand>& commands, int first, int last, int insertionIndex,
                           QString* problem = nullptr);

/// Model/View bridge for the Map Event navigator used by the Event Editor.
///
/// Important contract: the model never owns or mirrors a QVector<MapEvent>.
/// Every row is resolved from Editor::doc()->events on demand, so creation,
/// deletion, rename and document switching keep a single source of truth.
///
/// User-facing event numbers (001, 002, ...) are presentation-only and map to
/// document order. Internal navigation always continues to use MapEvent::id.
class MapEventListModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        EventIdRole = Qt::UserRole + 1,
        EventNameRole,
        EventCellRole,
        StableIdRole,
        EventNumberRole
    };

    explicit MapEventListModel(core::Editor& editor, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString eventIdAt(int row) const;
    int eventNumberAt(int row) const;
    int rowForEventId(const QString& eventId) const;

public slots:
    void refresh();
    void refreshEvent(const QString& eventId);

private:
    core::Editor& m_editor;
};

} // namespace ui
