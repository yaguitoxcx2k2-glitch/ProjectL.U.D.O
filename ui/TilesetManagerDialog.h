#pragma once

#include <QDialog>
#include <QStringList>

namespace core { class Editor; }
class QLabel;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QTabWidget;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QSpinBox;
class QWidget;
class QAction;

namespace ui {

class TilesetView;
class WangEditorWidget;

/// Centraliza toda a administracao de tilesets. O combo da paleta fica apenas
/// para SELECIONAR; criar, renomear, duplicar, importar e excluir vivem aqui.
class TilesetManagerDialog final : public QDialog
{
public:
    explicit TilesetManagerDialog(core::Editor& ed, QWidget* parent = nullptr);

    // Fluxos centrais reutilizados pelo menu principal, atalhos e Gerenciador.
    // Retornam true somente quando o projeto foi alterado.
    static bool runCreateTilesetFlow(core::Editor& ed, QWidget* parent, int* selectedTilesetIndex = nullptr);
    static bool runAddImageFlow(core::Editor& ed, QWidget* parent, int representativeTilesetIndex, int* selectedTilesetIndex = nullptr);
    static bool runImportAutotileFlow(core::Editor& ed, QWidget* parent, QString* createdAutotileId = nullptr);
    static bool runImportA1Flow(core::Editor& ed, QWidget* parent, QString* firstAutotileId = nullptr);

private:
    void refresh(int select = -1);
    void refreshPreview();
    void refreshAutotiles(const QString& selectId = QString());
    void refreshAutotilePanel();
    QString selectedAutotileId() const;
    int selectedAutotileOwnerIdx() const;
    int selectedTilesetIndex() const;
    bool selectedTilesetIsPage() const;
    void renameAutotile();
    void deleteAutotile();
    void createTileset();
    void importIntoTileset(int preferredRepresentative);
    void importAutotile();
    void importA1Sheet();
    void reduceTilesetSize();
    void renameTileset();
    void duplicateTileset();
    void updateSourceImage();
    void editAnimations(const QString& autotileId = QString());
    void deleteTileset();
    void deleteTilesetPage();
    QStringList mapsUsingTileset(int tilesetIdx) const;

    core::Editor& ed;
    QTreeWidget* m_list = nullptr;
    QListWidget* m_autotileList = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_autotileDetails = nullptr;
    QLabel* m_info = nullptr;
    QTabWidget* m_tabs = nullptr;
    TilesetView* m_terrainView = nullptr;
    WangEditorWidget* m_terrainEditor = nullptr;
    QLabel* m_terrainStatus = nullptr;
    int m_terrainTabIndex = -1;
    TilesetView* m_priorityView = nullptr;
    TilesetView* m_effectsView = nullptr;
    QLabel* m_effectsStatus = nullptr;
    QComboBox* m_tileReflectionPreset = nullptr;
    QSpinBox*  m_tileReflectionOpacity = nullptr;
    QComboBox* m_tileReflectionBlurMode = nullptr;
    QSpinBox*  m_tileReflectionBlurStrength = nullptr;
    QLabel* m_priorityStatus = nullptr;
    QComboBox* m_priorityCollisionMode = nullptr;
    QWidget* m_priorityControls = nullptr;
    QWidget* m_collisionControls = nullptr;
    QPushButton* m_renameAutotile = nullptr;
    QPushButton* m_deleteAutotile = nullptr;
    QAction* m_editAutotileAnimationAction = nullptr;
    QPushButton* m_openTerrainEditor = nullptr;
    QCheckBox* m_autotileBoundary = nullptr;
    QLineEdit* m_autotileCategory = nullptr;
    QComboBox* m_autotilePriority = nullptr;
    QComboBox* m_autotileCollision = nullptr;
    QComboBox* m_autotileReflectionPreset = nullptr;
    QSpinBox*  m_autotileReflectionOpacity = nullptr;
    QComboBox* m_autotileReflectionBlurMode = nullptr;
    QSpinBox*  m_autotileReflectionBlurStrength = nullptr;
};

} // namespace ui
