#include "GameExporter.h"

#include "ExportPreflight.h"
#include "ExportWizardDialog.h"
#include "ExportPlatformPolicy.h"

#include "core/Editor.h"
#include "core/ProjectIO.h"
#include "core/ProjectValidator.h"
#include "core/ResourceManager.h"
#include "core/SecureAssetPackage.h"
#include "core/ZipWriter.h"
#include "game/RuntimeGpuPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QBuffer>
#include <QLabel>
#include <QHash>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QPushButton>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ui {
namespace {

bool replaceCopy(const QString& from, const QString& to)
{
    if (QFileInfo::exists(to) && !QFile::remove(to)) return false;
    QDir().mkpath(QFileInfo(to).absolutePath());
    return QFile::copy(from, to);
}

bool copyDirectoryContents(const QString& sourceDir, const QString& targetDir)
{
    const QDir source(sourceDir);
    if (!source.exists()) return false;
    if (!QDir().mkpath(targetDir)) return false;

    bool copiedAnything = false;
    for (QDirIterator it(source.absolutePath(), QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString sourceFile = it.next();
        const QString relative = source.relativeFilePath(sourceFile);
        const QString targetFile = QDir(targetDir).filePath(relative);
        if (!replaceCopy(sourceFile, targetFile)) return false;
        copiedAnything = true;
    }
    return copiedAnything;
}

#ifdef Q_OS_WIN
quint16 icoRead16(const QByteArray& data,int offset)
{
    if(offset<0||offset+2>data.size())return 0;
    const auto*p=reinterpret_cast<const uchar*>(data.constData()+offset);
    return quint16(p[0])|(quint16(p[1])<<8);
}

quint32 icoRead32(const QByteArray& data,int offset)
{
    if(offset<0||offset+4>data.size())return 0;
    const auto*p=reinterpret_cast<const uchar*>(data.constData()+offset);
    return quint32(p[0])|(quint32(p[1])<<8)|(quint32(p[2])<<16)|(quint32(p[3])<<24);
}

void icoAppend16(QByteArray& data,quint16 value)
{
    data.append(char(value&0xff));data.append(char((value>>8)&0xff));
}

void icoAppend32(QByteArray& data,quint32 value)
{
    data.append(char(value&0xff));data.append(char((value>>8)&0xff));data.append(char((value>>16)&0xff));data.append(char((value>>24)&0xff));
}

QByteArray iconFileBytes(const QString& source,QString* error)
{
    QFile file(source);
    if(file.open(QIODevice::ReadOnly)){
        const QByteArray raw=file.readAll();
        if(raw.size()>=6&&icoRead16(raw,0)==0&&icoRead16(raw,2)==1&&icoRead16(raw,4)>0)return raw;
    }
    const QImage image(source);
    if(image.isNull()){
        if(error)*error=QObject::tr("A imagem escolhida não pôde ser lida.");
        return {};
    }
    QByteArray converted;QBuffer buffer(&converted);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"ICO")){
        if(error)*error=QObject::tr("O Qt não conseguiu converter a imagem para ICO. Use um arquivo .ico para alterar o ícone do executável.");
        return {};
    }
    return converted;
}

