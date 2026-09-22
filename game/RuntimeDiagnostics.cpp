#include "RuntimeDiagnostics.h"

#include <QtMath>
#include <algorithm>

namespace game {

QString debugWatchKindId(DebugWatchKind kind)
{
    switch (kind) {
    case DebugWatchKind::Variable: return QStringLiteral("variable");
    case DebugWatchKind::Switch: return QStringLiteral("switch");
    case DebugWatchKind::String: return QStringLiteral("string");
    case DebugWatchKind::GameValue: return QStringLiteral("gameValue");
    }
    return QStringLiteral("variable");
}

QString debugWatchKindLabel(DebugWatchKind kind)
{
    switch (kind) {
    case DebugWatchKind::Variable: return QStringLiteral("Variável");
    case DebugWatchKind::Switch: return QStringLiteral("Switch");
    case DebugWatchKind::String: return QStringLiteral("String");
    case DebugWatchKind::GameValue: return QStringLiteral("Game Value");
    }
    return QStringLiteral("Variável");
}

DebugWatchKind debugWatchKindFromId(const QString& id)
{
    if (id == QLatin1String("switch")) return DebugWatchKind::Switch;
    if (id == QLatin1String("string")) return DebugWatchKind::String;
    if (id == QLatin1String("gameValue")) return DebugWatchKind::GameValue;
    return DebugWatchKind::Variable;
}

namespace {
bool watchCompare(const QVariant& left, const QString& op, const QVariant& right)
{
    if (op.isEmpty()) return false;
    if (op == QLatin1String("contains")) return left.toString().contains(right.toString());
    if (op == QLatin1String("startsWith")) return left.toString().startsWith(right.toString());
    if (op == QLatin1String("endsWith")) return left.toString().endsWith(right.toString());
    bool leftNumber = false, rightNumber = false;
    const double a = left.toDouble(&leftNumber), b = right.toDouble(&rightNumber);
    if (leftNumber && rightNumber) {
        if (op == QLatin1String(">")) return a > b;
        if (op == QLatin1String(">=")) return a >= b;
        if (op == QLatin1String("<")) return a < b;
        if (op == QLatin1String("<=")) return a <= b;
        if (op == QLatin1String("!=")) return a != b;
        return a == b;
    }
    if (op == QLatin1String("!=")) return left.toString() != right.toString();
    return left.toString() == right.toString();
}
}

int DebugWatchStore::indexOf(quint64 serial) const
{
    for (int i = 0; i < m_watches.size(); ++i)
        if (m_watches.at(i).serial == serial) return i;
    return -1;
}

quint64 DebugWatchStore::add(DebugWatchKind kind, QString reference, QString label)
{
    reference = reference.trimmed();
    if (reference.isEmpty()) return 0;
    for (const DebugWatch& watch : m_watches)
        if (watch.kind == kind && watch.reference == reference) return watch.serial;
    DebugWatch watch;
    watch.serial = m_nextSerial++;
    watch.kind = kind;
    watch.reference = std::move(reference);
    watch.label = label.trimmed();
    m_watches.push_back(watch);
    return watch.serial;
}

bool DebugWatchStore::remove(quint64 serial)
{
    const int index = indexOf(serial);
    if (index < 0) return false;
    m_watches.removeAt(index);
    m_samples.remove(serial);
    m_previousCondition.remove(serial);
    return true;
}

bool DebugWatchStore::setBreakOnChange(quint64 serial, bool enabled)
{
    const int index = indexOf(serial);
    if (index < 0) return false;
    m_watches[index].breakOnChange = enabled;
    return true;
}

bool DebugWatchStore::setCondition(quint64 serial, QString op, QVariant value)
{
    const int index = indexOf(serial);
    if (index < 0) return false;
    m_watches[index].breakOperator = op.trimmed();
    m_watches[index].breakValue = std::move(value);
    m_previousCondition.remove(serial);
    return true;
}

bool DebugWatchStore::setEnabled(quint64 serial, bool enabled)
{
    const int index = indexOf(serial);
    if (index < 0) return false;
    m_watches[index].enabled = enabled;
    return true;
}

QVector<DebugWatchSample> DebugWatchStore::samples() const
{
    QVector<DebugWatchSample> result;
    result.reserve(m_watches.size());
    for (const DebugWatch& watch : m_watches) {
        DebugWatchSample sample = m_samples.value(watch.serial);
        sample.watch = watch;
        result.push_back(sample);
    }
    return result;
}

void DebugWatchStore::update(const Resolver& resolver)
{
    if (!resolver) return;
    for (const DebugWatch& watch : m_watches) {
        DebugWatchSample sample = m_samples.value(watch.serial);
        sample.watch = watch;
        const QVariant next = resolver(watch);
        sample.previousValue = sample.value;
        sample.changed = sample.initialized && sample.value != next;
        sample.value = next;
        sample.conditionMatched = watch.enabled && !watch.breakOperator.isEmpty()
            && watchCompare(next, watch.breakOperator, watch.breakValue);
        const bool previouslyMatched = m_previousCondition.value(watch.serial, false);
        if (watch.enabled && ((watch.breakOnChange && sample.changed)
            || (sample.conditionMatched && !previouslyMatched))) m_breakRequested = true;
        m_previousCondition.insert(watch.serial, sample.conditionMatched);
        sample.initialized = true;
        m_samples.insert(watch.serial, sample);
    }
}

bool DebugWatchStore::consumeBreakRequest()
{
    const bool requested = m_breakRequested;
    m_breakRequested = false;
    return requested;
}

QString EventDebugController::breakpointKey(const QString& source, int commandIndex)
{
    return source + QLatin1Char('#') + QString::number(commandIndex);
}

bool EventDebugController::hasBreakpoint(const QString& source, int commandIndex) const
{
    return m_breakpoints.contains(breakpointKey(source, commandIndex));
}

void EventDebugController::addBreakpoint(const QString& source, int commandIndex)
{
    if (!source.isEmpty() && commandIndex >= 0) m_breakpoints.insert(breakpointKey(source, commandIndex));
}

void EventDebugController::removeBreakpoint(const QString& source, int commandIndex)
{
    const QString key = breakpointKey(source, commandIndex);
    m_breakpoints.remove(key);
    m_breakpointConditions.remove(key);
    if (m_resumePastBreakpoint == key) m_resumePastBreakpoint.clear();
}

void EventDebugController::setBreakpointCondition(const QString& source, int commandIndex,
                                                  const QVariantMap& condition)
{
    const QString key = breakpointKey(source, commandIndex);
    if (source.isEmpty() || commandIndex < 0) return;
    m_breakpoints.insert(key);
    if (condition.isEmpty()) m_breakpointConditions.remove(key);
    else m_breakpointConditions.insert(key, condition);
}

QVariantMap EventDebugController::breakpointCondition(const QString& source, int commandIndex) const
{
    return m_breakpointConditions.value(breakpointKey(source, commandIndex));
}

QStringList EventDebugController::breakpoints() const
{
    QStringList result = m_breakpoints.values();
    result.sort();
    return result;
}

void EventDebugController::pause()
{
    m_paused = true;
    m_stepBudget = 0;
    m_stepMode = StepMode::None;
    m_stepHasExecuted = false;
}

void EventDebugController::continueRun()
{
    if (m_blocked && hasBreakpoint(m_current.source, m_current.commandIndex))
        m_resumePastBreakpoint = breakpointKey(m_current.source, m_current.commandIndex);
    m_paused = false;
    m_blocked = false;
    m_stepBudget = 0;
    m_stepMode = StepMode::None;
    m_stepHasExecuted = false;
}

void EventDebugController::step() { stepInto(); }

void EventDebugController::stepInto()
{
    m_paused = true;
    m_blocked = false;
    m_stepBudget = 0;
    m_stepMode = StepMode::Into;
    m_stepOriginDepth = qMax(0, m_current.callDepth);
    m_stepHasExecuted = false;
}

void EventDebugController::stepOver()
{
    m_paused = true;
    m_blocked = false;
    m_stepBudget = 0;
    m_stepMode = StepMode::Over;
    m_stepOriginDepth = qMax(0, m_current.callDepth);
    m_stepHasExecuted = false;
}

void EventDebugController::stepOut()
{
    m_paused = true;
    m_blocked = false;
    m_stepBudget = 0;
    m_stepMode = StepMode::Out;
    m_stepOriginDepth = qMax(1, m_current.callDepth);
    m_stepHasExecuted = false;
}

bool EventDebugController::beforeCommand(const DebugCommandLocation& location,
                                         const BreakpointEvaluator& evaluator)
{
    m_current = location;
    m_runtime.current = location;
    const QString key = breakpointKey(location.source, location.commandIndex);
    const bool skipBreakpointOnce = (!m_resumePastBreakpoint.isEmpty() && m_resumePastBreakpoint == key);
    if (skipBreakpointOnce) m_resumePastBreakpoint.clear();
    const bool hasConfiguredBreakpoint = hasBreakpoint(location.source, location.commandIndex);
    const QVariantMap condition = m_breakpointConditions.value(key);
    const bool breakpoint = hasConfiguredBreakpoint
        && (condition.isEmpty() || (evaluator && evaluator(condition)));

    if (breakpoint && !skipBreakpointOnce && (!m_paused || (m_stepMode != StepMode::None && m_stepHasExecuted))) {
        m_paused = true;
        m_blocked = true;
        m_stepMode = StepMode::None;
        return false;
    }
    if (!m_paused) {
        m_blocked = false;
        return true;
    }

    if (m_stepMode == StepMode::Into) {
        if (!m_stepHasExecuted) { m_stepHasExecuted = true; m_blocked = false; return true; }
        m_stepMode = StepMode::None; m_blocked = true; return false;
    }
    if (m_stepMode == StepMode::Over) {
        if (!m_stepHasExecuted) { m_stepHasExecuted = true; m_blocked = false; return true; }
        if (location.callDepth > m_stepOriginDepth) { m_blocked = false; return true; }
        m_stepMode = StepMode::None; m_blocked = true; return false;
    }
    if (m_stepMode == StepMode::Out) {
        m_stepHasExecuted = true;
        if (location.callDepth >= m_stepOriginDepth) { m_blocked = false; return true; }
        m_stepMode = StepMode::None; m_blocked = true; return false;
    }

    m_blocked = true;
    return false;
}

void RuntimeProfiler::submitFrame(double ms, double fps)
{
    if (qIsFinite(ms) && ms >= 0.0) {
        m_frames.push_back(ms);
        if (m_frames.size() > 600) m_frames.remove(0, m_frames.size() - 600);
    }
    if (qIsFinite(fps) && fps >= 0.0) m_lastFps = fps;
}

void RuntimeProfiler::setRenderStats(QString renderer, int drawCalls, int quads)
{
    m_renderer = renderer.trimmed().isEmpty() ? QStringLiteral("unknown") : renderer.trimmed();
    m_drawCalls = qMax(0, drawCalls);
    m_quads = qMax(0, quads);
}

void RuntimeProfiler::setRuntimeCounts(int interpreters, int traceEntries)
{
    m_interpreters = qMax(0, interpreters);
    m_traceEntries = qMax(0, traceEntries);
}

void RuntimeProfiler::setStageTiming(const QString& stage, double ms)
{
    if (stage.trimmed().isEmpty() || !qIsFinite(ms) || ms < 0.0) return;
    m_stageMs.insert(stage, ms);
}

void RuntimeProfiler::setFramePacingStats(int lateFrames, int totalFrames, double lateRatio,
                                          double lastOverrunMs, double worstOverrunMs)
{
    m_lateFrames = qMax(0, lateFrames);
    m_totalFrames = qMax(0, totalFrames);
    m_lateFrameRatio = qBound(0.0, lateRatio, 1.0);
    m_lastFrameOverrunMs = qMax(0.0, lastOverrunMs);
    m_worstFrameOverrunMs = qMax(0.0, worstOverrunMs);
}

void RuntimeProfiler::setRenderWorkStats(qint64 textureUploadBytes, qint64 vertexUploadBytes,
                                         int textureUploads, int mapCacheHits, int mapCacheMisses,
                                         int pipelineChanges, int shaderResourceChanges,
                                         qint64 staticMapVertexUploadBytes,
                                         qint64 dynamicVertexUploadBytes,
                                         int gpuMapMeshBuilds)
{
    m_textureUploadBytes = qMax<qint64>(0, textureUploadBytes);
    m_vertexUploadBytes = qMax<qint64>(0, vertexUploadBytes);
    m_staticMapVertexUploadBytes = qMax<qint64>(0, staticMapVertexUploadBytes);
    m_dynamicVertexUploadBytes = dynamicVertexUploadBytes < 0
        ? m_vertexUploadBytes : qMax<qint64>(0, dynamicVertexUploadBytes);
    m_gpuMapMeshBuilds = qMax(0, gpuMapMeshBuilds);
    m_textureUploads = qMax(0, textureUploads);
    m_mapCacheHits = qMax(0, mapCacheHits);
    m_mapCacheMisses = qMax(0, mapCacheMisses);
    m_pipelineChanges = qMax(0, pipelineChanges);
    m_shaderResourceChanges = qMax(0, shaderResourceChanges);
}

static double percentile(const QVector<double>& sorted, double quantile)
{
    if (sorted.isEmpty()) return 0.0;
    const int index = qBound(0, int(qCeil(double(sorted.size()) * quantile)) - 1, int(sorted.size()) - 1);
    return sorted.at(index);
}

RuntimeProfilerSnapshot RuntimeProfiler::snapshot() const
{
    RuntimeProfilerSnapshot out;
    out.fps = m_lastFps;
    out.renderer = m_renderer;
    out.drawCalls = m_drawCalls;
    out.quads = m_quads;
    out.activeInterpreters = m_interpreters;
    out.traceEntries = m_traceEntries;
    out.stageMs = m_stageMs;
    out.textureUploadBytes = m_textureUploadBytes;
    out.vertexUploadBytes = m_vertexUploadBytes;
    out.dynamicVertexUploadBytes = m_dynamicVertexUploadBytes;
    out.staticMapVertexUploadBytes = m_staticMapVertexUploadBytes;
    out.gpuMapMeshBuilds = m_gpuMapMeshBuilds;
    out.textureUploads = m_textureUploads;
    out.mapCacheHits = m_mapCacheHits;
    out.mapCacheMisses = m_mapCacheMisses;
    out.pipelineChanges = m_pipelineChanges;
    out.shaderResourceChanges = m_shaderResourceChanges;
    out.lateFrames = m_lateFrames;
    out.totalFrames = m_totalFrames;
    out.lateFrameRatio = m_lateFrameRatio;
    out.lastFrameOverrunMs = m_lastFrameOverrunMs;
    out.worstFrameOverrunMs = m_worstFrameOverrunMs;
    out.sampleCount = m_frames.size();
    if (m_frames.isEmpty()) return out;
    out.frameMs = m_frames.last();
    double total = 0.0;
    double maximum = 0.0;
    QVector<double> sorted = m_frames;
    for (double value : sorted) { total += value; maximum = qMax(maximum, value); }
    std::sort(sorted.begin(), sorted.end());
    out.averageFrameMs = total / double(sorted.size());
    out.p50FrameMs = percentile(sorted, 0.50);
    out.p95FrameMs = percentile(sorted, 0.95);
    out.p99FrameMs = percentile(sorted, 0.99);
    out.maxFrameMs = maximum;
    return out;
}

void RuntimeProfiler::reset()
{
    m_frames.clear();
    m_lastFps = 0.0;
    m_drawCalls = 0;
    m_quads = 0;
    m_interpreters = 0;
    m_traceEntries = 0;
    m_stageMs.clear();
    m_textureUploadBytes = 0;
    m_vertexUploadBytes = 0;
    m_dynamicVertexUploadBytes = 0;
    m_staticMapVertexUploadBytes = 0;
    m_gpuMapMeshBuilds = 0;
    m_textureUploads = 0;
    m_mapCacheHits = 0;
    m_mapCacheMisses = 0;
    m_pipelineChanges = 0;
    m_shaderResourceChanges = 0;
    m_lateFrames = 0;
    m_totalFrames = 0;
    m_lateFrameRatio = 0.0;
    m_lastFrameOverrunMs = 0.0;
    m_worstFrameOverrunMs = 0.0;
}

QJsonObject profilerToJson(const RuntimeProfilerSnapshot& s)
{
    QJsonObject stages;
    for (auto it = s.stageMs.cbegin(); it != s.stageMs.cend(); ++it) stages.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("fps"), s.fps},
                       {QStringLiteral("frameMs"), s.frameMs},
                       {QStringLiteral("averageFrameMs"), s.averageFrameMs},
                       {QStringLiteral("p50FrameMs"), s.p50FrameMs},
                       {QStringLiteral("p95FrameMs"), s.p95FrameMs},
                       {QStringLiteral("p99FrameMs"), s.p99FrameMs},
                       {QStringLiteral("maxFrameMs"), s.maxFrameMs},
                       {QStringLiteral("sampleCount"), s.sampleCount},
                       {QStringLiteral("drawCalls"), s.drawCalls},
                       {QStringLiteral("quads"), s.quads},
                       {QStringLiteral("renderer"), s.renderer},
                       {QStringLiteral("activeInterpreters"), s.activeInterpreters},
                       {QStringLiteral("traceEntries"), s.traceEntries},
                       {QStringLiteral("stageMs"), stages},
                       {QStringLiteral("textureUploadBytes"), double(s.textureUploadBytes)},
                       {QStringLiteral("vertexUploadBytes"), double(s.vertexUploadBytes)},
                       {QStringLiteral("dynamicVertexUploadBytes"), double(s.dynamicVertexUploadBytes)},
                       {QStringLiteral("staticMapVertexUploadBytes"), double(s.staticMapVertexUploadBytes)},
                       {QStringLiteral("gpuMapMeshBuilds"), s.gpuMapMeshBuilds},
                       {QStringLiteral("textureUploads"), s.textureUploads},
                       {QStringLiteral("mapCacheHits"), s.mapCacheHits},
                       {QStringLiteral("mapCacheMisses"), s.mapCacheMisses},
                       {QStringLiteral("pipelineChanges"), s.pipelineChanges},
                       {QStringLiteral("shaderResourceChanges"), s.shaderResourceChanges},
                       {QStringLiteral("lateFrames"), s.lateFrames},
                       {QStringLiteral("totalFrames"), s.totalFrames},
                       {QStringLiteral("lateFrameRatio"), s.lateFrameRatio},
                       {QStringLiteral("lastFrameOverrunMs"), s.lastFrameOverrunMs},
                       {QStringLiteral("worstFrameOverrunMs"), s.worstFrameOverrunMs}};
}

} // namespace game
