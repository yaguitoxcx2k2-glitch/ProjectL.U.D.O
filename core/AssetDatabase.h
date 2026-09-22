// ============================================================================
// AssetDatabase.h — banco de imagens estável do LUDO Map Editor.
//
// Cada imagem de autoria abaixo de Assets/ recebe um GUID persistente.
// Caminhos continuam gravados por compatibilidade, mas o GUID permite que
// renomes/movimentos sejam reconciliados sem quebrar referências do projeto.
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace core {

struct AssetRecord {
    QString id;                 ///< asset.<uuid>
    QString path;               ///< caminho atual relativo ao projeto (Assets/...)
    QString type;               ///< image/audio/font/other
    QString category;           ///< categoria de workflow: characters/bgm/voice/...
    QString sha256;             ///< hash hexadecimal do conteúdo
    qint64 size = 0;
    QStringList aliases;        ///< caminhos anteriores, para migração segura
    QJsonObject metadata;       ///< metadata extensível por asset (ex.: spriteLayout).
    bool missing = false;       ///< registrado, porém arquivo não encontrado
};

struct AssetPathChange {
    QString id;
    QString oldPath;
    QString newPath;
};

class AssetDatabase
{
public:
    AssetDatabase() = default;
    AssetDatabase(const AssetDatabase& other) : m_records(other.m_records), m_pathChanges(other.m_pathChanges) {}
    AssetDatabase& operator=(const AssetDatabase& other) {
        if (this != &other) { m_records=other.m_records; m_pathChanges=other.m_pathChanges; }
        return *this; // Observers belong to the destination editor, never to worker copies.
    }
    void clear();
    std::function<void(const QString&)> metadataChanged;
    static bool isInternalPath(const QString& path);

    /// Reconcilia imagens de autoria com o conteúdo atual de Assets/. IDs são
    /// preservados por caminho e, em moves/renames, por SHA-256 único.
    bool synchronize(const QString& projectRoot, QString* error = nullptr);

    /// Atualiza apenas arquivos conhecidos como alterados. É o caminho usado
    /// pelo Team Asset Sync e pelos watchers para não re-hashear Assets/ inteiro.
    bool synchronizePaths(const QString& projectRoot, const QStringList& projectRelativePaths,
                          QString* error = nullptr);

    QString idForPath(const QString& projectRelativePath) const;
    QString pathForId(const QString& id) const;
    QString canonicalPath(const QString& currentOrLegacyPath) const;
    const AssetRecord* recordById(const QString& id) const;
    const AssetRecord* recordByPath(const QString& path) const;
    QJsonObject metadataForPath(const QString& path) const;
    bool setMetadataValueForPath(const QString& path, const QString& key, const QJsonValue& value);

    QVector<AssetRecord> records() const;
    QVector<AssetRecord> recordsForCategory(const QString& categoryId, bool includeMissing = false) const;
    QVector<AssetRecord> missingRecords() const;
    QVector<AssetPathChange> takePathChanges();

    /// Reassocia manualmente um GUID a um novo arquivo (localizador de asset
    /// ausente). O arquivo deve estar dentro do projeto.
    bool rebind(const QString& id, const QString& projectRelativePath,
                const QString& projectRoot, QString* error = nullptr);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    static QString normalizePath(const QString& path);
    static QString typeForPath(const QString& path);
    static QString newAssetId();

private:
    static bool fingerprint(const QString& absolutePath, QString* sha256,
                            qint64* size, QString* error = nullptr);
    int indexById(const QString& id) const;
    int indexByPathOrAlias(const QString& normalized) const;

    QVector<AssetRecord> m_records;
    QVector<AssetPathChange> m_pathChanges;
};

} // namespace core