bool applyWindowsExecutableIcon(const QString& executable,const QString& sourceIcon,QString* error)
{
    QString conversionError;const QByteArray ico=iconFileBytes(sourceIcon,&conversionError);
    if(ico.isEmpty()){if(error)*error=conversionError;return false;}
    const int count=qMin<int>(icoRead16(ico,4),32);
    if(count<=0||ico.size()<6+count*16){if(error)*error=QObject::tr("Arquivo ICO inválido.");return false;}

    struct Entry{uchar width=0,height=0,colorCount=0,reserved=0;quint16 planes=1,bitCount=32;quint32 bytes=0,offset=0;quint16 resourceId=0;};
    QVector<Entry> entries;entries.reserve(count);
    for(int i=0;i<count;++i){
        const int pos=6+i*16;Entry e;e.width=uchar(ico.at(pos));e.height=uchar(ico.at(pos+1));e.colorCount=uchar(ico.at(pos+2));e.reserved=uchar(ico.at(pos+3));
        e.planes=icoRead16(ico,pos+4);e.bitCount=icoRead16(ico,pos+6);e.bytes=icoRead32(ico,pos+8);e.offset=icoRead32(ico,pos+12);e.resourceId=quint16(i+1);
        if(e.bytes==0||e.offset>quint32(ico.size())||e.bytes>quint32(ico.size())-e.offset)continue;entries.push_back(e);
    }
    if(entries.isEmpty()){if(error)*error=QObject::tr("O ICO não contém imagens válidas.");return false;}

    HANDLE update=BeginUpdateResourceW(reinterpret_cast<LPCWSTR>(executable.utf16()),FALSE);
    if(!update){if(error)*error=QObject::tr("O Windows não permitiu abrir o executável para atualizar o ícone (erro %1).").arg(GetLastError());return false;}
    bool ok=true;const WORD language=MAKELANGID(LANG_NEUTRAL,SUBLANG_NEUTRAL);
    for(const Entry&e:entries){
        if(!UpdateResourceW(update,RT_ICON,MAKEINTRESOURCEW(e.resourceId),language,
                            const_cast<char*>(ico.constData()+e.offset),DWORD(e.bytes))){ok=false;break;}
    }
    QByteArray group;if(ok){
        icoAppend16(group,0);icoAppend16(group,1);icoAppend16(group,quint16(entries.size()));
        for(const Entry&e:entries){
            group.append(char(e.width));group.append(char(e.height));group.append(char(e.colorCount));group.append(char(e.reserved));
            icoAppend16(group,e.planes);icoAppend16(group,e.bitCount);icoAppend32(group,e.bytes);icoAppend16(group,e.resourceId);
        }
        ok=UpdateResourceW(update,RT_GROUP_ICON,MAKEINTRESOURCEW(1),language,group.data(),DWORD(group.size()));
    }
    const DWORD lastError=ok?ERROR_SUCCESS:GetLastError();
    if(!EndUpdateResourceW(update,ok?FALSE:TRUE))ok=false;
    if(!ok&&error)*error=QObject::tr("Não foi possível gravar o ícone dentro do executável (erro %1).").arg(lastError==ERROR_SUCCESS?GetLastError():lastError);
    return ok;
}

QStringList mingwRuntimeSearchDirs()
{
    QStringList dirs;
    auto add = [&dirs](const QString& path) {
        const QString clean = QDir::cleanPath(path);
        if (!clean.isEmpty() && QDir(clean).exists() && !dirs.contains(clean, Qt::CaseInsensitive))
            dirs.push_back(clean);
    };

    // A pasta dist criada pelo build-windows.bat já deve conter estes DLLs.
    add(QCoreApplication::applicationDirPath());
    add(QLibraryInfo::path(QLibraryInfo::BinariesPath));

    // Instalação oficial típica: C:/Qt/6.8.3/mingw_64/bin e
    // C:/Qt/Tools/mingw1310_64/bin.
    QDir qtRoot(QLibraryInfo::path(QLibraryInfo::BinariesPath));
    if (qtRoot.cdUp() && qtRoot.cdUp() && qtRoot.cdUp()) {
        QDir tools(qtRoot.filePath(QStringLiteral("Tools")));
        const QStringList mingwKits = tools.entryList(QStringList{QStringLiteral("mingw*_64")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& kit : mingwKits)
            add(tools.filePath(kit + QStringLiteral("/bin")));
    }

    const QString path = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    for (const QString& item : path.split(QDir::listSeparator(), Qt::SkipEmptyParts)) add(item);
    return dirs;
}

QStringList copyMinGwRuntime(const QDir& out, const QStringList& searchDirs)
{
    const QStringList runtimeDlls = {
        QStringLiteral("libgcc_s_seh-1.dll"),
        QStringLiteral("libstdc++-6.dll"),
        QStringLiteral("libwinpthread-1.dll")
    };
    QStringList missing;
    for (const QString& dll : runtimeDlls) {
        bool copied = QFileInfo::exists(out.filePath(dll));
        if (!copied) {
            for (const QString& dir : searchDirs) {
                const QString source = QDir(dir).filePath(dll);
                if (QFileInfo::exists(source) && replaceCopy(source, out.filePath(dll))) {
                    copied = true;
                    break;
                }
            }
        }
        if (!copied) missing.push_back(dll);
    }
    return missing;
}

void copyRuntimeDllFamilies(const QDir& out, const QStringList& searchDirs,
                            const QStringList& nameFilters)
{
    QSet<QString> copied;
    for (const QString& dir : searchDirs) {
        const QDir source(dir);
        for (const QString& name : source.entryList(nameFilters, QDir::Files, QDir::Name)) {
            const QString key=name.toLower();
            if(copied.contains(key)||QFileInfo::exists(out.filePath(name)))continue;
            if(replaceCopy(source.filePath(name),out.filePath(name)))copied.insert(key);
        }
    }
}

bool ensureWindowsPlatformPlugin(const QDir& out, QString* copiedFrom = nullptr)
{
    const QString target = out.filePath(QStringLiteral("platforms/qwindows.dll"));
    if (QFileInfo::exists(target)) return true;

    QStringList pluginRoots;
    auto addRoot = [&pluginRoots](const QString& path) {
        const QString clean = QDir::cleanPath(path);
        if (!clean.isEmpty() && QDir(clean).exists() && !pluginRoots.contains(clean, Qt::CaseInsensitive))
            pluginRoots.push_back(clean);
    };

    // Primeiro prefere a pasta dist do editor, que já foi preparada pelo
    // build-windows.bat. Depois tenta a instalação Qt usada para compilar.
    addRoot(QCoreApplication::applicationDirPath());
    addRoot(QLibraryInfo::path(QLibraryInfo::PluginsPath));

    for (const QString& root : pluginRoots) {
        const QString direct = QDir(root).filePath(QStringLiteral("platforms/qwindows.dll"));
        if (QFileInfo::exists(direct) && replaceCopy(direct, target)) {
            if (copiedFrom) *copiedFrom = direct;
            return true;
        }
        // QLibraryInfo::PluginsPath já pode apontar para .../plugins; no caso
        // da pasta do executável, também aceitamos um subdiretório plugins/.
        const QString nested = QDir(root).filePath(QStringLiteral("plugins/platforms/qwindows.dll"));
        if (QFileInfo::exists(nested) && replaceCopy(nested, target)) {
            if (copiedFrom) *copiedFrom = nested;
            return true;
        }
    }
    return false;
}

void copyKnownQtPluginDirectories(const QDir& out)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString qtPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    const QStringList dirs = {
        QStringLiteral("imageformats"),
        QStringLiteral("iconengines"),
        QStringLiteral("multimedia"),
        QStringLiteral("networkinformation"),
        QStringLiteral("styles"),
        QStringLiteral("tls"),
        QStringLiteral("generic")
    };
    for (const QString& name : dirs) {
        const QString target = out.filePath(name);
        const QString fromApp = QDir(appDir).filePath(name);
        if (QDir(fromApp).exists()) {
            copyDirectoryContents(fromApp, target);
            continue;
        }
        const QString fromQt = QDir(qtPlugins).filePath(name);
        if (QDir(fromQt).exists()) copyDirectoryContents(fromQt, target);
    }
}

