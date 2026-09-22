#include "EditorDialogGeometry.h"

#include <QDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWidget>

namespace ui {

QRect fitEditorDialogRect(const QRect& available, const QRect& requested, const QSize& minimum)
{
    if (!available.isValid() || available.isEmpty()) return requested;

    const int minW = qMin(qMax(1, minimum.width()), available.width());
    const int minH = qMin(qMax(1, minimum.height()), available.height());
    const int reqW = requested.width() > 0 ? requested.width() : minW;
    const int reqH = requested.height() > 0 ? requested.height() : minH;
    const int w = qBound(minW, reqW, available.width());
    const int h = qBound(minH, reqH, available.height());

    int x = requested.isValid() ? requested.x() : available.center().x() - w / 2;
    int y = requested.isValid() ? requested.y() : available.center().y() - h / 2;
    x = qBound(available.left(), x, available.right() - w + 1);
    y = qBound(available.top(), y, available.bottom() - h + 1);
    return QRect(x, y, w, h);
}

static QScreen* screenForDialog(const QDialog& dialog)
{
    if (dialog.parentWidget() && dialog.parentWidget()->screen()) return dialog.parentWidget()->screen();
    if (dialog.screen()) return dialog.screen();
    return QGuiApplication::primaryScreen();
}

void restoreEditorDialogGeometry(QDialog& dialog, const QString& settingsKey,
                                 const QSize& preferred, const QSize& minimum)
{
    QScreen* screen = screenForDialog(dialog);
    if (!screen) {
        dialog.resize(preferred.expandedTo(minimum));
        return;
    }
    const QRect available = screen->availableGeometry();
    const QSize safeMin(qMin(minimum.width(), available.width()),
                        qMin(minimum.height(), available.height()));
    dialog.setMinimumSize(qMax(1, safeMin.width()), qMax(1, safeMin.height()));

    const QRect stored = QSettings().value(settingsKey + QStringLiteral("/rect")).toRect();
    QRect requested = stored;
    if (!requested.isValid() || requested.isEmpty()) {
        const QSize size(qMin(qMax(preferred.width(), safeMin.width()), available.width()),
                         qMin(qMax(preferred.height(), safeMin.height()), available.height()));
        requested = QRect(QPoint(0, 0), size);
        requested.moveCenter(available.center());
    }
    dialog.setGeometry(fitEditorDialogRect(available, requested, safeMin));
}

void saveEditorDialogGeometry(const QDialog& dialog, const QString& settingsKey)
{
    QRect rect = dialog.isMaximized() ? dialog.normalGeometry() : dialog.geometry();
    if (!rect.isValid() || rect.isEmpty()) return;
    QSettings().setValue(settingsKey + QStringLiteral("/rect"), rect);
}

} // namespace ui
