#include "EditorUiPreferences.h"

#include <QSettings>

namespace ui {
namespace {
QSettings editorSettings()
{
    // Pode ser usado antes da construção de QApplication.
    return QSettings(QStringLiteral("LudoEngine"), QStringLiteral("Ludo Engine"));
}


}

int EditorUiPreferences::normalizeUiScalePercent(int percent)
{
    static const int allowed[] = {100, 110, 125, 150};
    int best = allowed[0];
    int distance = qAbs(percent - best);
    for (int candidate : allowed) {
        const int d = qAbs(percent - candidate);
        if (d < distance) { best = candidate; distance = d; }
    }
    return best;
}

int EditorUiPreferences::storedUiScalePercent()
{
    QSettings s = editorSettings();
    return normalizeUiScalePercent(s.value(QStringLiteral("editor/uiScalePercent"), 100).toInt());
}

EditorUiPreferences EditorUiPreferences::load()
{
    QSettings s = editorSettings();
    EditorUiPreferences out;
    out.theme = QStringLiteral("dark");
    out.uiScalePercent = normalizeUiScalePercent(s.value(QStringLiteral("editor/uiScalePercent"), 100).toInt());
    out.showGrid = s.value(QStringLiteral("showGrid"), true).toBool();
    out.multigrid = s.value(QStringLiteral("multigrid"), false).toBool();
    out.gridColor = s.value(QStringLiteral("gridColor"), QColor(18,20,24,190)).value<QColor>();
    out.checkerboardBackground = s.value(QStringLiteral("checkerboardBackground"), true).toBool();
    out.checkerColorA = s.value(QStringLiteral("checkerColorA"), QColor(74,78,84)).value<QColor>();
    out.checkerColorB = s.value(QStringLiteral("checkerColorB"), QColor(46,49,54)).value<QColor>();
    out.ghostPreview = s.value(QStringLiteral("ghostPreview"), true).toBool();
    out.snapGridSize = qBound(1, s.value(QStringLiteral("snapGridSize"), 32).toInt(), 512);
    out.focusDim = qBound(0.0, s.value(QStringLiteral("focusDim"), 0.25).toDouble(), 1.0);
    return out;
}

void EditorUiPreferences::save() const
{
    QSettings s = editorSettings();
    s.remove(QStringLiteral("editor/theme")); // tema único: escuro
    s.setValue(QStringLiteral("editor/uiScalePercent"), normalizeUiScalePercent(uiScalePercent));
    // Backend e filtro pertencem ao projeto (Configurações do Jogo), não às
    // preferências locais do Editor. Removemos chaves legadas para impedir que
    // uma configuração antiga volte a divergir de F5/F6.
    s.remove(QStringLiteral("game/renderer"));
    s.remove(QStringLiteral("game/gpuBackend"));
    s.remove(QStringLiteral("game/scaleFilter"));
    s.setValue(QStringLiteral("showGrid"), showGrid);
    s.setValue(QStringLiteral("multigrid"), multigrid);
    s.setValue(QStringLiteral("gridColor"), gridColor);
    s.setValue(QStringLiteral("checkerboardBackground"), checkerboardBackground);
    s.setValue(QStringLiteral("checkerColorA"), checkerColorA);
    s.setValue(QStringLiteral("checkerColorB"), checkerColorB);
    s.setValue(QStringLiteral("ghostPreview"), ghostPreview);
    s.setValue(QStringLiteral("snapGridSize"), qBound(1, snapGridSize, 512));
    s.setValue(QStringLiteral("focusDim"), qBound(0.0, focusDim, 1.0));
}

} // namespace ui