bool writeQtConf(const QDir& out)
{
    QSaveFile conf(out.filePath(QStringLiteral("qt.conf")));
    if (!conf.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    static const QByteArray data("[Paths]\r\nPrefix=.\r\nPlugins=.\r\n");
    return conf.write(data) == data.size() && conf.commit();
}

int runWinDeploy(const QString& windeploy, const QString& playerPath, const QStringList& runtimeDirs,
                 const std::shared_ptr<std::atomic_bool>& cancelled)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value(QStringLiteral("PATH"));
    for (auto it = runtimeDirs.crbegin(); it != runtimeDirs.crend(); ++it)
        path.prepend(*it + QDir::listSeparator());
    env.insert(QStringLiteral("PATH"), path);
    process.setProcessEnvironment(env);
    process.start(windeploy, {QStringLiteral("--release"),
                              QStringLiteral("--no-translations"),
                              QStringLiteral("--compiler-runtime"),
                              playerPath});
    if (!process.waitForStarted()) return -1;
    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            process.kill();
            process.waitForFinished(3000);
            return -2;
        }
        process.waitForFinished(100);
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}
#endif

struct AssetSyncResult {
    bool ok = false;
    QString error;
    core::AssetDatabase database;
    QVector<core::AssetPathChange> changes;
};

struct ExportPlan {
    QString exportName;
    QString safeName;
    QString parentDir;
    QString finalDir;
    QString tempDir;
    QString sourcePlayer;
    QString playerName;
    QString exportIconPath;
    QString projectRoot;
    QString appDir;
    QString qtBinDir;
    QString qtPluginsDir;
    QString windeploy;
    QStringList runtimeDirs;
    QStringList selectedAssets;
    QByteArray plainProjectBytes;
    QString gpuBackend;
    QString scaleFilter;
    bool secure = false;
    bool portable = false;
    bool cleanupUnused = false;
    int skippedUnusedAssets = 0;
    bool audioRequired = false;
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString finalDir;
    QString playerName;
    QString note;
};

using ExportProgress = std::function<void(int, const QString&)>;

bool exportCancelled(const std::shared_ptr<std::atomic_bool>& cancelled)
{
    return cancelled && cancelled->load(std::memory_order_relaxed);
}

