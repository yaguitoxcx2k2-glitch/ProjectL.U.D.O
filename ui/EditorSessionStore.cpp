#include "EditorSessionStore.h"
#include "core/MapWorkspace.h"
#include "core/PaintOps.h"

#include <QCryptographicHash>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

namespace ui {
namespace {
QString settingsRoot(const core::Editor& editor)
{
    return QStringLiteral("editor/projectSessions/%1").arg(EditorSessionStore::projectKey(editor));
}
}

QString EditorSessionStore::projectKey(const core::Editor& editor)
{
    QString identity = editor.projectId.trimmed();
    if (identity.isEmpty()) identity = editor.projectPath.trimmed();
    if (identity.isEmpty()) identity = editor.projectName.trimmed();
    if (identity.isEmpty()) identity = QStringLiteral("unsaved-project");
    return QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}

void EditorSessionStore::save(const core::Editor& editor)
{
    QSettings settings;
    settings.beginGroup(settingsRoot(editor));
    QStringList openMaps; for (const QString& id : editor.session.openMapIds) openMaps.push_back(id);
    settings.setValue(QStringLiteral("openMaps"), openMaps);
    settings.setValue(QStringLiteral("activeMap"), editor.doc() ? editor.doc()->id : QString());
    settings.setValue(QStringLiteral("inspectorPinned"), editor.session.inspectorPinned);

    settings.setValue(QStringLiteral("brush/size"), editor.session.brush.size);
    settings.setValue(QStringLiteral("brush/shape"), editor.session.brush.shape);
    settings.setValue(QStringLiteral("brush/density"), editor.session.brush.density);
    settings.setValue(QStringLiteral("brush/spacing"), editor.session.brush.spacing);
    settings.setValue(QStringLiteral("brush/alphaMaskPath"), editor.session.brush.alphaMaskPath);
    settings.setValue(QStringLiteral("brush/alphaMaskRotation"), editor.session.brush.alphaMaskRotation);
    settings.setValue(QStringLiteral("brush/alphaMaskInvert"), editor.session.brush.alphaMaskInvert);

    const auto& rb = editor.session.rasterBrush;
    settings.setValue(QStringLiteral("rasterBrush/authoringMode"), rb.authoringMode);
    settings.setValue(QStringLiteral("rasterBrush/sizePx"), rb.sizePx);
    settings.setValue(QStringLiteral("rasterBrush/opacity"), rb.opacity);
    settings.setValue(QStringLiteral("rasterBrush/flow"), rb.flow);
    settings.setValue(QStringLiteral("rasterBrush/hardness"), rb.hardness);
    settings.setValue(QStringLiteral("rasterBrush/spacingPercent"), rb.spacingPercent);
    settings.setValue(QStringLiteral("rasterBrush/color"), rb.color);
    settings.setValue(QStringLiteral("rasterBrush/tipMode"), rb.tipMode);
    settings.setValue(QStringLiteral("rasterBrush/tipImagePath"), rb.tipImagePath);
    settings.setValue(QStringLiteral("rasterBrush/rotation"), rb.rotation);
    settings.setValue(QStringLiteral("rasterBrush/rotateToStroke"), rb.rotateToStroke);
    settings.setValue(QStringLiteral("rasterBrush/scatterPercent"), rb.scatterPercent);
    settings.setValue(QStringLiteral("rasterBrush/sizeJitter"), rb.sizeJitter);
    settings.setValue(QStringLiteral("rasterBrush/rotationJitter"), rb.rotationJitter);
    settings.setValue(QStringLiteral("rasterBrush/blendMode"), rb.blendMode);
    settings.setValue(QStringLiteral("rasterBrush/pixelSize"), rb.pixelSize);
    settings.setValue(QStringLiteral("rasterBrush/pixelScale"), rb.pixelScale);
    settings.setValue(QStringLiteral("rasterBrush/pixelShape"), rb.pixelShape);
    settings.setValue(QStringLiteral("rasterBrush/pixelDither"), rb.pixelDither);
    settings.setValue(QStringLiteral("rasterBrush/pixelMirrorH"), rb.pixelMirrorH);
    settings.setValue(QStringLiteral("rasterBrush/pixelMirrorV"), rb.pixelMirrorV);
    settings.setValue(QStringLiteral("rasterBrush/pixelReplaceEnabled"), rb.pixelReplaceEnabled);
    settings.setValue(QStringLiteral("rasterBrush/pixelReplaceColor"), rb.pixelReplaceColor);
    settings.setValue(QStringLiteral("rasterBrush/softenImageEdges"), rb.softenImageEdges);
    settings.setValue(QStringLiteral("rasterBrush/edgeSoftnessPercent"), rb.edgeSoftnessPercent);
    settings.setValue(QStringLiteral("rasterBrush/edgeSoftnessStrength"), rb.edgeSoftnessStrength);
    settings.setValue(QStringLiteral("rasterBrush/edgeIrregularityPercent"), rb.edgeIrregularityPercent);
    settings.setValue(QStringLiteral("rasterBrush/preserveEdgeCenter"), rb.preserveEdgeCenter);

    QVariantList viewports;
    for (auto it = editor.session.mapViewports.cbegin(); it != editor.session.mapViewports.cend(); ++it) {
        const core::MapViewportState& state = it.value();
        if (!state.initialized) continue;
        QVariantMap row;
        row.insert(QStringLiteral("mapId"), it.key());
        row.insert(QStringLiteral("zoom"), state.zoom);
        row.insert(QStringLiteral("center"), state.center);
        viewports.push_back(row);
    }
    settings.setValue(QStringLiteral("viewports"), viewports);
    settings.endGroup();
}

bool EditorSessionStore::restore(core::Editor& editor)
{
    QSettings settings;
    settings.beginGroup(settingsRoot(editor));
    const QStringList openMaps = settings.value(QStringLiteral("openMaps")).toStringList();
    const QString activeMap = settings.value(QStringLiteral("activeMap")).toString();
    const bool hasBrushSession = settings.contains(QStringLiteral("brush/size"));

    if (hasBrushSession) {
        editor.session.brush.size = qBound(1, settings.value(QStringLiteral("brush/size"), 1).toInt(), 32);
        editor.session.brush.shape = settings.value(QStringLiteral("brush/shape"), QStringLiteral("square")).toString();
        if (editor.session.brush.shape != QLatin1String("square") &&
            editor.session.brush.shape != QLatin1String("circle") &&
            editor.session.brush.shape != QLatin1String("diamond") &&
            editor.session.brush.shape != QLatin1String("alpha"))
            editor.session.brush.shape = QStringLiteral("square");
        editor.session.brush.density = qBound(0, settings.value(QStringLiteral("brush/density"), 100).toInt(), 100);
        editor.session.brush.spacing = qBound(1, settings.value(QStringLiteral("brush/spacing"), 1).toInt(), 64);
        editor.session.brush.alphaMaskPath = settings.value(QStringLiteral("brush/alphaMaskPath")).toString();
        editor.session.brush.alphaMaskRotation = settings.value(QStringLiteral("brush/alphaMaskRotation"), 0).toInt();
        editor.session.brush.alphaMaskInvert = settings.value(QStringLiteral("brush/alphaMaskInvert"), false).toBool();
        editor.session.brush.alphaMask = core::paint::loadBrushAlphaMask(editor.session.brush.alphaMaskPath);
        if (editor.session.brush.shape == QLatin1String("alpha") && editor.session.brush.alphaMask.isNull())
            editor.session.brush.shape = QStringLiteral("square");
    }

    auto& rb = editor.session.rasterBrush;
    rb.authoringMode = settings.value(QStringLiteral("rasterBrush/authoringMode"), QStringLiteral("normal")).toString();
    if (rb.authoringMode != QLatin1String("normal") && rb.authoringMode != QLatin1String("pixel-art"))
        rb.authoringMode = QStringLiteral("normal");
    rb.sizePx = qBound(1, settings.value(QStringLiteral("rasterBrush/sizePx"), 64).toInt(), 2048);
    rb.opacity = qBound(1, settings.value(QStringLiteral("rasterBrush/opacity"), 100).toInt(), 100);
    rb.flow = qBound(1, settings.value(QStringLiteral("rasterBrush/flow"), 100).toInt(), 100);
    rb.hardness = qBound(0, settings.value(QStringLiteral("rasterBrush/hardness"), 80).toInt(), 100);
    rb.spacingPercent = qBound(1, settings.value(QStringLiteral("rasterBrush/spacingPercent"), 20).toInt(), 400);
    rb.color = settings.value(QStringLiteral("rasterBrush/color"), QColor(90, 70, 55)).value<QColor>();
    rb.tipMode = settings.value(QStringLiteral("rasterBrush/tipMode"), QStringLiteral("round")).toString();
    if (rb.tipMode != QLatin1String("round") && rb.tipMode != QLatin1String("alpha") && rb.tipMode != QLatin1String("color"))
        rb.tipMode = QStringLiteral("round");
    rb.tipImagePath = settings.value(QStringLiteral("rasterBrush/tipImagePath")).toString();
    rb.tipImage = core::paint::loadRasterBrushTip(rb.tipImagePath);
    if (rb.tipMode != QLatin1String("round") && rb.tipImage.isNull()) rb.tipMode = QStringLiteral("round");
    rb.rotation = settings.value(QStringLiteral("rasterBrush/rotation"), 0).toInt();
    rb.rotateToStroke = settings.value(QStringLiteral("rasterBrush/rotateToStroke"), false).toBool();
    rb.scatterPercent = qBound(0, settings.value(QStringLiteral("rasterBrush/scatterPercent"), 0).toInt(), 400);
    rb.sizeJitter = qBound(0, settings.value(QStringLiteral("rasterBrush/sizeJitter"), 0).toInt(), 100);
    rb.rotationJitter = qBound(0, settings.value(QStringLiteral("rasterBrush/rotationJitter"), 0).toInt(), 360);
    rb.blendMode = settings.value(QStringLiteral("rasterBrush/blendMode"), QStringLiteral("source-over")).toString();
    rb.pixelSize = qBound(1, settings.value(QStringLiteral("rasterBrush/pixelSize"), 1).toInt(), 2048);
    rb.pixelScale = qBound(1, settings.value(QStringLiteral("rasterBrush/pixelScale"), 1).toInt(), 8);
    rb.pixelSize = qMin(rb.pixelSize, qMax(1, 2048 / rb.pixelScale));
    rb.pixelShape = settings.value(QStringLiteral("rasterBrush/pixelShape"), QStringLiteral("square")).toString();
    if (rb.pixelShape != QLatin1String("square") && rb.pixelShape != QLatin1String("circle"))
        rb.pixelShape = QStringLiteral("square");
    rb.pixelDither = settings.value(QStringLiteral("rasterBrush/pixelDither"), QStringLiteral("none")).toString();
    if (rb.pixelDither != QLatin1String("none") && rb.pixelDither != QLatin1String("25") &&
        rb.pixelDither != QLatin1String("50") && rb.pixelDither != QLatin1String("75"))
        rb.pixelDither = QStringLiteral("none");
    rb.pixelMirrorH = settings.value(QStringLiteral("rasterBrush/pixelMirrorH"), false).toBool();
    rb.pixelMirrorV = settings.value(QStringLiteral("rasterBrush/pixelMirrorV"), false).toBool();
    rb.pixelReplaceEnabled = settings.value(QStringLiteral("rasterBrush/pixelReplaceEnabled"), false).toBool();
    rb.pixelReplaceColor = settings.value(QStringLiteral("rasterBrush/pixelReplaceColor"), QColor(0,0,0,255)).value<QColor>();
    rb.softenImageEdges = settings.value(QStringLiteral("rasterBrush/softenImageEdges"), false).toBool();
    rb.edgeSoftnessPercent = qBound(1, settings.value(QStringLiteral("rasterBrush/edgeSoftnessPercent"), 18).toInt(), 50);
    rb.edgeSoftnessStrength = qBound(0, settings.value(QStringLiteral("rasterBrush/edgeSoftnessStrength"), 70).toInt(), 100);
    rb.edgeIrregularityPercent = qBound(0, settings.value(QStringLiteral("rasterBrush/edgeIrregularityPercent"), 30).toInt(), 100);
    rb.preserveEdgeCenter = settings.value(QStringLiteral("rasterBrush/preserveEdgeCenter"), true).toBool();

    const bool hasStoredSession = !openMaps.isEmpty() || !activeMap.isEmpty();
    if (!hasStoredSession) { settings.endGroup(); return hasBrushSession; }

    editor.session.openMapIds.clear();
    for (const QString& id : openMaps)
        if (editor.mapById(id) && !editor.session.openMapIds.contains(id)) editor.session.openMapIds.push_back(id);
    editor.session.inspectorPinned = settings.value(QStringLiteral("inspectorPinned"), false).toBool();
    editor.session.mapViewports.clear();
    const QVariantList viewports = settings.value(QStringLiteral("viewports")).toList();
    for (const QVariant& value : viewports) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("mapId")).toString();
        if (!editor.mapById(id)) continue;
        core::MapViewportState state;
        state.zoom = row.value(QStringLiteral("zoom"), 1.0).toDouble();
        state.center = row.value(QStringLiteral("center")).toPointF();
        state.initialized = true;
        editor.session.mapViewports.insert(id, state);
    }
    core::mapworkspace::normalize(editor);
    if (!activeMap.isEmpty()) {
        for (int i = 0; i < editor.docs.size(); ++i)
            if (editor.docs.at(i).id == activeMap) { editor.switchDoc(i); break; }
    }
    settings.endGroup();
    return true;
}

} // namespace ui
