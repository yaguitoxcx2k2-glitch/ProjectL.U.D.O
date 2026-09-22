#include "EditorTheme.h"
#include "EditorUiPreferences.h"

#include <QApplication>
#include <QFile>
#include <QList>
#include <QPalette>
#include <QPair>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace ui {
namespace {
QString recolor(QString qss, const EditorThemeTokens& t)
{
    // O style.qss continua sendo a descrição estrutural. As cores históricas
    // são tratadas como slots do Design System e substituídas aqui por tokens.
    const QList<QPair<QString, QString>> replacements = {
        {QStringLiteral("#2b2b2b"), t.background},
        {QStringLiteral("#353535"), t.surface},
        {QStringLiteral("#3d3d3d"), t.surfaceAlt},
        {QStringLiteral("#3c3c3c"), t.surfaceAlt},
        {QStringLiteral("#333333"), t.surface},
        {QStringLiteral("#303030"), t.surface},
        {QStringLiteral("#2f2f2f"), t.surface},
        {QStringLiteral("#262626"), t.surfaceRaised},
        {QStringLiteral("#2a2a2a"), t.surfaceRaised},
        {QStringLiteral("#1e1e1e"), t.input},
        {QStringLiteral("#1a1a1a"), t.input},
        {QStringLiteral("#222222"), t.border},
        {QStringLiteral("#444444"), t.border},
        {QStringLiteral("#444"), t.border},
        {QStringLiteral("#4a4a4a"), t.borderStrong},
        {QStringLiteral("#454545"), t.borderStrong},
        {QStringLiteral("#555555"), t.borderStrong},
        {QStringLiteral("#5a5a5a"), t.borderStrong},
        {QStringLiteral("#3a3a3a"), t.surfaceAlt},
        {QStringLiteral("#cccccc"), t.text},
        {QStringLiteral("#ccc"), t.text},
        {QStringLiteral("#dddddd"), t.text},
        {QStringLiteral("#bbbbbb"), t.textMuted},
        {QStringLiteral("#aaaaaa"), t.textMuted},
        {QStringLiteral("#999999"), t.textMuted},
        {QStringLiteral("#999"), t.textMuted},
        {QStringLiteral("#666666"), t.textDisabled},
        {QStringLiteral("#4a90d7"), t.accent},
        {QStringLiteral("#5aa0e7"), t.accentHover},
        {QStringLiteral("#ffffff"), t.accentText}
    };
    for (const auto& pair : replacements) qss.replace(pair.first, pair.second, Qt::CaseInsensitive);

    qss += QStringLiteral(R"QSS(
QWidget[uiRole="hint"] { color: %1; }
QLabel[uiRole="sectionTitle"] { color: %2; font-weight: 600; font-size: 13px; }
QFrame[uiRole="infoBanner"] { background: %3; border: 1px solid %4; border-radius: 5px; }
QFrame[uiRole="warningBanner"] { background: %3; border: 1px solid %5; border-radius: 5px; }
QLabel[uiRole="warningText"] { color: %5; }
QLabel[uiRole="errorText"] { color: %6; }
QPushButton[uiRole="danger"] { border-color: %6; color: %6; }
QPushButton[uiRole="danger"]:hover { background: %6; color: %7; }
)QSS").arg(t.textMuted, t.text, t.surfaceRaised, t.accent, t.warning, t.danger, t.accentText);
    qss += QStringLiteral(R"QSS(
QWidget[uiRole="previewCanvas"] { background: %1; border: 1px solid %2; color: %3; }
QLabel[uiRole="successText"] { color: %4; }
)QSS").arg(t.input, t.borderStrong, t.textMuted, QStringLiteral("#4f8f58"));
    return qss;
}
}

QString EditorTheme::effectiveTheme(const QString&, const QApplication&)
{
    return QStringLiteral("dark");
}

EditorThemeTokens EditorTheme::tokensFor(const QString&)
{
    return {
        QStringLiteral("#2b2b2b"), QStringLiteral("#353535"), QStringLiteral("#3d3d3d"),
        QStringLiteral("#262626"), QStringLiteral("#1e1e1e"), QStringLiteral("#444444"),
        QStringLiteral("#4a4a4a"), QStringLiteral("#cccccc"), QStringLiteral("#999999"),
        QStringLiteral("#666666"), QStringLiteral("#4a90d7"), QStringLiteral("#5aa0e7"),
        QStringLiteral("#ffffff"), QStringLiteral("#ff6b6b"), QStringLiteral("#ffd166")
    };
}

void EditorTheme::apply(QApplication& app)
{
    apply(app, EditorUiPreferences::load().theme);
}

void EditorTheme::apply(QApplication& app, const QString&)
{
    // Não deixe widgets sem regra QSS herdarem a paleta clara/accent do
    // Windows. Fusion + paleta explícita tornam o Editor 100% escuro em todos
    // os modos do sistema operacional.
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    if(QStyle* fusion=QStyleFactory::create(QStringLiteral("Fusion")))app.setStyle(fusion);
    const auto t=tokensFor(QStringLiteral("dark"));
    QPalette palette;
    palette.setColor(QPalette::Window,QColor(t.background));
    palette.setColor(QPalette::WindowText,QColor(t.text));
    palette.setColor(QPalette::Base,QColor(t.input));
    palette.setColor(QPalette::AlternateBase,QColor(t.surfaceAlt));
    palette.setColor(QPalette::ToolTipBase,QColor(t.surfaceRaised));
    palette.setColor(QPalette::ToolTipText,QColor(t.text));
    palette.setColor(QPalette::Text,QColor(t.text));
    palette.setColor(QPalette::Button,QColor(t.surface));
    palette.setColor(QPalette::ButtonText,QColor(t.text));
    palette.setColor(QPalette::BrightText,QColor(t.accentText));
    palette.setColor(QPalette::Highlight,QColor(t.accent));
    palette.setColor(QPalette::HighlightedText,QColor(t.accentText));
    palette.setColor(QPalette::Disabled,QPalette::WindowText,QColor(t.textDisabled));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor(t.textDisabled));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(t.textDisabled));
    app.setPalette(palette);
    QFile qss(QStringLiteral(":/resources/style.qss"));
    if (!qss.open(QIODevice::ReadOnly)) return;
    app.setProperty("editorTheme", QStringLiteral("dark"));
    app.setStyleSheet(recolor(QString::fromUtf8(qss.readAll()), t));
}

} // namespace ui
