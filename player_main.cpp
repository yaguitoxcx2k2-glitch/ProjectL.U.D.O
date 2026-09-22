// ============================================================================
//  player_main.cpp — Player standalone: abre um projeto sem carregar o editor.
// ============================================================================
#include "core/Editor.h"
#include "core/ProjectIO.h"
#include "core/SecureAssetPackage.h"
#include "core/Version.h"
#include "game/RuntimeGpuPolicy.h"
#include "player/PlayerWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QLibraryInfo>
#include <QTranslator>
#include <QTemporaryDir>

#include <memory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTranslator qtTranslator;
    const QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qtbase_pt_BR"), translationsPath) ||
        qtTranslator.load(QStringLiteral("qt_pt_BR"), translationsPath) ||
        qtTranslator.load(QStringLiteral("qtbase_pt"), translationsPath)) {
        app.installTranslator(&qtTranslator);
    }
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("Ludo Player"));
    QApplication::setApplicationVersion(QString::fromLatin1(core::version::Engine));
    QApplication::setOrganizationName(QStringLiteral("LudoEngine"));
    QApplication::setOrganizationDomain(QStringLiteral("ludoengine.local"));

    QFile qss(QStringLiteral(":/resources/style.qss"));
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Ludo Player — runtime standalone do Ludo Engine."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("projeto"),
                                 QStringLiteral("Arquivo .ludo. Sem argumento, procura game.ludo ao lado do executável."),
                                 QStringLiteral("[projeto.ludo]"));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    const QString projectPath = positional.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("game.ludo"))
        : QFileInfo(positional.first()).absoluteFilePath();
    if (!QFileInfo::exists(projectPath)) {
        QMessageBox::critical(nullptr, QObject::tr("Ludo Player"),
                              QObject::tr("Projeto não encontrado:\n%1\n\n"
                                          "Passe um arquivo .ludo na linha de comando ou coloque "
                                          "game.ludo ao lado do LudoPlayer.").arg(projectPath));
        return 2;
    }

    QString runtimeProjectPath = projectPath;
    std::unique_ptr<QTemporaryDir> secureRuntimeDir;
    const QString assetPackagePath = QDir(QFileInfo(projectPath).absolutePath())
                                         .filePath(QStringLiteral("game.assets"));
    if (QFileInfo::exists(assetPackagePath)) {
        secureRuntimeDir = std::make_unique<QTemporaryDir>(
            QDir(QDir::tempPath()).filePath(QStringLiteral("LudoPlayer-XXXXXX")));
        if (!secureRuntimeDir->isValid()) {
            QMessageBox::critical(nullptr, QObject::tr("Ludo Player"),
                                  QObject::tr("Não foi possível preparar os Assets protegidos do jogo."));
            return 3;
        }
        QString packageError;
        if (!core::secure_assets::extract(assetPackagePath, secureRuntimeDir->path(), &packageError)) {
            QMessageBox::critical(nullptr, QObject::tr("Ludo Player"), packageError);
            return 3;
        }
        runtimeProjectPath = QDir(secureRuntimeDir->path()).filePath(QStringLiteral("game.ludo"));
        if (!QFile::copy(projectPath, runtimeProjectPath)) {
            QMessageBox::critical(nullptr, QObject::tr("Ludo Player"),
                                  QObject::tr("Não foi possível preparar game.ludo para execução protegida."));
            return 3;
        }
    }

    // RC2.45.1: keep the player on the canonical .ludo loading path for this branch.
    // This source is intentionally touched so incremental builds discard stale RC2.44
    // objects that referenced the separate RuntimeDistribution experiment.
    core::Editor project;
    QString error;
    if (!core::io::loadProject(project, runtimeProjectPath, &error)) {
        QMessageBox::critical(nullptr, QObject::tr("Ludo Player"), error);
        return 3;
    }

    // Cada jogo exportado ganha preferencias proprias. Antes todos os jogos
    // compartilhavam "Ludo Player" no registro do Windows; um jogo em
    // nearest podia sobrescrever o bilinear de outro (e o editor usava ainda
    // outro namespace).
    QString settingsId=project.projectId.trimmed();
    for(qsizetype i=0;i<settingsId.size();++i){const QChar ch=settingsId.at(i);if(!ch.isLetterOrNumber()&&ch!=QLatin1Char('-')&&ch!=QLatin1Char('_'))settingsId[i]=QLatin1Char('_');}
    if(settingsId.isEmpty())settingsId=QStringLiteral("default");
    QCoreApplication::setApplicationName(QStringLiteral("LudoGame-%1").arg(settingsId.left(96)));

    const QString runtimeConfig=QDir(QFileInfo(projectPath).absolutePath()).filePath(QStringLiteral("ludo-runtime.ini"));
    if(QFileInfo::exists(runtimeConfig)){
        QSettings defaults(runtimeConfig,QSettings::IniFormat);
        QSettings playerSettings;
        if(!playerSettings.contains(QStringLiteral("game/gpuBackend")))
            playerSettings.setValue(QStringLiteral("game/gpuBackend"),defaults.value(QStringLiteral("graphics/gpuBackend"),QStringLiteral("auto")));
        if(!playerSettings.contains(QStringLiteral("game/scaleFilter")))
            playerSettings.setValue(QStringLiteral("game/scaleFilter"),defaults.value(QStringLiteral("graphics/scaleFilter"),QStringLiteral("nearest")));
        playerSettings.sync();
    }
    game::migrateLegacyRuntimeRendererSettings();
    player::PlayerWindow window(project);
    window.show();
    return app.exec();
}
