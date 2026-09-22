#pragma once

#include "core/Editor.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

namespace ui {

/// Tela inicial de gerenciamento de projetos da LUDO.
///
/// O Project Manager usa o mesmo ProjectCreationWorkflow da MainWindow.
/// O manager mantém somente metadados locais
/// (recentes/favoritos) em QSettings; o arquivo .ludo continua sendo a fonte
/// de verdade do projeto.
class ProjectManagerDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ProjectManagerDialog(core::Editor& editor, QWidget* parent = nullptr);

    QString selectedProjectPath() const { return m_selectedProjectPath; }
    bool selectedProjectAlreadyLoaded() const { return m_selectedProjectAlreadyLoaded; }
    bool teamRequested() const { return m_teamRequested; }
    QString selectedRpgMakerRoot() const { return m_selectedRpgMakerRoot; }

    /// Registra/atualiza um projeto na lista de recentes do Project Manager.
    /// Pode ser chamado pelo MainWindow quando um projeto é criado/aberto/salvo.
    static void rememberProject(const QString& projectPath, bool touchLastOpened = true);
    /// Caminho canônico de um projeto lógico. Prefere a cópia integrada ao
    /// RPG Maker quando entradas antigas apontam para o mesmo projectId.
    static QString projectPathForIdentity(const QString& projectId);

private:
    void rebuildTable();
    void refreshDetails();
    void updateButtons();
    QString currentProjectPath() const;
    void openSelected();
    void createProject();
    void addExistingProject();
    void locateMissingProject();
    void toggleFavorite();
    void duplicateProject();
    void renameProject();
    void showProjectFolder();
    void removeFromList();
    void deleteProject();

    core::Editor& m_editor;
    QString m_selectedProjectPath;
    bool m_selectedProjectAlreadyLoaded = false;
    bool m_teamRequested = false;
    QString m_selectedRpgMakerRoot;

    QLineEdit* m_search = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailStatus = nullptr;
    QLabel* m_detailMeta = nullptr;
    QLabel* m_detailHealth = nullptr;
    QLabel* m_detailRecovery = nullptr;
    QLabel* m_detailPath = nullptr;
    QLabel* m_onboarding = nullptr;
    QComboBox* m_sort = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_open = nullptr;
    QPushButton* m_locate = nullptr;
    QPushButton* m_favorite = nullptr;
    QPushButton* m_duplicate = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_showFolder = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_delete = nullptr;
};

} // namespace ui
