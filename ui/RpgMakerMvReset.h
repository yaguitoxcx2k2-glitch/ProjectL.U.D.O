#pragma once

#include "../core/RpgMakerTarget.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QWidget>

namespace ui::rpgMakerMvReset {

inline QString projectFile(const QString& projectRoot)
{
    QDir root(projectRoot);
    const QString preferred = root.filePath(QStringLiteral("Game.rpgproject"));
    if (QFileInfo::exists(preferred)) return preferred;

    const QStringList projects = root.entryList(QStringList{QStringLiteral("*.rpgproject")}, QDir::Files, QDir::Name);
    return projects.isEmpty() ? QString() : root.filePath(projects.constFirst());
}

inline QString rememberedEditorExecutable()
{
    const QString path = QSettings().value(QStringLiteral("rpgmaker/mvExecutable")).toString().trimmed();
    return (!path.isEmpty() && QFileInfo::exists(path)) ? QDir::cleanPath(path) : QString();
}

inline void rememberEditorExecutable(const QString& path)
{
    if (!path.trimmed().isEmpty() && QFileInfo::exists(path)) {
        QSettings().setValue(QStringLiteral("rpgmaker/mvExecutable"), QDir::cleanPath(path));
    }
}

#ifdef Q_OS_WIN
inline int runningEditorCount(QString* error = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("powershell.exe"), {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-Command"),
        QStringLiteral("$p=@(Get-Process -Name RPGMV -ErrorAction SilentlyContinue); [Console]::Out.Write($p.Count)")
    });
    if (!process.waitForStarted(2500) || !process.waitForFinished(3500)) {
        if (error) *error = QObject::tr("Não foi possível consultar o processo do RPG Maker MV.");
        return -1;
    }
    bool ok = false;
    const int count = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed().toInt(&ok);
    if (!ok) {
        if (error) *error = QObject::tr("O Windows retornou uma resposta inesperada ao procurar o RPG Maker MV.");
        return -1;
    }
    return count;
}

inline QString runningEditorExecutable(QString* error = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("powershell.exe"), {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-Command"),
        QStringLiteral("$p=Get-Process -Name RPGMV -ErrorAction SilentlyContinue | Select-Object -First 1; if($p){ [Console]::Out.Write($p.Path) }")
    });
    if (!process.waitForStarted(2500) || !process.waitForFinished(3500)) {
        if (error) *error = QObject::tr("Não foi possível descobrir o caminho do RPGMV.exe em execução.");
        return QString();
    }
    const QString path = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        const QString clean = QDir::cleanPath(path);
        rememberEditorExecutable(clean);
        return clean;
    }
    if (error) *error = QObject::tr("O processo do RPG Maker MV não informou um caminho válido para o RPGMV.exe.");
    return QString();
}

inline QString findEditorExecutable()
{
    // 1) Se o MV estiver aberto, reutiliza exatamente o executável dessa instância.
    const QString running = runningEditorExecutable();
    if (!running.isEmpty()) return running;

    // 2) Reutiliza o último RPGMV.exe já conhecido pelo LUDO.
    const QString remembered = rememberedEditorExecutable();
    if (!remembered.isEmpty()) return remembered;

    // 3) Tenta localizar pelo PATH.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("RPGMV.exe"));
    if (!onPath.isEmpty()) {
        rememberEditorExecutable(onPath);
        return QDir::cleanPath(onPath);
    }

    // 4) Fallbacks comuns de instalação. Não dependemos da associação .rpgproject.
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList roots;
    const QString pf86 = env.value(QStringLiteral("ProgramFiles(x86)"));
    const QString pf = env.value(QStringLiteral("ProgramFiles"));
    if (!pf86.isEmpty()) roots << pf86;
    if (!pf.isEmpty() && !roots.contains(pf, Qt::CaseInsensitive)) roots << pf;

    for (const QString& base : roots) {
        const QStringList candidates = {
            QDir(base).filePath(QStringLiteral("Steam/steamapps/common/RPG Maker MV/RPGMV.exe")),
            QDir(base).filePath(QStringLiteral("KADOKAWA/RPGMV/RPGMV.exe")),
            QDir(base).filePath(QStringLiteral("RPG Maker MV/RPGMV.exe"))
        };
        for (const QString& candidate : candidates) {
            if (QFileInfo::exists(candidate)) {
                rememberEditorExecutable(candidate);
                return QDir::cleanPath(candidate);
            }
        }
    }
    return QString();
}

