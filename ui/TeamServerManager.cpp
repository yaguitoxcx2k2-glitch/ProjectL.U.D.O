#include "TeamServerManager.h"
#include "CollaborationClient.h"

#include <QAbstractButton>
#include <QAction>
#include <QAbstractItemView>
#include <QAbstractSocket>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QHostAddress>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidgetItem>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTcpSocket>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <functional>

namespace ui {
namespace {

bool isTailscaleV4(const QHostAddress& address) {
    if (address.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 ip = address.toIPv4Address();
    return (ip & 0xffc00000u) == 0x64400000u; // 100.64.0.0/10
}

QString roleLabel(const QString& role) {
    if (role == QStringLiteral("admin")) return QObject::tr("Administrador");
    if (role == QStringLiteral("viewer")) return QObject::tr("Visualização");
    return QObject::tr("Editor");
}

QString roleValue(const QString& label) {
    if (label == QObject::tr("Administrador")) return QStringLiteral("admin");
    if (label == QObject::tr("Visualização")) return QStringLiteral("viewer");
    return QStringLiteral("editor");
}

} // namespace

TeamServerManager::TeamServerManager(CollaborationClient* client, QWidget* parent)
    : QObject(parent), collaboration(client), window(parent), process(this) {
    QSettings settings;
    dataFolder = settings.value(QStringLiteral("collaboration/hostDataFolder"), defaultDataFolder()).toString();
    serverName = settings.value(QStringLiteral("collaboration/hostServerName"), tr("Minha equipe LUDO")).toString();
    port = settings.value(QStringLiteral("collaboration/hostPort"), 8787).toInt();
    allowNetwork = settings.value(QStringLiteral("collaboration/hostAllowNetwork"), true).toBool();
    detachedByEditor = settings.value(QStringLiteral("collaboration/hostDetached"), false).toBool();
    process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        lastOutput += QString::fromUtf8(process.readAllStandardOutput());
        if (lastOutput.size() > 12000) lastOutput = lastOutput.right(12000);
    });
}

bool TeamServerManager::isRunning() {
    if (process.state() != QProcess::NotRunning) return true;
    if (!detachedByEditor) return false;
    if (localServerResponding()) return true;
    detachedByEditor = false;
    QSettings().setValue(QStringLiteral("collaboration/hostDetached"), false);
    return false;
}

TeamServerManager::PythonCommand TeamServerManager::pythonCommand() const {
    PythonCommand result;
#ifdef Q_OS_WIN
    result.program = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (!result.program.isEmpty()) {
        result.prefix = {QStringLiteral("-3"), QStringLiteral("-u")};
        return result;
    }
#endif
    result.program = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (result.program.isEmpty()) result.program = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (!result.program.isEmpty()) result.prefix = {QStringLiteral("-u")};
    return result;
}

QString TeamServerManager::serverScriptPath() const {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(app).filePath(QStringLiteral("server/ludo_server.py")),
        QDir(app).filePath(QStringLiteral("../server/ludo_server.py")),
        QDir(app).filePath(QStringLiteral("../../server/ludo_server.py")),
        QDir::current().filePath(QStringLiteral("server/ludo_server.py"))
    };
    for (const auto& candidate : candidates) {
        const QFileInfo file(candidate);
        if (file.isFile()) return file.absoluteFilePath();
    }
    return {};
}

QString TeamServerManager::defaultDataFolder() const {
    return QDir::home().filePath(QStringLiteral("LudoServidor"));
}

QString TeamServerManager::tailscaleAddress() const {
    // First prefer the address reported by Tailscale itself when its CLI is on PATH.
    QString executable = QStandardPaths::findExecutable(QStringLiteral("tailscale"));
#ifdef Q_OS_WIN
    if (executable.isEmpty()) {
        const auto env = QProcessEnvironment::systemEnvironment();
        const QString programFiles = env.value(QStringLiteral("ProgramFiles"));
        const QString candidate = QDir(programFiles).filePath(QStringLiteral("Tailscale/tailscale.exe"));
        if (QFileInfo(candidate).isExecutable()) executable = candidate;
    }
#endif
    if (!executable.isEmpty()) {
        QProcess probe;
        probe.start(executable, {QStringLiteral("ip"), QStringLiteral("-4")});
        if (probe.waitForFinished(1600) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            const auto lines = QString::fromUtf8(probe.readAllStandardOutput()).split(QRegularExpression(QStringLiteral("[\\r\\n\\s]+")), Qt::SkipEmptyParts);
            for (const auto& line : lines) {
                QHostAddress address;
                if (address.setAddress(line.trimmed()) && isTailscaleV4(address)) return address.toString();
            }
        }
    }
    // Fallback keeps the feature working even when tailscale.exe is not on PATH.
    for (const auto& address : QNetworkInterface::allAddresses()) {
        if (isTailscaleV4(address)) return address.toString();
    }
    return {};
}

QString TeamServerManager::localAddress() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

QString TeamServerManager::sharedAddress() const {
    const QString tailscale = tailscaleAddress();
    if (allowNetwork && !tailscale.isEmpty()) return QStringLiteral("http://%1:%2").arg(tailscale).arg(port);
    return localAddress();
}

QString TeamServerManager::processErrorText() const {
    const QString text = lastOutput.trimmed();
    if (!text.isEmpty()) return text.right(3500);
    return process.errorString();
}

QString TeamServerManager::stopSignalPath() const {
    return QDir(dataFolder).filePath(QStringLiteral(".ludo-team-server-%1.stop").arg(port));
}

int TeamServerManager::localServerTeamProtocol() const {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, quint16(port));
    if (!socket.waitForConnected(500)) return 0;
    const QByteArray request = "GET /v1/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(500)) return 0;
    QByteArray response;
    while (socket.waitForReadyRead(600)) {
        response += socket.readAll();
        if (socket.state() == QAbstractSocket::UnconnectedState) break;
    }
    response += socket.readAll();
    const bool ok = response.startsWith("HTTP/1.0 200") || response.startsWith("HTTP/1.1 200");
    if (!ok || !response.contains("\"serverName\"")) return 0;
    for (int protocol = 9; protocol >= 1; --protocol) {
        const QByteArray marker = QByteArray("\"teamProtocol\":") + QByteArray::number(protocol);
        if (response.contains(marker)) return protocol;
    }
    return 0;
}

bool TeamServerManager::localServerResponding() const {
    return localServerTeamProtocol() == 6;
}

