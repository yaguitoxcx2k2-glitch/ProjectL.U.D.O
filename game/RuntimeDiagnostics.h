#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

#include <functional>

namespace game {

struct DebugCommandLocation {
    QString source;
    QString mapId;
    QString eventId;
    QString commandType;
    int commandIndex = -1;
    int callDepth = 0;
};

enum class DebugWatchKind { Variable, Switch, String, GameValue };

QString debugWatchKindId(DebugWatchKind kind);
QString debugWatchKindLabel(DebugWatchKind kind);
DebugWatchKind debugWatchKindFromId(const QString& id);

struct DebugWatch {
    quint64 serial = 0;
    DebugWatchKind kind = DebugWatchKind::Variable;
    QString reference;
    QString label;
    bool enabled = true;
    bool breakOnChange = false;
    QString breakOperator;
    QVariant breakValue;
};

struct DebugWatchSample {
    DebugWatch watch;
    QVariant value;
    QVariant previousValue;
    bool initialized = false;
    bool changed = false;
    bool conditionMatched = false;
};

/// Watches pertencem ao playtest, não ao projeto/save. O resolver é fornecido
/// pela GameSession para que Variable/Switch/String/Game Value consumam o
/// mesmo estado e o mesmo Value Resolver usados pelos Event Commands.
class DebugWatchStore {
public:
    using Resolver = std::function<QVariant(const DebugWatch&)>;