bool replaceDirectoryAtomically(const QString& tempDir, const QString& finalDir, QString* error)
{
    const QFileInfo finalInfo(finalDir);
    const QDir parent(finalInfo.absolutePath());
    const QString finalName = finalInfo.fileName();
    const QString tempName = QFileInfo(tempDir).fileName();
    const QString backupName = QStringLiteral(".%1.previous-%2")
                                   .arg(finalName, QUuid::createUuid().toString(QUuid::WithoutBraces));

    bool hadPrevious = QDir(finalDir).exists();
    if (hadPrevious && !QDir(finalInfo.absolutePath()).rename(finalName, backupName)) {
        if (error) *error = QObject::tr("Não foi possível substituir a exportação anterior. Feche o jogo exportado e tente novamente.");
        return false;
    }

    if (!QDir(finalInfo.absolutePath()).rename(tempName, finalName)) {
        if (hadPrevious) QDir(finalInfo.absolutePath()).rename(backupName, finalName);
        if (error) *error = QObject::tr("A nova exportação ficou pronta, mas não pôde ser movida para a pasta final.");
        return false;
    }

    if (hadPrevious) QDir(parent.filePath(backupName)).removeRecursively();
    return true;
}

ExportResult performExport(const ExportPlan& plan,
                           const std::shared_ptr<std::atomic_bool>& cancelled,
                           const ExportProgress& progress)
{
    ExportResult result;
    result.finalDir = plan.finalDir;
    result.playerName = plan.playerName;
    auto report = [&](int value, const QString& stage) {
        if (progress) progress(qBound(0, value, 100), stage);
    };
    auto fail = [&](const QString& message) {
        result.error = message;
        QDir(plan.tempDir).removeRecursively();
        return result;
    };
    auto checkCancel = [&]() {
        if (!exportCancelled(cancelled)) return false;
        result.cancelled = true;
        QDir(plan.tempDir).removeRecursively();
        return true;
    };

    QDir(plan.tempDir).removeRecursively();
    if (!QDir().mkpath(plan.tempDir))
        return fail(QObject::tr("Não foi possível criar a pasta temporária de exportação."));
    QDir out(plan.tempDir);

    report(3, QObject::tr("Preparando exportação temporária…"));
    if (!replaceCopy(plan.sourcePlayer, out.filePath(plan.playerName)))
        return fail(QObject::tr("Não foi possível copiar o LudoPlayer."));
    if (checkCancel()) return result;

    QString identityNote;
    if (!plan.exportIconPath.isEmpty() && QFileInfo::exists(plan.exportIconPath)) {
        const QString suffix = QFileInfo(plan.exportIconPath).suffix().toLower().isEmpty()
                                   ? QStringLiteral("png") : QFileInfo(plan.exportIconPath).suffix().toLower();
        if (!replaceCopy(plan.exportIconPath, out.filePath(QStringLiteral("game-icon.") + suffix)))
            identityNote += QObject::tr("\n\nAviso: não foi possível copiar o ícone para a pasta exportada.");
#ifdef Q_OS_WIN
        QString iconError;
        if (!applyWindowsExecutableIcon(out.filePath(plan.playerName), plan.exportIconPath, &iconError))
            identityNote += QObject::tr("\n\nAviso: o ícone será usado pela janela do jogo, mas não pôde ser gravado no .exe: %1").arg(iconError);
#endif
    }

    report(10, QObject::tr("Gravando game.ludo…"));
    const QByteArray projectBytes = plan.secure
        ? core::io::protectProjectPayload(plan.plainProjectBytes)
        : plan.plainProjectBytes;
    QSaveFile projectFile(out.filePath(QStringLiteral("game.ludo")));
    if (!projectFile.open(QIODevice::WriteOnly) ||
        projectFile.write(projectBytes) != projectBytes.size() || !projectFile.commit())
        return fail(QObject::tr("Não foi possível criar game.ludo."));

    // Preferencias de apresentacao pertencem ao jogo exportado, nao ao
    // registro da Ludo Engine. O player usa este arquivo apenas como default
    // inicial e depois preserva as escolhas feitas pelo jogador in-game.
    QSaveFile runtimeConfig(out.filePath(QStringLiteral("ludo-runtime.ini")));
    if(!runtimeConfig.open(QIODevice::WriteOnly|QIODevice::Text))
        return fail(QObject::tr("Não foi possível criar ludo-runtime.ini."));
    const QByteArray runtimeBytes=QStringLiteral("[graphics]\r\ngpuBackend=%1\r\nscaleFilter=%2\r\n")
        .arg(plan.gpuBackend,plan.scaleFilter).toUtf8();
    if(runtimeConfig.write(runtimeBytes)!=runtimeBytes.size()||!runtimeConfig.commit())
        return fail(QObject::tr("Não foi possível finalizar ludo-runtime.ini."));
    if (checkCancel()) return result;

#ifndef TES_HAS_AUDIO
    if(plan.audioRequired)
        return fail(QObject::tr("Este build da LUDO foi compilado sem Qt Multimedia, mas o projeto usa áudio. Recompile com TES_AUDIO=ON antes de exportar."));
#endif

    const QDir projectRoot(plan.projectRoot);
    if (plan.secure) {
        QVector<core::secure_assets::Entry> entries;
        entries.reserve(plan.selectedAssets.size());
        for (int i = 0; i < plan.selectedAssets.size(); ++i) {
            if (checkCancel()) return result;
            const QString& projectRelative = plan.selectedAssets.at(i);
            report(16 + ((i + 1) * 31 / qMax(1, plan.selectedAssets.size())),
                   QObject::tr("Lendo arquivos para o pacote protegido…"));
            QFile source(projectRoot.filePath(projectRelative));
            if (!source.open(QIODevice::ReadOnly))
                return fail(QObject::tr("Não foi possível ler o arquivo %1.").arg(projectRelative));
            entries.push_back({QDir::fromNativeSeparators(projectRelative), source.readAll()});
        }
        QString assetError;
        report(49, QObject::tr("Criptografando game.assets…"));
        if (!core::secure_assets::write(out.filePath(QStringLiteral("game.assets")), entries, &assetError))
            return fail(assetError);
    } else {
        for (int i = 0; i < plan.selectedAssets.size(); ++i) {
            if (checkCancel()) return result;
            const QString& projectRelative = plan.selectedAssets.at(i);
            const QString rel = projectRelative.mid(QStringLiteral("Assets/").size());
            report(16 + ((i + 1) * 33 / qMax(1, plan.selectedAssets.size())),
                   QObject::tr("Copiando arquivos do jogo…"));
            const QString from = projectRoot.filePath(projectRelative);
            const QString to = out.filePath(QStringLiteral("Assets/") + rel);
            if (!replaceCopy(from, to))
                return fail(QObject::tr("Não foi possível copiar o arquivo %1.").arg(rel));
        }
    }
    if (checkCancel()) return result;

    QString deployNote = identityNote;
    if (plan.secure)
        deployNote += QObject::tr("\n\nProteção de arquivos: game.ludo foi incluído em game.assets. Arquivos do jogo protegidos: %1.")
                          .arg(plan.selectedAssets.size());
    if (plan.cleanupUnused)
        deployNote += QObject::tr("\n\nArquivos do projeto: %1 usados pelo jogo; %2 sem uso foram deixados de fora.")
                          .arg(plan.selectedAssets.size()).arg(plan.skippedUnusedAssets);

#ifdef Q_OS_WIN
    report(54, QObject::tr("Preparando dependências do LudoPlayer…"));
    if (!plan.windeploy.isEmpty()) {
        report(58, QObject::tr("Executando windeployqt…"));
        const int code = runWinDeploy(plan.windeploy, out.filePath(plan.playerName), plan.runtimeDirs, cancelled);
        if (code == -2) { result.cancelled = true; QDir(plan.tempDir).removeRecursively(); return result; }
        if (code != 0)
            deployNote += QObject::tr("\n\nAviso: windeployqt retornou o código %1; conferindo DLLs manualmente.").arg(code);
    } else {
        deployNote += QObject::tr("\n\nAviso: windeployqt não foi encontrado. Copiando dependências conhecidas manualmente.");
    }
    if (checkCancel()) return result;

    const QStringList qtDlls = {
        QStringLiteral("Qt6Core.dll"), QStringLiteral("Qt6Gui.dll"),
        QStringLiteral("Qt6Widgets.dll"), QStringLiteral("Qt6Multimedia.dll"),
        QStringLiteral("Qt6Network.dll"), QStringLiteral("Qt6OpenGL.dll"),
        QStringLiteral("Qt6Svg.dll")
    };
    const QStringList qtDirs = { plan.appDir, plan.qtBinDir };
    for (const QString& dll : qtDlls) {
        if (QFileInfo::exists(out.filePath(dll))) continue;
        for (const QString& dir : qtDirs) {
            const QString src = QDir(dir).filePath(dll);
            if (QFileInfo::exists(src)) { replaceCopy(src, out.filePath(dll)); break; }
        }
    }
    report(72, QObject::tr("DLLs do Qt copiadas."));

    const QString platformTarget = out.filePath(QStringLiteral("platforms/qwindows.dll"));
    if (!QFileInfo::exists(platformTarget)) {
        QStringList candidates = {
            QDir(plan.appDir).filePath(QStringLiteral("platforms/qwindows.dll")),
            QDir(plan.appDir).filePath(QStringLiteral("plugins/platforms/qwindows.dll")),
            QDir(plan.qtPluginsDir).filePath(QStringLiteral("platforms/qwindows.dll"))
        };
        bool copied = false;
        for (const QString& candidate : candidates) {
            if (QFileInfo::exists(candidate) && replaceCopy(candidate, platformTarget)) { copied = true; break; }
        }
        if (!copied)
            return fail(QObject::tr("Não foi possível localizar platforms/qwindows.dll; o LudoPlayer não conseguiria iniciar."));
    }

    const QStringList pluginDirs = {
        QStringLiteral("imageformats"), QStringLiteral("iconengines"),
        QStringLiteral("multimedia"), QStringLiteral("networkinformation"),
        QStringLiteral("styles"), QStringLiteral("tls"), QStringLiteral("generic")
    };
    for (const QString& name : pluginDirs) {
        const QString fromApp = QDir(plan.appDir).filePath(name);
        const QString fromQt = QDir(plan.qtPluginsDir).filePath(name);
        if (QDir(fromApp).exists()) copyDirectoryContents(fromApp, out.filePath(name));
        else if (QDir(fromQt).exists()) copyDirectoryContents(fromQt, out.filePath(name));
    }
    // Fallback completo do backend FFmpeg do Qt Multimedia. Essas DLLs nao
    // possuem nomes fixos entre Qt 6.2/6.8; copiar por familia evita quebrar
    // a exportacao ao atualizar o patch/minor do Qt.
    QStringList multimediaRuntimeDirs=plan.runtimeDirs;
    multimediaRuntimeDirs.prepend(plan.qtBinDir);
    multimediaRuntimeDirs.prepend(plan.appDir);
    copyRuntimeDllFamilies(out,multimediaRuntimeDirs,{
        QStringLiteral("avcodec-*.dll"),QStringLiteral("avformat-*.dll"),
        QStringLiteral("avutil-*.dll"),QStringLiteral("swresample-*.dll"),
        QStringLiteral("swscale-*.dll")
    });
    writeQtConf(out);
    const QStringList missingRuntime = copyMinGwRuntime(out, plan.runtimeDirs);
    if (!missingRuntime.isEmpty())
        deployNote += QObject::tr("\n\nAviso: não encontrei estas DLLs do MinGW: %1")
                          .arg(missingRuntime.join(QStringLiteral(", ")));
    if(plan.audioRequired){
        if(!QFileInfo::exists(out.filePath(QStringLiteral("Qt6Multimedia.dll"))))
            return fail(QObject::tr("A exportação usa áudio, mas Qt6Multimedia.dll não foi implantada."));
        const QDir multimedia(out.filePath(QStringLiteral("multimedia")));
        const QDir nestedMultimedia(out.filePath(QStringLiteral("plugins/multimedia")));
        const bool hasBackend=!multimedia.entryList(QStringList{QStringLiteral("*.dll")},QDir::Files).isEmpty()||
                              !nestedMultimedia.entryList(QStringList{QStringLiteral("*.dll")},QDir::Files).isEmpty();
        if(!hasBackend)
            return fail(QObject::tr("A exportação usa áudio, mas o suporte de mídia do Qt não foi encontrado. Verifique se Qt Multimedia/FFmpeg está instalado."));
    }
#endif

    if (checkCancel()) return result;
    report(84, QObject::tr("Finalizando arquivos do jogo…"));

    QString commitError;
    if (!replaceDirectoryAtomically(plan.tempDir, plan.finalDir, &commitError))
        return fail(commitError);

    QString zipNote;
    if (plan.portable && !exportCancelled(cancelled)) {
        const QDir finalOut(plan.finalDir);
        QVector<core::zip::Entry> packageEntries;
        QStringList packageFiles;
        for (QDirIterator it(finalOut.absolutePath(), QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
            packageFiles.push_back(it.next());
        packageEntries.reserve(packageFiles.size());
        for (int i = 0; i < packageFiles.size(); ++i) {
            if (exportCancelled(cancelled)) break;
            const QString filePath = packageFiles.at(i);
            report(86 + ((i + 1) * 10 / qMax(1, packageFiles.size())),
                   QObject::tr("Montando pacote portátil…"));
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) continue;
            packageEntries.push_back({
                finalOut.relativeFilePath(filePath).replace(QLatin1Char('\\'), QLatin1Char('/')),
                file.readAll()
            });
        }
        if (!exportCancelled(cancelled)) {
            const QString zipPath = QDir(plan.parentDir).filePath(plan.safeName + QStringLiteral("-portatil.zip"));
            const QString tempZip = zipPath + QStringLiteral(".tmp");
            QFile::remove(tempZip);
            QString zipError;
            if (core::zip::write(tempZip, packageEntries, &zipError)) {
                QFile::remove(zipPath);
                if (QFile::rename(tempZip, zipPath))
                    zipNote = QObject::tr("\n\nPacote portátil criado em:\n%1").arg(zipPath);
                else
                    zipNote = QObject::tr("\n\nO ZIP foi criado, mas não pôde substituir a versão portátil anterior.");
            } else {
                zipNote = QObject::tr("\n\nNão foi possível criar o ZIP portátil: %1").arg(zipError);
            }
        } else {
            zipNote = QObject::tr("\n\nA exportação principal foi concluída; a criação do ZIP portátil foi cancelada.");
        }
    }

    report(100, QObject::tr("Exportação concluída."));
    result.ok = true;
    result.note = zipNote + deployNote;
    return result;
}

} // namespace

