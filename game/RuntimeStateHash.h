#pragma once
#include <QByteArray>
#include <QString>
#include <QPointF>
namespace game { class GameState; class World; }
namespace game {
class RuntimeStateHash {
public:
    static QByteArray calculate(const GameState& state, const QString& mapId,
                                const QPointF& playerPosition, quint64 rngState);
};
}
