#pragma once

#include <QString>

class QApplication;

namespace ui {

struct EditorThemeTokens {
    QString background;
    QString surface;
    QString surfaceAlt;
    QString surfaceRaised;
    QString input;
    QString border;
    QString borderStrong;
    QString text;
    QString textMuted;
    QString textDisabled;
    QString accent;
    QString accentHover;
    QString accentText;
    QString danger;
    QString warning;
};

class EditorTheme final {
public:
    static QString effectiveTheme(const QString& requested, const QApplication& app);
    static EditorThemeTokens tokensFor(const QString& effectiveTheme);
    static void apply(QApplication& app);
    static void apply(QApplication& app, const QString& requestedTheme);
};

} // namespace ui
