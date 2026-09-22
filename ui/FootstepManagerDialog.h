#pragma once

#include <QDialog>

namespace core { class Editor; }
class QListWidget;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QTableWidget;

namespace ui {

/// Editor central das superfícies de passo. Tiles/Terrain/Eventos apontam para
/// IDs estáveis daqui; nenhum deles duplica caminhos de áudio.
class FootstepManagerDialog final : public QDialog
{
public:
    explicit FootstepManagerDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    void refreshSurfaces(const QString& selectId = QString());
    void loadCurrent();
    void saveCurrent();
    void refreshSoundTable();
    void refreshTerrainTable();
    void refreshWangTable();
    void markChanged();
    QString currentSurfaceId() const;

    core::Editor& ed;
    int m_current = -1;
    bool m_loading = false;
    QListWidget* m_surfaces = nullptr;
    QLineEdit* m_name = nullptr;
    QSpinBox* m_volume = nullptr;
    QSpinBox* m_pitchMin = nullptr;
    QSpinBox* m_pitchMax = nullptr;
    QCheckBox* m_noRepeat = nullptr;
    QTableWidget* m_sounds = nullptr;
    QComboBox* m_defaultSurface = nullptr;
    QTableWidget* m_terrain = nullptr;
    QTableWidget* m_wang = nullptr;
};

} // namespace ui