bool TeamServerManager::requestGracefulStop() {
    QDir().mkpath(dataFolder);
    QFile signal(stopSignalPath());
    if (!signal.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (signal.write("stop\n") < 0) return false;
    signal.close();
    return true;
}

bool TeamServerManager::runServerTool(const QStringList& arguments, QByteArray* output, const QByteArray& stdinData) {
    const auto python = pythonCommand();
    const QString script = serverScriptPath();
    if (!python.valid()) {
        QMessageBox::warning(window, tr("Servidor da equipe"),
                             tr("O Python 3 não foi encontrado. Instale o Python 3 com o Python Launcher e tente novamente."));
        return false;
    }
    if (script.isEmpty()) {
        QMessageBox::warning(window, tr("Servidor da equipe"),
                             tr("Os arquivos do LUDO Team Server não foram encontrados ao lado do Editor."));
        return false;
    }
    QProcess tool;
    tool.setWorkingDirectory(QFileInfo(script).absolutePath());
    tool.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = python.prefix;
    args << script << QStringLiteral("--data") << dataFolder;
    args << arguments;
    tool.start(python.program, args);
    if (!tool.waitForStarted(4000)) {
        QMessageBox::warning(window, tr("Servidor da equipe"), tool.errorString());
        return false;
    }
    if (!stdinData.isEmpty()) {
        tool.write(stdinData);
        tool.closeWriteChannel();
    }
    if (!tool.waitForFinished(15000)) {
        tool.kill();
        tool.waitForFinished(2000);
        QMessageBox::warning(window, tr("Servidor da equipe"), tr("O utilitário do servidor não respondeu."));
        return false;
    }
    const QByteArray bytes = tool.readAllStandardOutput();
    if (output) *output = bytes;
    if (tool.exitStatus() != QProcess::NormalExit || tool.exitCode() != 0) {
        QMessageBox::warning(window, tr("Servidor da equipe"),
                             QString::fromUtf8(bytes).trimmed().isEmpty() ? tool.errorString() : QString::fromUtf8(bytes).trimmed());
        return false;
    }
    return true;
}

bool TeamServerManager::hasAdministrator() {
    QByteArray output;
    if (!runServerTool({QStringLiteral("--list-users-json")}, &output)) return false;
    const auto doc = QJsonDocument::fromJson(output.trimmed());
    if (!doc.isArray()) return false;
    for (const auto& value : doc.array()) {
        if (value.toObject().value(QStringLiteral("role")).toString() == QStringLiteral("admin")) return true;
    }
    return false;
}

bool TeamServerManager::addPersonDialog() {
    QDialog dialog(window);
    dialog.setWindowTitle(tr("Adicionar pessoa"));
    QFormLayout layout(&dialog);
    QLineEdit name(&dialog), password(&dialog), confirm(&dialog);
    password.setEchoMode(QLineEdit::Password);
    confirm.setEchoMode(QLineEdit::Password);
    QComboBox role(&dialog);
    role.addItems({tr("Administrador"), tr("Editor"), tr("Visualização")});
    layout.addRow(tr("Nome de acesso"), &name);
    layout.addRow(tr("Senha (mínimo 10 caracteres)"), &password);
    layout.addRow(tr("Confirmar senha"), &confirm);
    layout.addRow(tr("Permissão"), &role);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    if (name.text().trimmed().isEmpty()) {
        QMessageBox::warning(window, tr("Adicionar pessoa"), tr("Informe um nome de acesso."));
        return false;
    }
    if (password.text() != confirm.text() || password.text().size() < 10) {
        QMessageBox::warning(window, tr("Adicionar pessoa"), tr("As senhas precisam coincidir e ter pelo menos 10 caracteres."));
        return false;
    }
    QByteArray secret = password.text().toUtf8();
    secret.append('\n');
    const bool ok = runServerTool({QStringLiteral("--add-user"), name.text().trimmed(),
                                   QStringLiteral("--role"), roleValue(role.currentText()),
                                   QStringLiteral("--password-stdin")}, nullptr, secret);
    secret.fill('\0');
    password.clear();
    confirm.clear();
    return ok;
}

bool TeamServerManager::removePersonDialog() {
    QByteArray output;
    if (!runServerTool({QStringLiteral("--list-users-json")}, &output)) return false;
    const auto users = QJsonDocument::fromJson(output.trimmed()).array();
    if (users.isEmpty()) {
        QMessageBox::information(window, tr("Equipe"), tr("Nenhuma pessoa cadastrada."));
        return false;
    }
    QStringList labels;
    for (const auto& value : users) {
        const auto user = value.toObject();
        labels << QStringLiteral("%1 — %2").arg(user.value(QStringLiteral("name")).toString(),
                                                roleLabel(user.value(QStringLiteral("role")).toString()));
    }
    bool accepted = false;
    const QString selected = QInputDialog::getItem(window, tr("Remover pessoa"), tr("Pessoa"), labels, 0, false, &accepted);
    if (!accepted) return false;
    const int index = labels.indexOf(selected);
    if (index < 0) return false;
    const QString name = users.at(index).toObject().value(QStringLiteral("name")).toString();
    if (QMessageBox::question(window, tr("Remover acesso"),
                              tr("Remover o acesso de %1? As sessões e a presença dessa pessoa na equipe serão encerradas.").arg(name)) != QMessageBox::Yes)
        return false;
    return runServerTool({QStringLiteral("--remove-user"), name});
}

void TeamServerManager::managePeople() {
    for (;;) {
        QByteArray output;
        if (!runServerTool({QStringLiteral("--list-users-json")}, &output)) return;
        const auto users = QJsonDocument::fromJson(output.trimmed()).array();

        QDialog dialog(window);
        dialog.setWindowTitle(tr("Pessoas da equipe"));
        QVBoxLayout layout(&dialog);
        auto* list = new QListWidget(&dialog);
        for (const auto& value : users) {
            const auto user = value.toObject();
            list->addItem(QStringLiteral("%1 — %2").arg(user.value(QStringLiteral("name")).toString(),
                                                        roleLabel(user.value(QStringLiteral("role")).toString())));
        }
        if (users.isEmpty()) list->addItem(tr("Nenhuma pessoa cadastrada."));
        layout.addWidget(list);
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton(tr("Adicionar pessoa…"), &dialog);
        auto* remove = new QPushButton(tr("Remover pessoa…"), &dialog);
        auto* close = new QPushButton(tr("Fechar"), &dialog);
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        row->addWidget(close);
        layout.addLayout(row);
        int action = 0;
        connect(add, &QPushButton::clicked, &dialog, [&] { action = 1; dialog.accept(); });
        connect(remove, &QPushButton::clicked, &dialog, [&] { action = 2; dialog.accept(); });
        connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        dialog.exec();
        if (action == 1) { addPersonDialog(); continue; }
        if (action == 2) { removePersonDialog(); continue; }
        return;
    }
}

void TeamServerManager::rememberSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("collaboration/hostDataFolder"), dataFolder);
    settings.setValue(QStringLiteral("collaboration/hostServerName"), serverName);
    settings.setValue(QStringLiteral("collaboration/hostPort"), port);
    settings.setValue(QStringLiteral("collaboration/hostAllowNetwork"), allowNetwork);
}

