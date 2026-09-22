// ============================================================================
//  main.cpp — Ponto de entrada do LUDO Map Editor.
// ============================================================================
#include "core/Editor.h"
#include "core/ProjectIO.h"
#include "core/Version.h"
#include "ui/MainWindow.h"
#include "ui/ProjectManagerDialog.h"
#include "ui/ProjectOpenWorkflow.h"
#include "ui/EditorUiPreferences.h"
#include "ui/EditorTheme.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QTranslator>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QMutex>
#include <QPainter>
#include <QSplashScreen>

namespace {
QMutex g_logMutex;
void ludoMessageHandler(QtMsgType type,const QMessageLogContext& ctx,const QString& msg)
{
    QMutexLocker locker(&g_logMutex);
    const QString dirPath=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/logs");QDir().mkpath(dirPath);
    QFile f(QDir(dirPath).filePath(QStringLiteral("LudoMapEditor-%1.log").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")))));
    if(f.open(QIODevice::Append|QIODevice::Text)){QTextStream out(&f);const char* level=type==QtDebugMsg?"DEBUG":type==QtInfoMsg?"INFO":type==QtWarningMsg?"WARN":type==QtCriticalMsg?"CRITICAL":"FATAL";out<<QDateTime::currentDateTime().toString(Qt::ISODate)<<" ["<<level<<"] "<<msg;if(ctx.file)out<<" ("<<ctx.file<<":"<<ctx.line<<")";out<<"\n";}
    if(type==QtFatalMsg)abort();
}
class LudoSplash final : public QSplashScreen
{
public:
    explicit LudoSplash(const QPixmap& p) : QSplashScreen(p) { setFixedSize(p.size()); }
protected:
    void drawContents(QPainter* p) override
    {
        p->setRenderHint(QPainter::Antialiasing,true);
        QFont f=p->font();f.setBold(true);f.setPixelSize(13);p->setFont(f);
        const QString text=QStringLiteral("LUDO Map Editor  %1").arg(QString::fromLatin1(core::version::Editor));
        const QRect box(14,height()-35,p->fontMetrics().horizontalAdvance(text)+22,23);
        p->setPen(QPen(QColor(255,255,255,185),1));p->setBrush(QColor(35,38,70,185));
        p->drawRoundedRect(box,8,8);p->setPen(Qt::white);p->drawText(box,Qt::AlignCenter,text);
    }
};
}

int main(int argc, char** argv)
{
    // Escala é uma preferência do Editor e precisa ser definida antes de a
    // QApplication criar a árvore de widgets. Não altera resolução do jogo.
    const int uiScalePercent = ui::EditorUiPreferences::storedUiScalePercent();
    if (uiScalePercent != 100)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(uiScalePercent / 100.0, 'f', 2));

    QApplication app(argc, argv);

    // Traduz os botões padrão do Qt (Salvar, Descartar, Cancelar, Fechar...).
    // Sem o catálogo qtbase, esses textos aparecem em inglês mesmo quando a
    // interface da LUDO está em português.
    QTranslator qtTranslator;
    const QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qtbase_pt_BR"), translationsPath) ||
        qtTranslator.load(QStringLiteral("qt_pt_BR"), translationsPath) ||
        qtTranslator.load(QStringLiteral("qtbase_pt"), translationsPath)) {
        app.installTranslator(&qtTranslator);
    }
    qInstallMessageHandler(ludoMessageHandler);
    QApplication::setApplicationName(QStringLiteral("Ludo Engine"));
    QApplication::setApplicationDisplayName(QStringLiteral("LUDO Map Editor"));
    QApplication::setApplicationVersion(QString::fromLatin1(core::version::Editor));
    QApplication::setOrganizationName(QStringLiteral("LudoEngine"));
    QApplication::setOrganizationDomain(QStringLiteral("ludomapeditor.local"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/ludo-engine-icon.png")));

    ui::EditorTheme::apply(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("LUDO Map Editor — editor visual de mapas para RPG Maker MV e MZ."));
    parser.addHelpOption();parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("projeto"),QStringLiteral("Arquivo .ludo (ou .json antigo) para abrir."));
    parser.process(app);

    QPixmap splashArt(QStringLiteral(":/resources/splashscreen.png"));
    LudoSplash splash(splashArt);

    // O splash representa apenas trabalho real de abertura/montagem. Não há
    // tempo mínimo artificial: terminou o Project Lifecycle, termina a transição.
    const auto showSplash = [&] {
        splash.show();
        splash.raise();
        app.processEvents();
    };

    const QStringList args = parser.positionalArguments();
    QString startupProject;
    bool startupAlreadyLoaded = false;

    if (!args.isEmpty()) {
        startupProject = args.first();
        showSplash();
    }

    if (!startupProject.isEmpty() && !startupAlreadyLoaded) {
        ui::ProjectOpenResult openResult;
        QString error;
        if (!ui::openProjectWorkflow(core::editor(), startupProject, nullptr, &openResult, &error)) {
            if (!error.isEmpty()) QMessageBox::critical(nullptr, QObject::tr("Erro ao abrir projeto"), error);
            return 1;
        }
        ui::ProjectManagerDialog::rememberProject(openResult.projectPath);
    }

    ui::MainWindow window(core::editor());
    window.show();
    if (!splash.isHidden()) splash.finish(&window);
    return app.exec();
}
