// ============================================================================
// AssetBrowser.h — Navegador de conteúdo do projeto (estilo editores modernos).
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/ProjectDependencyIndex.h"

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeView;
QT_END_NAMESPACE

namespace ui {

class AssetBrowserDialog : public QDialog
{
public:
    enum class Mode { Manage, SelectOneImage, SelectManyImages };

    AssetBrowserDialog(core::Editor& ed, Mode mode,
                       const QString& initialFolder = QString(),
                       QWidget* parent = nullptr);

    QString selectedPath() const;
    QStringList selectedPaths() const;

    static QString chooseImage(core::Editor& ed, QWidget* parent,
                               const QString& initialFolder = QStringLiteral("References"));
    static QStringList chooseImages(core::Editor& ed, QWidget* parent,
                                    const QString& initialFolder = QStringLiteral("References"));
    static void manage(core::Editor& ed, QWidget* parent);

    /// Cria a estrutura padrão e devolve false com mensagem em caso de erro.
    static bool ensureProjectFolders(const QString& projectRoot, QString* error = nullptr);

private:
    QString destinationDirectory() const;
    void refreshPreview();
    void createFolder();
    void importFiles();
    void removeSelected();
    void synchronizeAssetDatabase(bool showErrors = false);
    void scheduleAssetDatabaseSync();
    void locateMissingAssets();
    void updateAssetDatabaseStatus();
    bool selectionIsAcceptable() const;
    void refreshDependencySnapshot();
    void applyBrowserFilter();
    void refreshWorkflowMetadata();
    void persistWorkflowMetadata();

    core::Editor& m_ed;
    Mode m_mode;
    QFileSystemModel* m_model = nullptr;
    QTreeView* m_tree = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_path = nullptr;
    QLabel* m_assetInfo = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_category = nullptr;
    QCheckBox* m_favorite = nullptr;
    QLineEdit* m_tags = nullptr;
    QPushButton* m_missingAssets = nullptr;
    QCheckBox* m_pixelArt2x = nullptr;
    bool m_assetSyncPending = false;
    bool m_assetSyncRunning = false;
    bool m_assetSyncAgain = false;
    quint64 m_previewGeneration = 0;
    core::ProjectDependencySnapshot m_dependencies;
    QHash<QString, QImage> m_previewCache;
    QStringList m_previewCacheOrder;
};

} // namespace ui