bool TeamServerManager::startServer() {
    if (isRunning()) return true;

    // Uma instância antiga pode ter sido mantida em segundo plano por uma
    // versão anterior do Editor. Ela ainda exigia “Reservar mapa”. Tentamos
    // encerrá-la pelo mesmo arquivo-sinal antes de iniciar o Protocol 9.
    int existingProtocol = localServerTeamProtocol();
    if (existingProtocol == 9) {
        detachedByEditor = true;
        QSettings().setValue(QStringLiteral("collaboration/hostDetached"), true);
        return true;
    }
    if (existingProtocol > 0 && existingProtocol != 9) {
        requestGracefulStop();
        for (int attempt = 0; attempt < 25 && localServerTeamProtocol() > 0; ++attempt)
            QThread::msleep(100);
        existingProtocol = localServerTeamProtocol();
        if (existingProtocol > 0) {
            QFile::remove(stopSignalPath());
            QMessageBox::warning(window, tr("Servidor antigo ainda ativo"),
                tr("Há um LUDO Team Server antigo usando esta porta. Encerre essa instância e clique em Iniciar servidor novamente. O modo em tempo real não usa mais Reserva de mapa."));
            return false;
        }
    }
    if (!hasAdministrator()) {
        const auto choice = QMessageBox::question(window, tr("Hospedar equipe"),
            tr("Este servidor ainda não possui um administrador. Criar o primeiro acesso agora?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (choice != QMessageBox::Yes || !addPersonDialog()) return false;
        if (!hasAdministrator()) return false;
    }

    const auto python = pythonCommand();
    const QString script = serverScriptPath();
    if (!python.valid() || script.isEmpty()) {
        if (!python.valid())
            QMessageBox::warning(window, tr("Hospedar equipe"), tr("O Python 3 não foi encontrado neste computador."));
        else
            QMessageBox::warning(window, tr("Hospedar equipe"), tr("Os arquivos do servidor não foram encontrados."));
        return false;
    }

    QDir().mkpath(dataFolder);
    rememberSettings();
    lastOutput.clear();
    lastProgram = python.program;
    lastWorkingDirectory = QFileInfo(script).absolutePath();
    lastArguments = python.prefix;
    lastArguments << script
                  << QStringLiteral("--data") << dataFolder
                  << QStringLiteral("--host") << (allowNetwork ? QStringLiteral("0.0.0.0") : QStringLiteral("127.0.0.1"))
                  << QStringLiteral("--port") << QString::number(port)
                  << QStringLiteral("--name") << serverName
                  << QStringLiteral("--stop-file") << stopSignalPath();
    process.setWorkingDirectory(lastWorkingDirectory);
    process.start(lastProgram, lastArguments);
    if (!process.waitForStarted(5000)) {
        QMessageBox::warning(window, tr("Hospedar equipe"), process.errorString());
        return false;
    }
    // The server prints its ready line immediately after binding the socket.
    // Waiting for that output also lets QProcess observe early bind/startup failures.
    process.waitForReadyRead(3000);
    const QByteArray startup = process.readAllStandardOutput();
    if (!startup.isEmpty()) lastOutput += QString::fromUtf8(startup);
    if (process.state() == QProcess::NotRunning) {
        process.waitForFinished(500);
        const QByteArray tail = process.readAllStandardOutput();
        if (!tail.isEmpty()) lastOutput += QString::fromUtf8(tail);
        QMessageBox::warning(window, tr("Não foi possível iniciar o servidor"), processErrorText());
        return false;
    }
    return true;
}

void TeamServerManager::stopServer() {
    const bool ownedProcess = process.state() != QProcess::NotRunning;
    const bool detached = detachedByEditor;
    if (!ownedProcess && !detached) return;

    requestGracefulStop();
    if (ownedProcess && !process.waitForFinished(3500)) {
        process.terminate();
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(1500);
        }
    }
    if (detached && !ownedProcess) {
        for (int attempt = 0; attempt < 12 && localServerResponding(); ++attempt)
            QThread::msleep(100);
    }
    detachedByEditor = false;
    QSettings().setValue(QStringLiteral("collaboration/hostDetached"), false);
}

bool TeamServerManager::keepServerRunningDetached() {
    if (process.state() == QProcess::NotRunning && detachedByEditor && localServerResponding()) return true;
    if (!isRunning()) return true;
    stopServer();
    qint64 pid = 0;
    QString detachedProgram = lastProgram;
#ifdef Q_OS_WIN
    const QString pyw = QStandardPaths::findExecutable(QStringLiteral("pyw"));
    if (!pyw.isEmpty() && QFileInfo(lastProgram).baseName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0)
        detachedProgram = pyw;
#endif
    if (!QProcess::startDetached(detachedProgram, lastArguments, lastWorkingDirectory, &pid)) {
        QMessageBox::warning(window, tr("Servidor da equipe"),
                             tr("Não foi possível manter o servidor em segundo plano. O Editor continuará aberto."));
        return false;
    }
    detachedByEditor = true;
    QSettings().setValue(QStringLiteral("collaboration/hostDetached"), true);
    return true;
}


void TeamServerManager::showTeamHub() {
    QDialog dialog(window);
    dialog.setWindowTitle(tr("Equipe"));
    dialog.resize(780, 590);
    QVBoxLayout root(&dialog);

    auto* stack=new QStackedWidget(&dialog);
    root.addWidget(stack,1);

    // ------------------------------------------------------------ servidores
    auto* serversPage=new QWidget(stack);
    QVBoxLayout serversLayout(serversPage);
    auto* serversTitle=new QLabel(tr("Equipe"),serversPage);
    QFont titleFont=serversTitle->font();titleFont.setPointSize(titleFont.pointSize()+5);titleFont.setBold(true);serversTitle->setFont(titleFont);
    serversLayout.addWidget(serversTitle);
    auto* serversHint=new QLabel(tr("Entre em uma equipe disponível. O LUDO encontra automaticamente as equipes acessíveis na sua rede."),serversPage);
    serversHint->setWordWrap(true);serversLayout.addWidget(serversHint);
    auto* serverList=new QListWidget(serversPage);serverList->setSelectionMode(QAbstractItemView::NoSelection);serverList->setSpacing(6);serversLayout.addWidget(serverList,1);
    auto* serverActions=new QHBoxLayout;
    auto* createServer=new QPushButton(tr("+ Criar equipe"),serversPage);
    auto* moreOptions=new QPushButton(tr("Mais opções…"),serversPage);
    auto* advancedMenu=new QMenu(moreOptions);
    auto* addByAddress=advancedMenu->addAction(tr("Entrar com endereço…"));
    auto* advancedServer=advancedMenu->addAction(tr("Configurações avançadas do servidor…"));
    moreOptions->setMenu(advancedMenu);
    auto* closeHub=new QPushButton(tr("Fechar"),serversPage);
    serverActions->addWidget(createServer);serverActions->addStretch();serverActions->addWidget(moreOptions);serverActions->addWidget(closeHub);
    serversLayout.addLayout(serverActions);
    stack->addWidget(serversPage);

    // ------------------------------------------------------------- projetos
    auto* projectsPage=new QWidget(stack);
    QVBoxLayout projectsLayout(projectsPage);
    auto* projectTop=new QHBoxLayout;
    auto* backServers=new QPushButton(tr("‹ Equipes"),projectsPage);
    auto* projectHeader=new QLabel(projectsPage);QFont projectHeaderFont=projectHeader->font();projectHeaderFont.setPointSize(projectHeaderFont.pointSize()+4);projectHeaderFont.setBold(true);projectHeader->setFont(projectHeaderFont);
    auto* leaveServer=new QPushButton(tr("Sair da equipe"),projectsPage);
    projectTop->addWidget(backServers);projectTop->addWidget(projectHeader,1);projectTop->addWidget(leaveServer);projectsLayout.addLayout(projectTop);
    auto* projectHint=new QLabel(tr("Seus projetos desta equipe. Cópias locais são guardadas automaticamente em Documentos/LUDO/Projetos da Equipe."),projectsPage);
    projectHint->setWordWrap(true);projectsLayout.addWidget(projectHint);
    auto* projectList=new QListWidget(projectsPage);projectList->setSelectionMode(QAbstractItemView::NoSelection);projectList->setSpacing(6);projectsLayout.addWidget(projectList,1);
    auto* projectActions=new QHBoxLayout;
    auto* publishCurrent=new QPushButton(tr("+ Adicionar projeto atual"),projectsPage);
    auto* refreshProjects=new QPushButton(tr("Atualizar"),projectsPage);
    auto* closeProjects=new QPushButton(tr("Fechar"),projectsPage);
    projectActions->addWidget(publishCurrent);projectActions->addStretch();projectActions->addWidget(refreshProjects);projectActions->addWidget(closeProjects);projectsLayout.addLayout(projectActions);
    stack->addWidget(projectsPage);

    QHash<QString,QJsonObject> servers;
    bool localHosted=isRunning(); // consulta potencialmente síncrona apenas uma vez ao abrir o Hub
    auto normalizeUrl=[](QString raw){
        QUrl url(raw.trimmed());url.setPath(QString());url.setQuery(QString());url.setFragment(QString());
        QString value=url.toString();
        while(value.endsWith(QLatin1Char('/')))value.chop(1);
        return value;
    };
    auto loadSaved=[&]{
        QSettings settings;const int count=settings.beginReadArray(QStringLiteral("collaboration/savedServers"));
        for(int i=0;i<count;++i){settings.setArrayIndex(i);const QString url=normalizeUrl(settings.value(QStringLiteral("url")).toString());if(url.isEmpty())continue;
            auto item=servers.value(url);item[QStringLiteral("url")]=url;item[QStringLiteral("name")]=settings.value(QStringLiteral("name"),tr("Equipe salva")).toString();item[QStringLiteral("serverId")]=settings.value(QStringLiteral("serverId")).toString();item[QStringLiteral("saved")]=true;
            if(!item.contains(QStringLiteral("available")))item[QStringLiteral("available")]=false;servers[url]=item;}
        settings.endArray();
    };
    auto rememberServer=[&](const QString& rawUrl,const QString& name,const QString& id){
        const QString url=normalizeUrl(rawUrl);if(url.isEmpty())return;
        QSettings settings;QVector<QJsonObject> items;const int count=settings.beginReadArray(QStringLiteral("collaboration/savedServers"));
        bool found=false;for(int i=0;i<count;++i){settings.setArrayIndex(i);QString u=normalizeUrl(settings.value(QStringLiteral("url")).toString());QString n=settings.value(QStringLiteral("name")).toString();QString sid=settings.value(QStringLiteral("serverId")).toString();if(u==url){n=name;if(!id.isEmpty())sid=id;found=true;}if(!u.isEmpty())items.push_back(QJsonObject{{QStringLiteral("url"),u},{QStringLiteral("name"),n},{QStringLiteral("serverId"),sid}});}settings.endArray();
        if(!found)items.push_back(QJsonObject{{QStringLiteral("url"),url},{QStringLiteral("name"),name},{QStringLiteral("serverId"),id}});settings.remove(QStringLiteral("collaboration/savedServers"));settings.beginWriteArray(QStringLiteral("collaboration/savedServers"));
        for(int i=0;i<items.size();++i){settings.setArrayIndex(i);settings.setValue(QStringLiteral("url"),items[i].value(QStringLiteral("url")).toString());settings.setValue(QStringLiteral("name"),items[i].value(QStringLiteral("name")).toString());settings.setValue(QStringLiteral("serverId"),items[i].value(QStringLiteral("serverId")).toString());}settings.endArray();
    };
    loadSaved();

    std::function<void()> rebuildServers;
    std::function<void()> rebuildProjects;
    std::function<void(const QJsonArray&)> renderProjects;
    std::function<void(const QJsonObject&)> enterServer;
    int projectListGeneration=0;

    renderProjects=[&](const QJsonArray& projects){
        projectList->clear();
        if(projects.isEmpty()){
            auto* item=new QListWidgetItem(projectList);item->setSizeHint(QSize(0,86));auto* card=new QFrame(projectList);card->setFrameShape(QFrame::StyledPanel);auto* row=new QHBoxLayout(card);
            auto* text=new QLabel(tr("Nenhum projeto nesta equipe ainda.\nUse “Adicionar projeto atual” para começar."),card);text->setWordWrap(true);row->addWidget(text,1);projectList->setItemWidget(item,card);return;
        }
        for(const auto& value:projects){
            const auto remote=value.toObject();const QString id=remote.value(QStringLiteral("id")).toString();const QString name=remote.value(QStringLiteral("name")).toString(tr("Projeto"));
            const auto local=collaboration->localProjectInfo(id,name,remote);const bool opened=local.value(QStringLiteral("opened")).toBool();
            auto* item=new QListWidgetItem(projectList);item->setSizeHint(QSize(0,96));
            auto* card=new QFrame(projectList);card->setFrameShape(QFrame::StyledPanel);auto* row=new QHBoxLayout(card);auto* info=new QVBoxLayout;
            auto* nameLabel=new QLabel(name,card);QFont f=nameLabel->font();f.setBold(true);nameLabel->setFont(f);info->addWidget(nameLabel);
            QStringList details;const int online=remote.value(QStringLiteral("online")).toInt();
            details<<(online==1?tr("1 pessoa trabalhando agora"):(online>1?tr("%1 pessoas trabalhando agora").arg(online):tr("Ninguém trabalhando agora")));
            const QString state=local.value(QStringLiteral("state")).toString();
            if(opened)details<<(collaboration->hasPending()?tr("Aberto neste Editor · sincronizando alterações"):tr("Aberto neste Editor"));
            else if(state==QStringLiteral("ready"))details<<tr("✓ Neste computador · atualizado");
            else if(state==QStringLiteral("update"))details<<tr("Atualização disponível");
            else if(state==QStringLiteral("local-unlinked"))details<<tr("Existe uma pasta com o mesmo nome; o LUDO manterá cópias separadas");
            else details<<tr("Ainda não está neste computador");
            auto* detailLabel=new QLabel(details.join(QStringLiteral(" · ")),card);detailLabel->setWordWrap(true);info->addWidget(detailLabel);row->addLayout(info,1);
            QString buttonText=opened?tr("Continuar"):(state==QStringLiteral("missing")||state==QStringLiteral("local-unlinked")?tr("Baixar e abrir"):(state==QStringLiteral("update")?tr("Atualizar e abrir"):tr("Abrir")));
            auto* open=new QPushButton(buttonText,card);open->setMinimumWidth(132);row->addWidget(open);
            connect(open,&QPushButton::clicked,&dialog,[&,id,name,remote]{if(collaboration->openRemoteProject(id,name,remote))dialog.accept();});
            projectList->setItemWidget(item,card);
        }
    };

    rebuildProjects=[&]{
        if(!collaboration||!collaboration->connected()){stack->setCurrentWidget(serversPage);return;}
        projectHeader->setText(collaboration->currentServerName());
        publishCurrent->setVisible(collaboration->currentRole()==QStringLiteral("admin"));
        projectList->clear();
        auto* loadingItem=new QListWidgetItem(projectList);loadingItem->setSizeHint(QSize(0,78));auto* loadingCard=new QFrame(projectList);loadingCard->setFrameShape(QFrame::StyledPanel);auto* loadingRow=new QHBoxLayout(loadingCard);auto* loadingText=new QLabel(tr("Atualizando projetos da equipe…"),loadingCard);loadingRow->addWidget(loadingText,1);projectList->setItemWidget(loadingItem,loadingCard);
        const int generation=++projectListGeneration;const QString expectedServer=collaboration->currentServerUrl();
        collaboration->listRemoteProjectsAsync(&dialog,[&,generation,expectedServer](bool ok,const QJsonArray& projects,const QString&){
            if(generation!=projectListGeneration||!collaboration||!collaboration->connected()||collaboration->currentServerUrl()!=expectedServer)return;
            if(!ok){
                projectList->clear();auto* item=new QListWidgetItem(projectList);item->setSizeHint(QSize(0,82));auto* card=new QFrame(projectList);card->setFrameShape(QFrame::StyledPanel);auto* box=new QVBoxLayout(card);auto* title=new QLabel(tr("Não foi possível atualizar os projetos"),card);QFont f=title->font();f.setBold(true);title->setFont(f);box->addWidget(title);auto* hint=new QLabel(tr("Verifique a conexão com a equipe e tente Atualizar novamente."),card);hint->setWordWrap(true);box->addWidget(hint);projectList->setItemWidget(item,card);return;
            }
            renderProjects(projects);
        });
    };

    enterServer=[&](const QJsonObject& item){
        if(!collaboration)return;const QString url=item.value(QStringLiteral("url")).toString();if(url.isEmpty())return;
        if(collaboration->connected()){
            if(normalizeUrl(collaboration->currentServerUrl())==normalizeUrl(url)){stack->setCurrentWidget(projectsPage);rebuildProjects();return;}
            if(QMessageBox::question(&dialog,tr("Trocar de equipe"),tr("Sair da equipe atual e entrar em %1?").arg(item.value(QStringLiteral("name")).toString(tr("outra equipe"))))!=QMessageBox::Yes)return;
            collaboration->disconnectServer();
            if(collaboration->connected())return; // salvar a cópia local pode ter sido cancelado/falhado
        }
        QDialog login(&dialog);login.setWindowTitle(tr("Entrar na equipe"));QVBoxLayout loginRoot(&login);
        auto* loginTitle=new QLabel(item.value(QStringLiteral("name")).toString(tr("Equipe LUDO")),&login);QFont lf=loginTitle->font();lf.setPointSize(lf.pointSize()+3);lf.setBold(true);loginTitle->setFont(lf);loginRoot.addWidget(loginTitle);
        auto* loginHint=new QLabel(tr("Use o acesso que o administrador da equipe criou para você."),&login);loginHint->setWordWrap(true);loginRoot.addWidget(loginHint);
        QFormLayout form;QLineEdit user(QSettings().value(QStringLiteral("collaboration/lastUser")).toString(),&login),password(&login);password.setEchoMode(QLineEdit::Password);form.addRow(tr("Usuário"),&user);form.addRow(tr("Senha"),&password);loginRoot.addLayout(&form);
        QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&login);buttons.button(QDialogButtonBox::Ok)->setText(tr("Entrar"));loginRoot.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&login,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&login,&QDialog::reject);
        if(login.exec()!=QDialog::Accepted)return;
        const bool ok=collaboration->loginToServer(QUrl(url),user.text(),password.text());password.clear();if(!ok)return;
        rememberServer(url,collaboration->currentServerName(),collaboration->currentServerId());stack->setCurrentWidget(projectsPage);rebuildProjects();
    };

    rebuildServers=[&]{
        createServer->setEnabled(!localHosted);
        createServer->setToolTip(localHosted?tr("Já existe uma equipe hospedada neste computador."):QString());
        // Recalcula os estados efêmeros a cada render; assim sair da equipe
        // ou parar o servidor não deixa um card preso em “Conectado/Online”.
        QStringList staleLocal;for(auto it=servers.begin();it!=servers.end();++it){(*it)[QStringLiteral("connected")]=false;if(it->value(QStringLiteral("local")).toBool()){(*it)[QStringLiteral("local")]=false;if(!it->value(QStringLiteral("saved")).toBool())staleLocal<<it.key();}}for(const auto& key:staleLocal)servers.remove(key);
        // Servidor hospedado neste próprio computador sempre aparece, mesmo
        // que o broadcast da rede esteja desativado pelo firewall.
        if(localHosted){
            const QString url=normalizeUrl(localAddress());auto item=servers.value(url);item[QStringLiteral("url")]=url;item[QStringLiteral("name")]=serverName;item[QStringLiteral("available")]=true;item[QStringLiteral("local")]=true;item[QStringLiteral("lastSeen")]=double(QDateTime::currentMSecsSinceEpoch());servers[url]=item;
        }
        if(collaboration&&collaboration->connected()){
            const QString url=normalizeUrl(collaboration->currentServerUrl());auto item=servers.value(url);item[QStringLiteral("url")]=url;item[QStringLiteral("name")]=collaboration->currentServerName();item[QStringLiteral("available")]=true;item[QStringLiteral("connected")]=true;servers[url]=item;
        }
        serverList->clear();
        // O mesmo servidor pode aparecer por LAN, Tailscale e um endereço salvo.
        // Agrupamos pelo serverId e mostramos um único card, preferindo a rota
        // conectada/local/disponível.
        QHash<QString,QJsonObject> merged;
        for(auto it=servers.constBegin();it!=servers.constEnd();++it){
            const auto candidate=it.value();const QString sid=candidate.value(QStringLiteral("serverId")).toString();const QString key=sid.isEmpty()?QStringLiteral("url:")+it.key():QStringLiteral("id:")+sid;
            if(!merged.contains(key)){merged.insert(key,candidate);continue;}
            auto current=merged.value(key);auto priority=[](const QJsonObject& v){return (v.value(QStringLiteral("connected")).toBool()?8:0)+(v.value(QStringLiteral("local")).toBool()?4:0)+(v.value(QStringLiteral("available")).toBool()?2:0)+(v.value(QStringLiteral("saved")).toBool()?1:0);};
            QJsonObject best=priority(candidate)>priority(current)?candidate:current;best[QStringLiteral("saved")]=candidate.value(QStringLiteral("saved")).toBool()||current.value(QStringLiteral("saved")).toBool();
            if(!best.contains(QStringLiteral("online"))&&candidate.contains(QStringLiteral("online")))best[QStringLiteral("online")]=candidate.value(QStringLiteral("online"));
            if(!best.contains(QStringLiteral("projects"))&&candidate.contains(QStringLiteral("projects")))best[QStringLiteral("projects")]=candidate.value(QStringLiteral("projects"));merged[key]=best;
        }
        QVector<QJsonObject> ordered;for(auto it=merged.constBegin();it!=merged.constEnd();++it)ordered.push_back(it.value());
        std::sort(ordered.begin(),ordered.end(),[](const QJsonObject& a,const QJsonObject& b){
            const int pa=(a.value(QStringLiteral("connected")).toBool()?4:0)+(a.value(QStringLiteral("local")).toBool()?2:0)+(a.value(QStringLiteral("available")).toBool()?1:0);
            const int pb=(b.value(QStringLiteral("connected")).toBool()?4:0)+(b.value(QStringLiteral("local")).toBool()?2:0)+(b.value(QStringLiteral("available")).toBool()?1:0);
            if(pa!=pb)return pa>pb;return a.value(QStringLiteral("name")).toString().localeAwareCompare(b.value(QStringLiteral("name")).toString())<0;
        });
        if(ordered.isEmpty()){
            auto* item=new QListWidgetItem(serverList);item->setSizeHint(QSize(0,92));auto* card=new QFrame(serverList);card->setFrameShape(QFrame::StyledPanel);auto* box=new QVBoxLayout(card);auto* title=new QLabel(tr("Nenhuma equipe encontrada na rede"),card);QFont f=title->font();f.setBold(true);title->setFont(f);box->addWidget(title);auto* hint=new QLabel(tr("Você pode criar um servidor neste computador ou entrar com um endereço salvo/VPN."),card);hint->setWordWrap(true);box->addWidget(hint);serverList->setItemWidget(item,card);return;
        }
        for(const auto& data:ordered){
            auto* item=new QListWidgetItem(serverList);item->setSizeHint(QSize(0,96));auto* card=new QFrame(serverList);card->setFrameShape(QFrame::StyledPanel);auto* row=new QHBoxLayout(card);auto* info=new QVBoxLayout;
            auto* nameLabel=new QLabel(data.value(QStringLiteral("name")).toString(tr("Equipe LUDO")),card);QFont f=nameLabel->font();f.setBold(true);nameLabel->setFont(f);info->addWidget(nameLabel);
            QStringList detail;if(data.value(QStringLiteral("connected")).toBool())detail<<tr("● Conectado");else if(data.value(QStringLiteral("available")).toBool())detail<<tr("● Disponível agora");else detail<<(data.value(QStringLiteral("saved")).toBool()?tr("○ Servidor salvo"):tr("○ Não disponível"));
            if(data.value(QStringLiteral("local")).toBool())detail<<tr("Neste computador");
            if(data.contains(QStringLiteral("online"))){const int n=data.value(QStringLiteral("online")).toInt();if(n>0)detail<<(n==1?tr("1 pessoa trabalhando"):tr("%1 pessoas trabalhando").arg(n));}
            if(data.contains(QStringLiteral("projects"))){const int n=data.value(QStringLiteral("projects")).toInt();detail<<(n==1?tr("1 projeto"):tr("%1 projetos").arg(n));}
            auto* detailLabel=new QLabel(detail.join(QStringLiteral(" · ")),card);detailLabel->setWordWrap(true);info->addWidget(detailLabel);row->addLayout(info,1);
            auto* enter=new QPushButton(data.value(QStringLiteral("connected")).toBool()?tr("Projetos"):tr("Entrar"),card);enter->setMinimumWidth(105);row->addWidget(enter);connect(enter,&QPushButton::clicked,&dialog,[&,data]{enterServer(data);});serverList->setItemWidget(item,card);
        }
    };

    // Broadcast passivo: nenhum scan de IP. Servidores Protocol 9 anunciam
    // um pacote pequeno a cada 1,5 s; o Hub só escuta e expira entradas antigas.
    QUdpSocket discovery(&dialog);
    discovery.bind(QHostAddress::AnyIPv4,47877,QUdpSocket::ShareAddress|QUdpSocket::ReuseAddressHint);
    connect(&discovery,&QUdpSocket::readyRead,&dialog,[&]{
        while(discovery.hasPendingDatagrams()){
            QByteArray bytes;bytes.resize(int(discovery.pendingDatagramSize()));QHostAddress sender;quint16 senderPort=0;discovery.readDatagram(bytes.data(),bytes.size(),&sender,&senderPort);Q_UNUSED(senderPort);
            const auto doc=QJsonDocument::fromJson(bytes);if(!doc.isObject())continue;auto info=doc.object();
            if(info.value(QStringLiteral("type")).toString()!=QStringLiteral("ludo-team")||info.value(QStringLiteral("teamProtocol")).toInt()!=9)continue;
            const int httpPort=info.value(QStringLiteral("port")).toInt();if(httpPort<=0)continue;const QString url=normalizeUrl(QStringLiteral("http://%1:%2").arg(sender.toString()).arg(httpPort));
            const bool existed=servers.contains(url);const auto before=servers.value(url);auto item=before;item[QStringLiteral("url")]=url;item[QStringLiteral("name")]=info.value(QStringLiteral("serverName")).toString(tr("Equipe LUDO"));item[QStringLiteral("serverId")]=info.value(QStringLiteral("serverId"));item[QStringLiteral("available")]=true;item[QStringLiteral("lastSeen")]=double(QDateTime::currentMSecsSinceEpoch());item[QStringLiteral("online")]=info.value(QStringLiteral("online"));item[QStringLiteral("projects")]=info.value(QStringLiteral("projects"));
            const bool visibleChanged=!existed||before.value(QStringLiteral("name"))!=item.value(QStringLiteral("name"))||before.value(QStringLiteral("serverId"))!=item.value(QStringLiteral("serverId"))||!before.value(QStringLiteral("available")).toBool()||before.value(QStringLiteral("online"))!=item.value(QStringLiteral("online"))||before.value(QStringLiteral("projects"))!=item.value(QStringLiteral("projects"));servers[url]=item;if(visibleChanged)rebuildServers();
        }
    });
    QTimer prune(&dialog);prune.setInterval(1000);connect(&prune,&QTimer::timeout,&dialog,[&]{
        const qint64 now=QDateTime::currentMSecsSinceEpoch();bool changed=false;QStringList removeKeys;for(auto it=servers.begin();it!=servers.end();++it){if(it->value(QStringLiteral("local")).toBool()||it->value(QStringLiteral("connected")).toBool())continue;const qint64 seen=qint64(it->value(QStringLiteral("lastSeen")).toDouble());if(seen>0&&now-seen>5000){if(it->value(QStringLiteral("saved")).toBool()){if(it->value(QStringLiteral("available")).toBool()){(*it)[QStringLiteral("available")]=false;changed=true;}}else {removeKeys<<it.key();changed=true;}}}for(const auto& key:removeKeys)servers.remove(key);if(changed)rebuildServers();
    });prune.start();

    // Servidores salvos (VPN/Internet) não recebem broadcast de LAN. Um
    // health-check assíncrono e pequeno mantém o card atualizado sem varrer
    // endereços nem bloquear a interface.
    QNetworkAccessManager probeNetwork(&dialog);QSet<QString> probing;
    auto probeSaved=[&]{
        for(auto it=servers.constBegin();it!=servers.constEnd();++it){
            const QString url=it.key();const auto data=it.value();if(!data.value(QStringLiteral("saved")).toBool()||data.value(QStringLiteral("local")).toBool()||probing.contains(url))continue;
            QUrl health(url);health.setPath(QStringLiteral("/v1/health"));QNetworkRequest request(health);request.setTransferTimeout(1800);probing.insert(url);QNetworkReply* reply=probeNetwork.get(request);
            connect(reply,&QNetworkReply::finished,&dialog,[&,reply,url]{
                probing.remove(url);const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto object=QJsonDocument::fromJson(reply->readAll()).object();const auto before=servers.value(url);auto item=before;
                if(status==200&&object.value(QStringLiteral("teamProtocol")).toInt()==9){const QString learnedName=object.value(QStringLiteral("serverName")).toString(item.value(QStringLiteral("name")).toString());const QString learnedId=object.value(QStringLiteral("serverId")).toString();const bool metadataChanged=item.value(QStringLiteral("name")).toString()!=learnedName||item.value(QStringLiteral("serverId")).toString()!=learnedId;item[QStringLiteral("available")]=true;item[QStringLiteral("name")]=learnedName;item[QStringLiteral("serverId")]=learnedId;if(object.contains(QStringLiteral("online")))item[QStringLiteral("online")]=object.value(QStringLiteral("online"));if(object.contains(QStringLiteral("projects")))item[QStringLiteral("projects")]=object.value(QStringLiteral("projects"));if(metadataChanged)rememberServer(url,learnedName,learnedId);}
                else if(qint64(item.value(QStringLiteral("lastSeen")).toDouble())==0)item[QStringLiteral("available")]=false;
                const bool visibleChanged=before.value(QStringLiteral("available"))!=item.value(QStringLiteral("available"))||before.value(QStringLiteral("name"))!=item.value(QStringLiteral("name"))||before.value(QStringLiteral("serverId"))!=item.value(QStringLiteral("serverId"))||before.value(QStringLiteral("online"))!=item.value(QStringLiteral("online"))||before.value(QStringLiteral("projects"))!=item.value(QStringLiteral("projects"));servers[url]=item;reply->deleteLater();if(visibleChanged)rebuildServers();
            });
        }
    };
    QTimer probeTimer(&dialog);probeTimer.setInterval(4000);connect(&probeTimer,&QTimer::timeout,&dialog,probeSaved);probeTimer.start();QTimer::singleShot(0,&dialog,probeSaved);

    connect(createServer,&QPushButton::clicked,&dialog,[&]{
        if(localHosted)return;
        const bool needsAdministrator=!hasAdministrator();
        QDialog create(&dialog);create.setWindowTitle(tr("Criar equipe"));QVBoxLayout box(&create);auto* title=new QLabel(tr("Criar uma equipe"),&create);QFont f=title->font();f.setPointSize(f.pointSize()+3);f.setBold(true);title->setFont(f);box.addWidget(title);auto* hint=new QLabel(tr("O LUDO configura a parte técnica para você. Escolha apenas o nome da equipe%1.").arg(needsAdministrator?tr(" e crie seu acesso de administrador"):QString()),&create);hint->setWordWrap(true);box.addWidget(hint);
        QFormLayout form;QLineEdit name(serverName,&create);QCheckBox network(tr("Disponível para outros computadores da minha rede"),&create);network.setChecked(true);form.addRow(tr("Nome da equipe"),&name);form.addRow(QString(),&network);
        QLineEdit owner(QSettings().value(QStringLiteral("collaboration/lastUser")).toString(),&create),password(&create),confirm(&create);password.setEchoMode(QLineEdit::Password);confirm.setEchoMode(QLineEdit::Password);
        if(needsAdministrator){form.addRow(tr("Seu nome de acesso"),&owner);form.addRow(tr("Sua senha (mínimo 10 caracteres)"),&password);form.addRow(tr("Confirmar senha"),&confirm);}
        box.addLayout(&form);QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&create);buttons.button(QDialogButtonBox::Ok)->setText(tr("Criar equipe"));box.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&create,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&create,&QDialog::reject);
        if(create.exec()!=QDialog::Accepted)return;
        const QString ownerName=owner.text().trimmed();QString ownerPassword=password.text();
        if(needsAdministrator){
            if(ownerName.isEmpty()){QMessageBox::warning(&create,tr("Criar equipe"),tr("Informe seu nome de acesso."));ownerPassword.clear();return;}
            if(ownerPassword.size()<10||ownerPassword!=confirm.text()){QMessageBox::warning(&create,tr("Criar equipe"),tr("As senhas precisam coincidir e ter pelo menos 10 caracteres."));ownerPassword.clear();password.clear();confirm.clear();return;}
            QByteArray secret=ownerPassword.toUtf8();secret.append('\n');const bool created=runServerTool({QStringLiteral("--add-user"),ownerName,QStringLiteral("--role"),QStringLiteral("admin"),QStringLiteral("--password-stdin")},nullptr,secret);secret.fill('\0');
            if(!created){ownerPassword.clear();password.clear();confirm.clear();return;}
            QSettings().setValue(QStringLiteral("collaboration/lastUser"),ownerName);
        }
        serverName=name.text().trimmed().isEmpty()?tr("Minha equipe LUDO"):name.text().trimmed();allowNetwork=network.isChecked();
        if(startServer()){
            localHosted=true;rememberSettings();rebuildServers();
            // Na primeira criação já temos a senha em memória: entramos na equipe
            // automaticamente para que o usuário vá direto aos projetos.
            if(needsAdministrator&&collaboration&&!collaboration->connected()&&collaboration->loginToServer(QUrl(localAddress()),ownerName,ownerPassword)){
                rememberServer(collaboration->currentServerUrl(),collaboration->currentServerName(),collaboration->currentServerId());stack->setCurrentWidget(projectsPage);rebuildProjects();
            }
        }
        ownerPassword.clear();password.clear();confirm.clear();
    });
    connect(addByAddress,&QAction::triggered,&dialog,[&]{if(collaboration){collaboration->connectToServerDialog(QString());if(collaboration->connected()){rememberServer(collaboration->currentServerUrl(),collaboration->currentServerName(),collaboration->currentServerId());stack->setCurrentWidget(projectsPage);rebuildProjects();}}});
    connect(advancedServer,&QAction::triggered,&dialog,[&]{showHostDialog();localHosted=isRunning();rebuildServers();});
    connect(closeHub,&QPushButton::clicked,&dialog,&QDialog::accept);connect(closeProjects,&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(backServers,&QPushButton::clicked,&dialog,[&]{stack->setCurrentWidget(serversPage);rebuildServers();});
    connect(leaveServer,&QPushButton::clicked,&dialog,[&]{if(collaboration)collaboration->disconnectServer();stack->setCurrentWidget(serversPage);rebuildServers();});
    connect(refreshProjects,&QPushButton::clicked,&dialog,[&]{rebuildProjects();});
    connect(publishCurrent,&QPushButton::clicked,&dialog,[&]{if(!collaboration)return;collaboration->publish();if(collaboration->attached())rebuildProjects();});

    rebuildServers();
    if(collaboration&&collaboration->connected()){stack->setCurrentWidget(projectsPage);rebuildProjects();}
    dialog.exec();
}

