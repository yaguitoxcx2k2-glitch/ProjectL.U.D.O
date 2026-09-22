#pragma once

#include <QColor>
#include <QString>

namespace ui {

/// Preferências que pertencem à instalação local do Editor, nunca ao projeto.
/// Mantém a fronteira Editor/Projeto explícita e centraliza as chaves QSettings.
struct EditorUiPreferences {
    QString theme = QStringLiteral("dark"); // tema fixo do editor
    int uiScalePercent = 100;                  // 100 / 110 / 125 / 150
    bool showGrid = true;
    bool multigrid = false;
    QColor gridColor = QColor(18, 20, 24, 190);
    bool checkerboardBackground = true;
    QColor checkerColorA = QColor(74, 78, 84);
    QColor checkerColorB = QColor(46, 49, 54);
    bool ghostPreview = true;
    int snapGridSize = 32;
    double focusDim = 0.25;

    static EditorUiPreferences load();
    void save() const;

    static int storedUiScalePercent();
    static int normalizeUiScalePercent(int percent);
};

} // namespace ui