void GameExporter::run(core::Editor& ed, QWidget* parentWidget)
{
    // O Asset Database é sincronizado antes do assistente para que a mesma
    // fotografia de referências alimente Project Health e o plano de export.
    QProgressDialog assetProgress(QObject::tr("Atualizando referências e arquivos…"), QString(), 0, 0, parentWidget);
    assetProgress.setWindowTitle(QObject::tr("Preparando exportação"));
    assetProgress.setWindowModality(Qt::WindowModal);
    assetProgress.setMinimumDuration(0);
    assetProgress.setCancelButton(nullptr);
    QFutureWatcher<AssetSyncResult> assetWatcher;
    QEventLoop assetLoop;
    QObject::connect(&assetWatcher, &QFutureWatcher<AssetSyncResult>::finished, &assetLoop, &QEventLoop::quit);
    const QString projectRootPath = ed.projectRoot();
    const core::AssetDatabase databaseCopy = ed.assetDatabase;
    assetWatcher.setFuture(QtConcurrent::run([databaseCopy, projectRootPath]() mutable {
        AssetSyncResult r;
        r.database = databaseCopy;
        r.ok = r.database.synchronize(projectRootPath, &r.error);
        if (r.ok) r.changes = r.database.takePathChanges();
        return r;
    }));
    assetProgress.show();
    if (!assetWatcher.isFinished()) assetLoop.exec();
    assetProgress.close();
    const AssetSyncResult assetSync = assetWatcher.result();
    if (!assetSync.ok) {
        QMessageBox::critical(parentWidget, QObject::tr("Saúde do projeto"),
                              QObject::tr("Não foi possível atualizar os arquivos do projeto antes da exportação: %1")
                                  .arg(assetSync.error));
        return;
    }
    ed.assetDatabase = assetSync.database;
    for (const core::AssetPathChange& change : assetSync.changes)
        core::io::rewriteEditorAssetPath(ed, change.oldPath, change.newPath);

    const ExportPreflightResult preflight = ExportPreflight::inspect(ed);
    ExportWizardDialog wizard(ed, preflight, parentWidget);
    if (wizard.exec() != QDialog::Accepted) return;
    if (!preflight.ready()) {
        QMessageBox::warning(parentWidget, QObject::tr("Exportação bloqueada"),
                             QObject::tr("Ainda não é possível exportar o jogo. Corrija os problemas indicados em Saúde do Projeto e tente novamente."));
        return;
    }
    const ExportWizardOptions options = wizard.options();

    const QString exportName = options.gameName.trimmed();
    const QString exportIconPath = options.iconPath.trimmed();
    const QString parent = options.parentDir;
    QString safe = exportName;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    if (safe.isEmpty()) safe = QStringLiteral("MeuJogo");
    const QString finalDir = QDir(parent).filePath(safe);
    if (QDir(finalDir).exists() &&
        QMessageBox::question(parentWidget, QObject::tr("Atualizar exportação"),
                              QObject::tr("A pasta %1 já existe. Atualizar os arquivos do jogo?\n\n"
                                          "A versão atual será preservada até a nova terminar.")
                                  .arg(finalDir)) != QMessageBox::Yes) return;

    const ExportPlatformProfile platformProfile = ExportPlatformPolicy::hostProfile();
    if (options.targetPlatform != ExportPlatform::Unknown && options.targetPlatform != platformProfile.platform) {
        QMessageBox::warning(parentWidget, QObject::tr("Plataforma de exportação"),
                             QObject::tr("Esta build da LUDO exporta para %1. Para gerar outra plataforma, execute o exportador nessa plataforma.")
                                 .arg(platformProfile.displayName));
        return;
    }
    const QString playerName = safe + platformProfile.executableSuffix;
    QJsonObject exportPayload = core::io::buildRuntimeProjectPayload(ed);
    exportPayload[QStringLiteral("projectName")] = exportName;
    exportPayload[QStringLiteral("runtimeTargetPlatform")] = platformProfile.id;
    exportPayload[QStringLiteral("runtimeBuildProfile")] = options.buildProfile;
    const QByteArray plainProjectBytes = QJsonDocument(exportPayload).toJson(QJsonDocument::Compact);

    const QStringList selectedAssets = options.cleanupUnused ? preflight.usedAssets : preflight.allAssets;
    const int skippedUnusedAssets = options.cleanupUnused ? preflight.unusedAssetCount() : 0;

    ExportPlan plan;
    plan.exportName = exportName;
    plan.safeName = safe;
    plan.parentDir = parent;
    plan.finalDir = finalDir;
    plan.tempDir = QDir(parent).filePath(QStringLiteral(".%1.ludo-export-%2")
                                            .arg(safe, QUuid::createUuid().toString(QUuid::WithoutBraces)));
    plan.sourcePlayer = preflight.sourcePlayerPath;
    plan.playerName = playerName;
    plan.exportIconPath = exportIconPath;
    plan.projectRoot = ed.projectRoot();
    plan.appDir = QCoreApplication::applicationDirPath();
    plan.qtBinDir = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    plan.qtPluginsDir = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    plan.selectedAssets = selectedAssets;
    plan.plainProjectBytes = plainProjectBytes;
    plan.gpuBackend = game::normalizeRuntimeGpuBackend(ed.runtimeGpuBackend);
    plan.scaleFilter = ed.runtimeScaleFilter == QLatin1String("bilinear")
        ? QStringLiteral("bilinear") : QStringLiteral("nearest");
    plan.secure = options.secure;
    plan.portable = options.portableZip;
    plan.cleanupUnused = options.cleanupUnused;
    plan.skippedUnusedAssets = skippedUnusedAssets;
    plan.audioRequired = preflight.audioRequired;
#ifdef Q_OS_WIN
    plan.runtimeDirs = mingwRuntimeSearchDirs();
    plan.windeploy = QStandardPaths::findExecutable(QStringLiteral("windeployqt"));
    if (plan.windeploy.isEmpty()) {
        const QString candidate = QDir(plan.qtBinDir).filePath(QStringLiteral("windeployqt.exe"));
        if (QFileInfo::exists(candidate)) plan.windeploy = candidate;
    }
#endif

    QProgressDialog progress(parentWidget);
    progress.setWindowTitle(QObject::tr("Exportando jogo"));
    progress.setRange(0, 100);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setCancelButtonText(QObject::tr("Cancelar"));
    progress.setLabelText(QObject::tr("Preparando job de exportação…"));
    progress.setValue(0);

    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    QObject::connect(&progress, &QProgressDialog::canceled, &progress, [cancelled, &progress] {
        cancelled->store(true, std::memory_order_relaxed);
        progress.setLabelText(QObject::tr("Cancelando com segurança…\nA exportação anterior será preservada."));
    });

    QFutureWatcher<ExportResult> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<ExportResult>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([plan, cancelled, &progress] {
        const ExportProgress reporter = [&progress](int percent, const QString& stage) {
            QMetaObject::invokeMethod(&progress, [&progress, percent, stage] {
                progress.setLabelText(QObject::tr("%1\n\n%2% concluído · %3% restante")
                                          .arg(stage).arg(percent).arg(100 - percent));
                progress.setValue(percent);
            }, Qt::QueuedConnection);
        };
        return performExport(plan, cancelled, reporter);
    }));

    progress.show();
    if (!watcher.isFinished()) loop.exec();
    progress.close();
    const ExportResult result = watcher.result();

    if (result.cancelled && !result.ok) {
        QMessageBox::information(parentWidget, QObject::tr("Exportação cancelada"),
                                 QObject::tr("A exportação foi cancelada. A versão exportada anterior foi preservada."));
        return;
    }
    if (!result.ok) {
        QMessageBox::warning(parentWidget, QObject::tr("Exportar jogo"), result.error);
        return;
    }

    QMessageBox done(parentWidget);
    done.setWindowTitle(QObject::tr("Jogo exportado"));
    done.setIcon(QMessageBox::Information);
    done.setText(QObject::tr("Exportação concluída com sucesso."));
    done.setInformativeText(QObject::tr("Pasta: %1\nExecutável: %2").arg(result.finalDir, result.playerName) + result.note);
    QAbstractButton* openFolder = done.addButton(QObject::tr("Abrir pasta"), QMessageBox::ActionRole);
    done.addButton(QObject::tr("Fechar"), QMessageBox::AcceptRole);
    done.exec();
    if (done.clickedButton() == openFolder)
        QDesktopServices::openUrl(QUrl::fromLocalFile(result.finalDir));
}

} // namespace ui
