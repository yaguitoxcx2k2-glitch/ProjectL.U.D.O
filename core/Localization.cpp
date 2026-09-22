#include "Localization.h"

#include <QFile>
#include <QLocale>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStringConverter>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>

namespace core {
namespace {
QString csvEscape(const QString& value)
{
    QString out = value;
    out.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (out.contains(QLatin1Char(',')) || out.contains(QLatin1Char('"')) ||
        out.contains(QLatin1Char('\n')) || out.contains(QLatin1Char('\r')))
        return QLatin1Char('"') + out + QLatin1Char('"');
    return out;
}

QVector<QStringList> parseCsvRecords(const QString& data, bool* valid)
{
    QVector<QStringList> records;
    QStringList row;
    QString cell;
    bool quoted = false;
    bool fieldStartedWithQuote = false;
    bool ok = true;
    auto finishCell = [&] { row.push_back(cell); cell.clear(); fieldStartedWithQuote = false; };
    auto finishRow = [&] { finishCell(); records.push_back(row); row.clear(); };
    for (int i = 0; i < data.size(); ++i) {
        const QChar ch = data.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < data.size() && data.at(i + 1) == QLatin1Char('"')) { cell += QLatin1Char('"'); ++i; }
                else quoted = false;
            } else cell += ch;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            if (!cell.isEmpty() || fieldStartedWithQuote) { ok = false; break; }
            quoted = true; fieldStartedWithQuote = true;
        } else if (ch == QLatin1Char(',')) {
            finishCell();
        } else if (ch == QLatin1Char('\n')) {
            finishRow();
        } else if (ch == QLatin1Char('\r')) {
            if (i + 1 < data.size() && data.at(i + 1) == QLatin1Char('\n')) ++i;
            finishRow();
        } else cell += ch;
    }
    if (quoted) ok = false;
    if (ok && (!cell.isEmpty() || !row.isEmpty() || fieldStartedWithQuote)) finishRow();
    if (valid) *valid = ok;
    return records;
}
}

QString normalizeLocalizationKey(QString key)
{
    key = key.trimmed().toLower();
    key.replace(QRegularExpression(QStringLiteral("[^a-z0-9_.-]+")), QStringLiteral("_"));
    key.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    while (key.startsWith(QLatin1Char('.')) || key.startsWith(QLatin1Char('_'))) key.remove(0, 1);
    while (key.endsWith(QLatin1Char('.')) || key.endsWith(QLatin1Char('_'))) key.chop(1);
    return key.left(160);
}

QString systemLocaleCode()
{
    return QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'));
}

QString currentPlayerLocale(const LocalizationSettings& settings)
{
    if (!settings.enabled) return {};
    QSettings preferences;
    QString requested = preferences.value(QStringLiteral("game/locale")).toString().trimmed();
    if (requested.isEmpty()) {
        requested = settings.initialMode == QLatin1String("system")
            ? systemLocaleCode() : settings.defaultLocale;
    }
    return settings.resolvedLocale(requested);
}

QString setPlayerLocale(const LocalizationSettings& settings, const QString& requestedLocale)
{
    if (!settings.enabled) return {};
    QString requested = requestedLocale.trimmed();
    if (requested.isEmpty() || requested == QLatin1String("default"))
        requested = settings.defaultLocale;
    else if (requested == QLatin1String("system"))
        requested = systemLocaleCode();
    const QString resolved = settings.resolvedLocale(requested);
    if (resolved.isEmpty()) return {};
    QSettings preferences;
    preferences.setValue(QStringLiteral("game/locale"), resolved);
    preferences.sync();
    return resolved;
}

QString resolvePlayerText(const LocalizationSettings& settings, const QString& key,
                          const QString& fallbackText)
{
    return settings.resolve(key, fallbackText, currentPlayerLocale(settings));
}

QStringList LocalizationSettings::enabledLocaleCodes() const
{
    QStringList out;
    for (const auto& locale : locales) if (locale.enabled && !locale.code.trimmed().isEmpty()) out.push_back(locale.code.trimmed());
    if (out.isEmpty() && !defaultLocale.isEmpty()) out.push_back(defaultLocale);
    out.removeDuplicates();
    return out;
}

bool LocalizationSettings::hasLocale(const QString& code) const
{
    for (const auto& locale : locales) if (locale.code.compare(code, Qt::CaseInsensitive) == 0) return true;
    return false;
}

bool LocalizationSettings::hasKey(const QString& key) const
{
    return texts.contains(normalizeLocalizationKey(key));
}

QString LocalizationSettings::resolvedLocale(const QString& requestedLocale) const
{
    const QStringList available = enabledLocaleCodes();
    auto exact = [&](const QString& code) -> QString {
        for (const QString& value : available) if (value.compare(code, Qt::CaseInsensitive) == 0) return value;
        return {};
    };
    auto languageOnly = [&](const QString& code) -> QString {
        const QString language = code.section(QLatin1Char('-'), 0, 0).toLower();
        for (const QString& value : available) if (value.section(QLatin1Char('-'), 0, 0).toLower() == language) return value;
        return {};
    };
    QString locale = requestedLocale.trimmed();
    if (locale.isEmpty()) locale = defaultLocale;
    if (const QString v = exact(locale); !v.isEmpty()) return v;
    if (const QString v = languageOnly(locale); !v.isEmpty()) return v;
    if (const QString v = exact(fallbackLocale); !v.isEmpty()) return v;
    if (const QString v = exact(defaultLocale); !v.isEmpty()) return v;
    return available.value(0, defaultLocale);
}