inline bool requestGracefulClose(QString* error = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("powershell.exe"), {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-Command"),
        QStringLiteral("$p=@(Get-Process -Name RPGMV -ErrorAction SilentlyContinue); foreach($x in $p){ [void]$x.CloseMainWindow() }")
    });
    if (!process.waitForStarted(2500) || !process.waitForFinished(3500)) {
        if (error) *error = QObject::tr("Não foi possível solicitar o fechamento do RPG Maker MV.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QObject::tr("O Windows não conseguiu solicitar o fechamento normal do RPG Maker MV.");
        return false;
    }
    return true;
}
#else
inline int runningEditorCount(QString* error = nullptr)
{
    Q_UNUSED(error);
    return 0;
}
inline QString runningEditorExecutable(QString* error = nullptr)
{
    Q_UNUSED(error);
    return QString();
}
inline QString findEditorExecutable()
{
    return rememberedEditorExecutable();
}
inline bool requestGracefulClose(QString* error = nullptr)
{
    if (error) *error = QObject::tr("O reset automático do RPG Maker MV está disponível no Windows.");
    return false;
}
#endif

inline bool waitBrieflyForClose()
{
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1800) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (runningEditorCount() == 0) return true;
        QThread::msleep(80);
    }
    return runningEditorCount() == 0;
#else
    return true;
#endif
}

inline QString chooseEditorExecutable(QWidget* parent)
{
#ifdef Q_OS_WIN
    const QString selected = QFileDialog::getOpenFileName(
        parent,
        QObject::tr("Localizar RPGMV.exe"),
        QString(),
        QObject::tr("RPG Maker MV (RPGMV.exe);;Executáveis (*.exe)"));
    if (!selected.isEmpty() && QFileInfo(selected).fileName().compare(QStringLiteral("RPGMV.exe"), Qt::CaseInsensitive) == 0) {
        rememberEditorExecutable(selected);
        return QDir::cleanPath(selected);
    }
#else
    Q_UNUSED(parent);
#endif
    return QString();
}

inline bool reopenProject(QWidget* parent, const QString& projectRoot, QString executablePath = QString())
{
    const QString file = projectFile(projectRoot);
    if (file.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Resetar RPG Maker MV"),
                             QObject::tr("Não encontrei o arquivo .rpgproject na pasta vinculada:\n%1")
                                 .arg(QDir::toNativeSeparators(projectRoot)));
        return false;
    }

