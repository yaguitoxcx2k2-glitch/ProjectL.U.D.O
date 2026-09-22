#include "core/UiThemePackage.h"
#include "core/ProjectIO.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryFile>

namespace core::io {
namespace {

const QStringList& themeKeys()
{
    static const QStringList keys{
        QStringLiteral("windowSkinPath"), QStringLiteral("windowSkin"),
        QStringLiteral("sliceLeft"), QStringLiteral("sliceTop"),
        QStringLiteral("sliceRight"), QStringLiteral("sliceBottom"),
        QStringLiteral("cursorPath"), QStringLiteral("cursorImage"),
        QStringLiteral("windowFill"), QStringLiteral("windowBorder"),
        QStringLiteral("innerBorder"), QStringLiteral("textColor"),
        QStringLiteral("selectedTextColor"), QStringLiteral("accentColor"),
        QStringLiteral("selectionColor"), QStringLiteral("paddingX"),
        QStringLiteral("paddingY"), QStringLiteral("fontFamily"),
        QStringLiteral("fontSize"), QStringLiteral("windowOpacity"),
        QStringLiteral("openAnimation"), QStringLiteral("closeAnimation"),
        QStringLiteral("animationEasing"), QStringLiteral("animationMs"),
        QStringLiteral("cursorSePath"), QStringLiteral("confirmSePath"),
        QStringLiteral("cancelSePath"), QStringLiteral("soundVolume"),
        QStringLiteral("themeName"), QStringLiteral("widgetsInheritWindowSkin"),
        QStringLiteral("styleClasses"), QStringLiteral("widgetTypeStyles"),
        QStringLiteral("nativeComponentStyles")
    };
    return keys;
}

QJsonObject filteredThemeObject(const GameUiSettings& settings)
{
    Editor seed;
    seed.gameUi = settings;
    const QJsonObject all = buildProjectPayload(seed).value(QStringLiteral("gameUi")).toObject();
    QJsonObject theme;
    for (const QString& key : themeKeys())
        if (all.contains(key)) theme.insert(key, all.value(key));
    return theme;
}

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

} // namespace

QString uiThemePackageExtension()
{
    return QStringLiteral(".ludotheme");
}

bool saveUiThemePackage(const GameUiSettings& settings, const QString& path, QString* error)
{
    if (path.trimmed().isEmpty()) return fail(error, QStringLiteral("Caminho do tema vazio."));
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("LUDO_UI_THEME"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("gameUi"), filteredThemeObject(settings));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Não foi possível criar o tema: %1").arg(file.errorString()));
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0)
        return fail(error, QStringLiteral("Falha ao gravar o tema: %1").arg(file.errorString()));
    if (!file.commit())
        return fail(error, QStringLiteral("Falha ao finalizar o tema: %1").arg(file.errorString()));
    if (error) error->clear();
    return true;
}

bool loadUiThemePackage(const GameUiSettings& base, const QString& path,
                        GameUiSettings* out, QString* error)
{
    if (!out) return fail(error, QStringLiteral("Destino de tema inválido."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Não foi possível abrir o tema: %1").arg(file.errorString()));
    if (file.size() > 16 * 1024 * 1024)
        return fail(error, QStringLiteral("Arquivo de tema maior que o limite suportado (16 MiB)."));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, QStringLiteral("Tema inválido: %1").arg(parseError.errorString()));
    const QJsonObject package = doc.object();
    if (package.value(QStringLiteral("format")).toString() != QLatin1String("LUDO_UI_THEME"))
        return fail(error, QStringLiteral("Este arquivo não é um tema da LUDO."));
    if (package.value(QStringLiteral("version")).toInt() != 1)
        return fail(error, QStringLiteral("Versão de tema não suportada."));
    const QJsonObject incoming = package.value(QStringLiteral("gameUi")).toObject();
    if (incoming.isEmpty()) return fail(error, QStringLiteral("O tema não contém configurações."));

    // Reutiliza o parser canônico do ProjectIO em vez de manter um segundo
    // desserializador de UiStyleClass/GameUi. Só as chaves de tema entram no
    // merge; telas, Widgets, Components e layouts permanecem os de `base`.
    Editor seed;
    seed.gameUi = base;
    QJsonObject project = buildProjectPayload(seed);
    QJsonObject merged = project.value(QStringLiteral("gameUi")).toObject();
    for (const QString& key : themeKeys())
        if (incoming.contains(key)) merged.insert(key, incoming.value(key));
    project.insert(QStringLiteral("gameUi"), merged);

    QTemporaryFile temp;
    temp.setAutoRemove(true);
    if (!temp.open()) return fail(error, QStringLiteral("Não foi possível preparar o tema para validação."));
    if (temp.write(QJsonDocument(project).toJson(QJsonDocument::Compact)) < 0) return fail(error, QStringLiteral("Falha ao validar o tema."));
    const QString tempName = temp.fileName();
    temp.close();

    Editor parsed;
    QString loadError;
    if (!loadProject(parsed, tempName, &loadError))
        return fail(error, QStringLiteral("Tema rejeitado pelo ProjectIO: %1").arg(loadError));
    *out = parsed.gameUi;
    if (error) error->clear();
    return true;
}

} // namespace core::io