void TeamServerManager::showHostDialog() {
    QDialog dialog(window);
    dialog.setWindowTitle(tr("Hospedar equipe"));
    dialog.resize(620, 360);
    QVBoxLayout root(&dialog);

    auto* title = new QLabel(tr("Servidor da equipe"), &dialog);
    QFont titleFont = title->font(); titleFont.setPointSize(titleFont.pointSize() + 4); titleFont.setBold(true); title->setFont(titleFont);
    root.addWidget(title);
    auto* explanation = new QLabel(tr("O LUDO inicia e controla o servidor automaticamente. Você não precisa abrir a pasta server nem executar arquivos .bat."), &dialog);
    explanation->setWordWrap(true); root.addWidget(explanation);

    QFormLayout form;
    auto* nameEdit = new QLineEdit(serverName, &dialog);
    auto* folderEdit = new QLineEdit(dataFolder, &dialog);
    auto* chooseFolder = new QPushButton(tr("Escolher…"), &dialog);
    auto* folderRow = new QHBoxLayout; folderRow->addWidget(folderEdit, 1); folderRow->addWidget(chooseFolder);
    auto* portSpin = new QSpinBox(&dialog); portSpin->setRange(1024, 65535); portSpin->setValue(port);
    auto* networkBox = new QCheckBox(tr("Permitir acesso pela rede local / Tailscale"), &dialog); networkBox->setChecked(allowNetwork);
    form.addRow(tr("Nome da equipe"), nameEdit);
    form.addRow(tr("Dados e histórico"), folderRow);
    form.addRow(tr("Porta"), portSpin);
    form.addRow(QString(), networkBox);
    root.addLayout(&form);

    auto* status = new QLabel(&dialog); status->setWordWrap(true); root.addWidget(status);
    auto* address = new QLineEdit(&dialog); address->setReadOnly(true); root.addWidget(address);

    auto* actions = new QHBoxLayout;
    auto* people = new QPushButton(tr("Pessoas…"), &dialog);
    auto* toggle = new QPushButton(&dialog);
    auto* connectHere = new QPushButton(tr("Conectar neste servidor…"), &dialog);
    auto* copy = new QPushButton(tr("Copiar endereço"), &dialog);
    auto* close = new QPushButton(tr("Fechar"), &dialog);
    actions->addWidget(people); actions->addWidget(toggle); actions->addStretch(); actions->addWidget(connectHere); actions->addWidget(copy); actions->addWidget(close);
    root.addLayout(actions);

    const auto refresh = [&] {
        const bool running = isRunning();
        const QString tailscale = tailscaleAddress();
        toggle->setText(running ? tr("Parar servidor") : tr("Iniciar servidor"));
        status->setText(running
            ? (tailscale.isEmpty()
                ? tr("● Servidor online · Tailscale não detectado. O acesso local continua disponível.")
                : tr("● Servidor online · Tailscale detectado: %1").arg(tailscale))
            : (tailscale.isEmpty()
                ? tr("○ Servidor parado · Tailscale não detectado")
                : tr("○ Servidor parado · Tailscale detectado: %1").arg(tailscale)));
        address->setText(running ? sharedAddress() : QString());
        connectHere->setEnabled(running);
        copy->setEnabled(running);
        nameEdit->setEnabled(!running);
        folderEdit->setEnabled(!running);
        chooseFolder->setEnabled(!running);
        portSpin->setEnabled(!running);
        networkBox->setEnabled(!running);
    };

    connect(chooseFolder, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getExistingDirectory(&dialog, tr("Pasta do servidor"), folderEdit->text());
        if (!selected.isEmpty()) folderEdit->setText(selected);
    });
    connect(people, &QPushButton::clicked, &dialog, [&] {
        dataFolder = folderEdit->text().trimmed();
        if (dataFolder.isEmpty()) dataFolder = defaultDataFolder();
        managePeople();
        refresh();
    });
    connect(toggle, &QPushButton::clicked, &dialog, [&] {
        if (isRunning()) {
            stopServer();
        } else {
            dataFolder = folderEdit->text().trimmed();
            serverName = nameEdit->text().trimmed();
            if (serverName.isEmpty()) serverName = tr("Minha equipe LUDO");
            port = portSpin->value();
            allowNetwork = networkBox->isChecked();
            startServer();
        }
        refresh();
    });
    connect(copy, &QPushButton::clicked, &dialog, [&] {
        QGuiApplication::clipboard()->setText(address->text());
    });
    connect(connectHere, &QPushButton::clicked, &dialog, [&] {
        if (collaboration) collaboration->connectToServerDialog(localAddress());
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    refresh();
    dialog.exec();
}

bool TeamServerManager::prepareForEditorClose() {
    if (!isRunning()) return true;
    QMessageBox box(window);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Servidor da equipe ativo"));
    box.setText(tr("Existe uma equipe hospedada neste computador."));
    box.setInformativeText(tr("Você pode encerrar a hospedagem junto com o LUDO ou manter o servidor funcionando em segundo plano."));
    auto* stop = box.addButton(tr("Encerrar servidor"), QMessageBox::AcceptRole);
    auto* keep = box.addButton(tr("Manter funcionando"), QMessageBox::ActionRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(qobject_cast<QPushButton*>(stop));
    box.exec();
    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == keep) return keepServerRunningDetached();
    stopServer();
    return true;
}

} // namespace ui