#ifdef Q_OS_WIN
    if (executablePath.trimmed().isEmpty() || !QFileInfo::exists(executablePath)) {
        executablePath = findEditorExecutable();
    }
    if (executablePath.trimmed().isEmpty() || !QFileInfo::exists(executablePath)) {
        const auto answer = QMessageBox::question(
            parent,
            QObject::tr("Localizar RPG Maker MV"),
            QObject::tr(
                "O LUDO não encontrou automaticamente o RPGMV.exe.\n\n"
                "Deseja localizar o executável do RPG Maker MV agora?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (answer == QMessageBox::Yes) executablePath = chooseEditorExecutable(parent);
    }
    if (executablePath.trimmed().isEmpty() || !QFileInfo::exists(executablePath)) {
        QMessageBox::warning(parent, QObject::tr("Resetar RPG Maker MV"),
                             QObject::tr(
                                 "O projeto foi exportado, mas não encontrei o RPGMV.exe para reabrir o editor.\n\n"
                                 "O LUDO não usa a associação do arquivo .rpgproject."));
        return false;
    }

    rememberEditorExecutable(executablePath);

    // IMPORTANTE: abre o RPGMV.exe diretamente. O arquivo de projeto é somente um
    // argumento do executável; o Windows nunca recebe um pedido para "abrir .rpgproject".
    if (!QProcess::startDetached(executablePath, QStringList{file}, projectRoot)) {
        QMessageBox::warning(parent, QObject::tr("Resetar RPG Maker MV"),
                             QObject::tr(
                                 "O projeto foi exportado, mas não consegui iniciar o RPGMV.exe.\n\n"
                                 "Executável:\n%1\n\nProjeto:\n%2")
                                 .arg(QDir::toNativeSeparators(executablePath),
                                      QDir::toNativeSeparators(file)));
        return false;
    }
    return true;
#else
    Q_UNUSED(executablePath);
    QMessageBox::information(parent, QObject::tr("Resetar RPG Maker MV"),
                             QObject::tr("Abra novamente o RPG Maker MV para carregar os arquivos exportados."));
    return false;
#endif
}

inline bool askAndReset(QWidget* parent, core::RpgMakerEngine engine, const QString& projectRoot)
{
    if (engine != core::RpgMakerEngine::MV || projectRoot.trimmed().isEmpty()) return false;

    QString processError;
    const int count = runningEditorCount(&processError);
    QString detail = QObject::tr(
        "Os mapas já foram gravados no projeto MV.\n\n"
        "Resetar o RPG Maker MV agora para carregar as alterações?\n\n"
        "O LUDO fechará o MV normalmente e depois abrirá diretamente o RPGMV.exe. "
        "A associação do arquivo .rpgproject no Windows não será usada. "
        "Se houver alterações não salvas no MV, use a confirmação do próprio RPG Maker antes de continuar.");
    if (count > 1) {
        detail += QObject::tr("\n\nAtenção: há %1 instâncias do RPG Maker MV abertas. O reset solicitará o fechamento de todas elas.").arg(count);
    } else if (count < 0 && !processError.isEmpty()) {
        detail += QObject::tr("\n\nNão foi possível verificar se o MV está aberto: %1").arg(processError);
    }

    QMessageBox question(QMessageBox::Question, QObject::tr("Exportação concluída"), detail,
                         QMessageBox::NoButton, parent);
    auto* resetButton = question.addButton(QObject::tr("Resetar RPG Maker MV"), QMessageBox::AcceptRole);
    question.addButton(QObject::tr("Agora não"), QMessageBox::RejectRole);
    question.setDefaultButton(resetButton);
    question.exec();
    if (question.clickedButton() != resetButton) return false;

#ifdef Q_OS_WIN
    // Captura o caminho REAL do RPGMV.exe ANTES de encerrar o processo.
    QString mvExecutable = runningEditorExecutable();
    if (mvExecutable.isEmpty()) mvExecutable = findEditorExecutable();

    const int running = runningEditorCount();
    if (running < 0) {
        QMessageBox::warning(parent, QObject::tr("Resetar RPG Maker MV"),
                             QObject::tr("Não foi possível verificar com segurança o processo do RPG Maker MV. O projeto foi exportado, mas o reset automático foi cancelado."));
        return false;
    }
    if (running > 0) {
        QString closeError;
        if (!requestGracefulClose(&closeError)) {
            QMessageBox::warning(parent, QObject::tr("Resetar RPG Maker MV"), closeError);
            return false;
        }

        if (!waitBrieflyForClose()) {
            while (runningEditorCount() > 0) {
                QMessageBox pending(QMessageBox::Information,
                                    QObject::tr("Aguardando o RPG Maker MV"),
                                    QObject::tr(
                                        "O RPG Maker MV ainda está aberto. Ele pode estar aguardando você salvar ou confirmar o fechamento.\n\n"
                                        "Conclua o fechamento no próprio MV e depois clique em Continuar. O LUDO não vai forçar o encerramento."),
                                    QMessageBox::NoButton, parent);
                auto* continueButton = pending.addButton(QObject::tr("Continuar"), QMessageBox::AcceptRole);
                auto* cancelButton = pending.addButton(QObject::tr("Cancelar reset"), QMessageBox::RejectRole);
                pending.setDefaultButton(continueButton);
                pending.exec();
                if (pending.clickedButton() == cancelButton) return false;
            }
        }
    }
    return reopenProject(parent, projectRoot, mvExecutable);
#else
    if (count > 0) {
        QMessageBox::information(parent, QObject::tr("Resetar RPG Maker MV"),
                                 QObject::tr("Feche o RPG Maker MV e abra novamente o projeto para carregar os arquivos exportados."));
        return false;
    }
    return reopenProject(parent, projectRoot);
#endif
}

} // namespace ui::rpgMakerMvReset
