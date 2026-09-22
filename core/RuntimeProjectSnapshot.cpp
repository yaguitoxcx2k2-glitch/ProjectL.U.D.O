#include "RuntimeProjectSnapshot.h"

#include "Editor.h"
#include "ProjectIO.h"

#include <QJsonObject>
#include <QSet>

namespace core {
namespace {

QVector<LayerPtr> cloneLayers(const QVector<LayerPtr>& source)
{
    QVector<LayerPtr> out;
    out.reserve(source.size());
    for (const LayerPtr& layer : source) out.push_back(cloneLayer(layer, false));
    return out;
}

MapDoc cloneDocument(const MapDoc& source)
{
    MapDoc out = source;
    out.layers = cloneLayers(source.layers);
    // O runtime nao precisa do historico do editor e nao deve manter snapshots
    // gigantes vivos durante o jogo.
    out.history.clear();
    out.historyPtr = -1;
    out.historyRevision = 0;
    out.dirty = false;
    return out;
}

} // namespace

QStringList runtimeProjectParityIssues(const Editor& source, const Editor& runtime)
{
    const QJsonObject expected = io::buildRuntimeProjectPayload(source);
    const QJsonObject actual = io::buildRuntimeProjectPayload(runtime);
    if (expected == actual) return {};

    QStringList issues;
    QSet<QString> keys;
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) keys.insert(it.key());
    for (auto it = actual.constBegin(); it != actual.constEnd(); ++it) keys.insert(it.key());
    QStringList ordered = keys.values();
    ordered.sort(Qt::CaseInsensitive);
    for (const QString& key : ordered)
        if (expected.value(key) != actual.value(key)) issues.push_back(key);
    return issues;
}

namespace {

void populateRuntimeEditor(Editor& out, const Editor& source)
{
    // Identidade/configuracao do projeto.
    out.projectName = source.projectName;
    out.projectId = source.projectId;
    out.projectPath = source.projectPath; // mantem resolucao de Assets relativos
    out.startMapId = source.startMapId;
    out.startPosition = source.startPosition;

    // Recursos globais. Containers e imagens Qt sao COW, portanto esta copia e
    // barata ate que algum lado realmente altere o recurso.
    out.assetDatabase = source.assetDatabase;
    out.tilesets = source.tilesets;
    out.autotiles = source.autotiles;
    out.wangSets = source.wangSets;
    out.wangPresets = source.wangPresets;
    out.starTiles = source.starTiles;
    // Prioridade, colisao e probabilidade agora viajam dentro de `tilesets`.
    // Nao existe mais um mapa global paralelo para copiar ao runtime.
    out.randomPool = source.randomPool;
    out.savedStamps = source.savedStamps;
    out.recentTiles = source.recentTiles;

    out.player = source.player;
    out.inputMap = source.inputMap;
    out.inputSystem = source.inputSystem;
    out.cutsceneSkip = source.cutsceneSkip;
    out.titleScreen = source.titleScreen;
    out.gameUi = source.gameUi;
    out.localization = source.localization;
    out.accessibility = source.accessibility;
    out.subtitleStyle = source.subtitleStyle;
    out.speakerDatabase = source.speakerDatabase;
    out.gameResolution = source.gameResolution;
    // Estas duas preferencias pertencem ao projeto e fazem parte do objeto
    // `game` do payload executavel. O snapshot precisa copia-las mesmo quando
    // nao sao os defaults, senao F5/F6 diverge do game.ludo exportado.
    out.runtimeGpuBackend = source.runtimeGpuBackend;
    out.runtimeScaleFilter = source.runtimeScaleFilter;
    out.autosaveEnabled = source.autosaveEnabled;
    out.autosaveSlot = source.autosaveSlot;
    out.checkpointSlot = source.checkpointSlot;
    out.quickSaveEnabled = source.quickSaveEnabled;
    out.quickSaveSlot = source.quickSaveSlot;
    out.iconSet = source.iconSet;
    out.projectFonts = source.projectFonts;
    out.mainFontFamily = source.mainFontFamily;

    out.switches = source.switches;
    out.variables = source.variables;
    out.strings = source.strings;
    out.commonEvents = source.commonEvents;
    // RC2.51.1: FootstepSurface/FootstepSettings são dados executáveis do
    // projeto. Eles precisam cruzar a mesma fronteira F5/F6/Player que os
    // demais GameData; omiti-los fazia `footsteps` e, por consequência, o
    // grafo `assetReferences` divergirem do payload exportado.
    out.footstepSurfaces = source.footstepSurfaces;
    out.footstepSettings = source.footstepSettings;
    out.database = source.database;
    out.customDatabases = source.customDatabases;
    out.plugins = source.plugins;
    out.pictures = source.pictures;

    out.docs.clear();
    out.docs.reserve(source.docs.size());
    for (const MapDoc& doc : source.docs) out.docs.push_back(cloneDocument(doc));
    out.activeDocIdx = source.activeDocIdx;
    if (out.activeDocIdx < 0 || out.activeDocIdx >= out.docs.size())
        out.activeDocIdx = out.docs.isEmpty() ? -1 : 0;

    // Caches de preload pertencem a uma geração específica do payload. Hot
    // Reload e novos snapshots sempre começam sem caches derivados antigos.
    out.setRuntimePreloadCache({});

    // Estado puramente editorial não acompanha o runtime.
    out.session.resetForRuntime();
    out.projectDirty = false;
}

} // namespace

std::unique_ptr<Editor> makeRuntimeEditorSnapshot(const Editor& source, QString* error)
{
    if (error) error->clear();
    auto out = std::make_unique<Editor>();
    populateRuntimeEditor(*out, source);

    const QStringList parityIssues = runtimeProjectParityIssues(source, *out);
    if (!parityIssues.isEmpty()) {
        if (error) *error = QStringLiteral("Runtime Snapshot divergiu do payload exportado: %1")
                                .arg(parityIssues.join(QStringLiteral(", ")));
        return {};
    }
    return out;
}

bool refreshRuntimeEditorSnapshot(Editor& runtime, const Editor& source, QString* error)
{
    if (error) error->clear();

    // Hot Reload não é um novo F5: o jogador pode estar em outro mapa enquanto
    // o editor principal está olhando uma aba diferente. A identidade do mapa
    // vivo precisa sobreviver à atualização do modelo.
    const QString runtimeMapId = runtime.doc() ? runtime.doc()->id : QString();

    // Valida primeiro numa cópia independente; se a fronteira executável
    // divergir ou o mapa vivo tiver sido removido, o Editor runtime permanece
    // completamente intocado.
    std::unique_ptr<Editor> fresh = makeRuntimeEditorSnapshot(source, error);
    if (!fresh) return false;
    if (!runtimeMapId.isEmpty()) {
        const int currentIndex = fresh->mapIndexById(runtimeMapId);
        if (currentIndex < 0) {
            if (error) *error = QStringLiteral("Hot Reload recusado: o mapa runtime ativo não existe mais: %1")
                                    .arg(runtimeMapId);
            return false;
        }
        fresh->activeDocIdx = currentIndex;
    }

    populateRuntimeEditor(runtime, *fresh);
    const QStringList parityIssues = runtimeProjectParityIssues(source, runtime);
    if (!parityIssues.isEmpty()) {
        if (error) *error = QStringLiteral("Hot Reload divergiu do payload executável: %1")
                                .arg(parityIssues.join(QStringLiteral(", ")));
        return false;
    }
    return true;
}

} // namespace core
