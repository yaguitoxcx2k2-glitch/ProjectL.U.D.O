#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QPointF>

namespace core { class Editor; }
namespace game {
struct RuntimeScenarioInput { int frame=0; QString action; bool pressed=true; };
struct RuntimeScenarioExpectation { QString mapId; QHash<int,int> variables; QHash<int,bool> switches; QString stateHash; };
struct RuntimeScenarioDefinition {
    int frames=1;
    quint64 seed=0x4c55444fULL;
    QPointF startPixel;
    QVector<RuntimeScenarioInput> inputs;
    RuntimeScenarioExpectation expected;
    static bool parse(const QJsonObject&, RuntimeScenarioDefinition*, QString* error=nullptr);
};
class RuntimeScenarioRunner {
public:
    static bool run(core::Editor& source, const RuntimeScenarioDefinition&, QString* error=nullptr, QString* finalHash=nullptr);
};
}