    quint64 add(DebugWatchKind kind, QString reference, QString label = {});
    bool remove(quint64 serial);
    bool setBreakOnChange(quint64 serial, bool enabled);
    bool setCondition(quint64 serial, QString op, QVariant value);
    bool setEnabled(quint64 serial, bool enabled);
    const QVector<DebugWatch>& watches() const { return m_watches; }
    QVector<DebugWatchSample> samples() const;
    void update(const Resolver& resolver);
    bool consumeBreakRequest();

private:
    int indexOf(quint64 serial) const;
    QVector<DebugWatch> m_watches;
    QHash<quint64, DebugWatchSample> m_samples;
    QHash<quint64, bool> m_previousCondition;
    quint64 m_nextSerial = 1;
    bool m_breakRequested = false;
};

struct EventRuntimeDebugState {
    DebugCommandLocation current;
    int cutsceneDepth = 0;
    QString cutsceneSource;
    QString awaitableKind = QStringLiteral("none");
    QVariantMap awaitableDetails;
    QString cancellationReason = QStringLiteral("none");
    QVector<quint64> parallelTickets;
    QString targetResolution;
    QString conditionEvaluation;
};

/// Gate compartilhado por todos os Interpreters da sessão. O modo Step libera
/// exatamente UM comando; a próxima fronteira volta a bloquear a execução.
class EventDebugController {
public:
    using BreakpointEvaluator = std::function<bool(const QVariantMap&)>;
    bool beforeCommand(const DebugCommandLocation& location,
                       const BreakpointEvaluator& evaluator = {});
    void pause();
    void continueRun();
    void step(); // compatibilidade: Step Into
    void stepInto();
    void stepOver();
    void stepOut();
    bool paused() const { return m_paused; }
    bool blocked() const { return m_blocked; }
    bool stepPending() const { return m_stepMode != StepMode::None || m_stepBudget > 0; }
    const DebugCommandLocation& current() const { return m_current; }
    void addBreakpoint(const QString& source, int commandIndex);
    void removeBreakpoint(const QString& source, int commandIndex);
    void clearBreakpoints() { m_breakpoints.clear(); m_breakpointConditions.clear(); m_resumePastBreakpoint.clear(); }
    bool hasBreakpoint(const QString& source, int commandIndex) const;
    QStringList breakpoints() const;
    void setBreakpointCondition(const QString& source, int commandIndex,
                                const QVariantMap& condition);
    QVariantMap breakpointCondition(const QString& source, int commandIndex) const;
    void recordTargetResolution(QString summary) { m_runtime.targetResolution = std::move(summary); }
    void recordConditionEvaluation(QString summary) { m_runtime.conditionEvaluation = std::move(summary); }
    void setRuntimeState(EventRuntimeDebugState state) { m_runtime = std::move(state); }
    const EventRuntimeDebugState& runtimeState() const { return m_runtime; }

private:
    static QString breakpointKey(const QString& source, int commandIndex);
    enum class StepMode { None, Into, Over, Out };
    bool m_paused = false;
    bool m_blocked = false;
    int m_stepBudget = 0; // legado interno; mantido para diagnóstico compatível
    StepMode m_stepMode = StepMode::None;
    int m_stepOriginDepth = 0;
    bool m_stepHasExecuted = false;
    QSet<QString> m_breakpoints;
    QHash<QString, QVariantMap> m_breakpointConditions;
    QString m_resumePastBreakpoint;
    DebugCommandLocation m_current;
    EventRuntimeDebugState m_runtime;
};

struct RuntimeProfilerSnapshot {
    double fps = 0.0;
    double frameMs = 0.0;
    double averageFrameMs = 0.0;
    double p50FrameMs = 0.0;
    double p95FrameMs = 0.0;
    double p99FrameMs = 0.0;
    double maxFrameMs = 0.0;
    int sampleCount = 0;
    int drawCalls = 0;
    int quads = 0;
    QString renderer;
    int activeInterpreters = 0;
    int traceEntries = 0;
    QHash<QString, double> stageMs;
    qint64 textureUploadBytes = 0;
    qint64 vertexUploadBytes = 0;
    qint64 dynamicVertexUploadBytes = 0;
    qint64 staticMapVertexUploadBytes = 0;
    int gpuMapMeshBuilds = 0;
    int textureUploads = 0;
    int mapCacheHits = 0;
    int mapCacheMisses = 0;
    int pipelineChanges = 0;
    int shaderResourceChanges = 0;
    int lateFrames = 0;
    int totalFrames = 0;
    double lateFrameRatio = 0.0;
    double lastFrameOverrunMs = 0.0;
    double worstFrameOverrunMs = 0.0;
};

class RuntimeProfiler {
public:
    void submitFrame(double ms, double fps);
    void setRenderStats(QString renderer, int drawCalls, int quads);
    void setRuntimeCounts(int interpreters, int traceEntries);
    void setStageTiming(const QString& stage, double ms);
    void setFramePacingStats(int lateFrames, int totalFrames, double lateRatio,
                             double lastOverrunMs, double worstOverrunMs);
    void setRenderWorkStats(qint64 textureUploadBytes, qint64 vertexUploadBytes,
                            int textureUploads, int mapCacheHits, int mapCacheMisses,
                            int pipelineChanges, int shaderResourceChanges,
                            qint64 staticMapVertexUploadBytes = 0,
                            qint64 dynamicVertexUploadBytes = -1,
                            int gpuMapMeshBuilds = 0);
    RuntimeProfilerSnapshot snapshot() const;
    void reset();

private:
    QVector<double> m_frames;
    double m_lastFps = 0.0;
    QString m_renderer = QStringLiteral("unknown");
    int m_drawCalls = 0;
    int m_quads = 0;
    int m_interpreters = 0;
    int m_traceEntries = 0;
    QHash<QString, double> m_stageMs;
    qint64 m_textureUploadBytes = 0;
    qint64 m_vertexUploadBytes = 0;
    qint64 m_dynamicVertexUploadBytes = 0;
    qint64 m_staticMapVertexUploadBytes = 0;
    int m_gpuMapMeshBuilds = 0;
    int m_textureUploads = 0;
    int m_mapCacheHits = 0;
    int m_mapCacheMisses = 0;
    int m_pipelineChanges = 0;
    int m_shaderResourceChanges = 0;
    int m_lateFrames = 0;
    int m_totalFrames = 0;
    double m_lateFrameRatio = 0.0;
    double m_lastFrameOverrunMs = 0.0;
    double m_worstFrameOverrunMs = 0.0;
};

QJsonObject profilerToJson(const RuntimeProfilerSnapshot& snapshot);

} // namespace game
