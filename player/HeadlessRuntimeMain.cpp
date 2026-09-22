#include "core/Editor.h"
#include "core/ProjectIO.h"
#include "game/RuntimeScenario.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") ? QByteArray("offscreen") : qgetenv("QT_QPA_PLATFORM"));
    QGuiApplication app(argc, argv);
    QTextStream err(stderr), out(stdout);
    if (argc < 3) { err << "Usage: LudoRuntimeHeadless <project.ludo> <scenario.json>\n"; return 2; }
    core::Editor project; QString error;
    if (!core::io::loadProject(project, QString::fromLocal8Bit(argv[1]), &error)) { err << error << '\n'; return 3; }
    QFile f(QString::fromLocal8Bit(argv[2])); if (!f.open(QIODevice::ReadOnly)) { err << f.errorString() << '\n'; return 4; }
    QJsonParseError parseError; const auto doc=QJsonDocument::fromJson(f.readAll(),&parseError); if(parseError.error!=QJsonParseError::NoError||!doc.isObject()){err<<parseError.errorString()<<'\n';return 5;}
    game::RuntimeScenarioDefinition scenario; if(!game::RuntimeScenarioDefinition::parse(doc.object(),&scenario,&error)){err<<error<<'\n';return 6;}
    QString hash; if(!game::RuntimeScenarioRunner::run(project,scenario,&error,&hash)){err<<error<<'\n';return 7;}
    out << "PASS stateHash=" << hash << '\n'; return 0;
}
