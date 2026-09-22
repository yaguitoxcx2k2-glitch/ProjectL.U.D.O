#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

struct LocalizationLocale {
    QString code;
    QString name;
    bool enabled = true;
};

/// Bloco F / 3.28.1 — catálogo de textos do jogo. A localização é opcional:
/// textos diretos continuam válidos e funcionam como fallback seguro.
struct LocalizationSettings {
    bool enabled = false;
    QString defaultLocale = QStringLiteral("pt-BR");
    QString fallbackLocale = QStringLiteral("pt-BR");
    QString initialMode = QStringLiteral("default"); // default/system/last
    QVector<LocalizationLocale> locales;
    QHash<QString, QHash<QString, QString>> texts; // key -> locale -> text

    QStringList enabledLocaleCodes() const;
    bool hasLocale(const QString& code) const;
    bool hasKey(const QString& key) const;
    QString resolvedLocale(const QString& requestedLocale = QString()) const;
    QString resolve(const QString& key, const QString& fallbackText = QString(),
                    const QString& requestedLocale = QString()) const;
    int completionPercent(const QString& locale) const;
    void ensureDefaults();
};

QString normalizeLocalizationKey(QString key);
QString systemLocaleCode();

/// Idioma efetivo usado pelo jogo neste processo. Centraliza a preferência do
/// jogador para que Interpreter, UI, batalha, loja e Player não mantenham
/// leituras divergentes de QSettings.
QString currentPlayerLocale(const LocalizationSettings& settings);
/// Troca o idioma do jogador, normaliza contra os idiomas habilitados e força
/// a persistência antes de devolver. Retorna o código efetivamente escolhido.
QString setPlayerLocale(const LocalizationSettings& settings, const QString& requestedLocale);
/// Atalho para textos do runtime: resolve a chave usando o idioma atual do
/// jogador e preserva o texto direto como fallback.
QString resolvePlayerText(const LocalizationSettings& settings, const QString& key,
                          const QString& fallbackText = QString());

/// CSV simples e seguro para tradutores. Retorna false e preenche error em
/// entradas estruturalmente inválidas; chaves desconhecidas podem ser criadas.
bool exportLocalizationCsv(const LocalizationSettings& settings, const QString& filePath,
                           QString* error = nullptr);
bool importLocalizationCsv(LocalizationSettings& settings, const QString& filePath,
                           QStringList* warnings = nullptr, QString* error = nullptr);

} // namespace core