QString LocalizationSettings::resolve(const QString& key, const QString& fallbackText,
                                      const QString& requestedLocale) const
{
    if (!enabled || key.trimmed().isEmpty()) return fallbackText;
    const QString normalized = normalizeLocalizationKey(key);
    const auto it = texts.constFind(normalized);
    if (it == texts.cend()) return fallbackText;
    const QString locale = resolvedLocale(requestedLocale);
    auto valueFor = [&](const QString& code) -> QString {
        for (auto jt = it->cbegin(); jt != it->cend(); ++jt)
            if (jt.key().compare(code, Qt::CaseInsensitive) == 0 && !jt.value().isEmpty()) return jt.value();
        return {};
    };
    if (const QString v = valueFor(locale); !v.isEmpty()) return v;
    if (const QString v = valueFor(fallbackLocale); !v.isEmpty()) return v;
    if (const QString v = valueFor(defaultLocale); !v.isEmpty()) return v;
    for (auto jt = it->cbegin(); jt != it->cend(); ++jt) if (!jt.value().isEmpty()) return jt.value();
    return fallbackText;
}

int LocalizationSettings::completionPercent(const QString& locale) const
{
    if (texts.isEmpty()) return 100;
    int translated = 0;
    for (auto it = texts.cbegin(); it != texts.cend(); ++it) {
        bool found = false;
        for (auto jt = it->cbegin(); jt != it->cend(); ++jt)
            if (jt.key().compare(locale, Qt::CaseInsensitive) == 0 && !jt.value().trimmed().isEmpty()) { found = true; break; }
        if (found) ++translated;
    }
    return qRound(100.0 * translated / qMax(1, texts.size()));
}

void LocalizationSettings::ensureDefaults()
{
    if (defaultLocale.trimmed().isEmpty()) defaultLocale = QStringLiteral("pt-BR");
    if (fallbackLocale.trimmed().isEmpty()) fallbackLocale = defaultLocale;
    if (initialMode != QLatin1String("system") && initialMode != QLatin1String("last")) initialMode = QStringLiteral("default");
    if (!hasLocale(defaultLocale)) locales.prepend(LocalizationLocale{defaultLocale, defaultLocale, true});
    for (auto& locale : locales) {
        locale.code = locale.code.trimmed().replace(QLatin1Char('_'), QLatin1Char('-')).left(32);
        locale.name = locale.name.trimmed().left(96);
        if (locale.name.isEmpty()) locale.name = locale.code;
    }
}

bool exportLocalizationCsv(const LocalizationSettings& settings, const QString& filePath, QString* error)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { if (error) *error = file.errorString(); return false; }
    QTextStream out(&file); out.setEncoding(QStringConverter::Utf8);
    const QStringList locales = settings.enabledLocaleCodes();
    out << "key"; for (const QString& locale : locales) out << ',' << csvEscape(locale); out << '\n';
    QStringList keys = settings.texts.keys(); std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        out << csvEscape(key);
        const auto values = settings.texts.value(key);
        for (const QString& locale : locales) out << ',' << csvEscape(values.value(locale));
        out << '\n';
    }
    if (!file.commit()) { if (error) *error = file.errorString(); return false; }
    return true;
}

bool importLocalizationCsv(LocalizationSettings& settings, const QString& filePath,
                           QStringList* warnings, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { if (error) *error = file.errorString(); return false; }
    QTextStream in(&file); in.setEncoding(QStringConverter::Utf8);
    const QString csvData = in.readAll();
    if (csvData.isEmpty()) { if (error) *error = QStringLiteral("CSV vazio."); return false; }
    bool csvValid = false;
    const QVector<QStringList> records = parseCsvRecords(csvData, &csvValid);
    if (!csvValid || records.isEmpty()) { if (error) *error = QStringLiteral("CSV inválido: aspas ou campos malformados."); return false; }
    const QStringList header = records.first();
    if (header.isEmpty() || header.first().trimmed().compare(QStringLiteral("key"), Qt::CaseInsensitive) != 0) {
        if (error) *error = QStringLiteral("A primeira coluna precisa se chamar key."); return false;
    }
    QStringList locales;
    for (int i = 1; i < header.size(); ++i) {
        QString code = header.at(i).trimmed().replace(QLatin1Char('_'), QLatin1Char('-'));
        if (code.isEmpty()) { if (error) *error = QStringLiteral("Coluna de idioma vazia."); return false; }
        if (locales.contains(code, Qt::CaseInsensitive)) { if (error) *error = QStringLiteral("Idioma duplicado no CSV: %1").arg(code); return false; }
        locales.push_back(code);
        if (!settings.hasLocale(code)) settings.locales.push_back(LocalizationLocale{code, code, true});
    }
    QSet<QString> seen;
    for (int recordIndex = 1; recordIndex < records.size(); ++recordIndex) {
        const QStringList cells = records.at(recordIndex);
        bool emptyRecord = true; for (const QString& cell : cells) if (!cell.trimmed().isEmpty()) { emptyRecord = false; break; }
        if (emptyRecord) continue;
        const int displayLine = recordIndex + 1;
        const QString key = normalizeLocalizationKey(cells.value(0));
        if (key.isEmpty()) { if (warnings) warnings->push_back(QStringLiteral("Registro %1: chave vazia ignorada.").arg(displayLine)); continue; }
        if (seen.contains(key)) { if (warnings) warnings->push_back(QStringLiteral("Registro %1: chave duplicada %2; última ocorrência prevaleceu.").arg(displayLine).arg(key)); }
        seen.insert(key);
        auto& values = settings.texts[key];
        for (int i = 0; i < locales.size(); ++i) values[locales.at(i)] = cells.value(i + 1);
        if (cells.size() > locales.size() + 1 && warnings)
            warnings->push_back(QStringLiteral("Registro %1: colunas extras foram ignoradas.").arg(displayLine));
    }
    settings.ensureDefaults();
    return true;
}

} // namespace core
