#include "PlaytestPreparationDialog.h"

#include "core/Editor.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QProgressDialog>

namespace ui {

PlaytestPreparationResult preparePlaytestRuntime(core::Editor& source,
                                                 const game::RuntimePreloadOptions& options,
                                                 QWidget* parent)
{
    PlaytestPreparationResult result;
    QProgressDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Preparando playtest"));
    dialog.setLabelText(QObject::tr("Preparando o jogo para teste…"));
    dialog.setCancelButtonText(QObject::tr("Cancelar"));
    dialog.setRange(0, 0); // snapshot: trabalho real ainda não foi planejado
    dialog.setMinimumDuration(0);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    dialog.show();

    const auto callback = [&](const game::RuntimePreloadProgress& p) -> bool {
        if (p.totalWork <= 0) {
            dialog.setRange(0, 0);
        } else {
            if (dialog.maximum() == 0) dialog.setRange(0, 1000);
            const int value = int(qBound<qint64>(qint64(0), (p.completedWork * qint64(1000)) / qMax<qint64>(qint64(1), p.totalWork), qint64(1000)));
            dialog.setValue(value);
        }
        QString label = p.stage;
        if (!p.detail.trimmed().isEmpty()) label += QStringLiteral("\n") + p.detail;
        if (p.totalItems > 0)
            label += QObject::tr("\n%1 de %2 etapas").arg(qBound(0, p.completedItems, p.totalItems)).arg(p.totalItems);
        dialog.setLabelText(label);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
        return !dialog.wasCanceled();
    };

    result.runtime = game::prepareRuntimeProject(source, options, &result.report,
                                                 &result.error, callback);
    result.canceled = !result.runtime && result.error.isEmpty() && dialog.wasCanceled();
    if (result.runtime) {
        dialog.setRange(0, 1000);
        dialog.setValue(1000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    dialog.close();
    return result;
}

} // namespace ui
