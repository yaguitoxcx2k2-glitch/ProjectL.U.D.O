// ============================================================================
//  WangEditorWidget.h — Editor de Wang Tiles / terrenos.
//  Reune o que na versao web estava espalhado por #wangPropGroup,
//  #uxWangLargeEditor (editor grande 3x3), a galeria de presets e o
//  diagnostico de cobertura (mascaras faltando / duplicadas).
// ============================================================================
#pragma once

#include "core/Editor.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QCheckBox;
class QProgressBar;
QT_END_NAMESPACE

namespace ui {

/// Editor grande de um tile: 3x3 clicavel (cantos/bordas) com o tile ao fundo.
class WangTileCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit WangTileCanvas(core::Editor& ed, QWidget* parent = nullptr);
    void setTile(int tilesetIdx, int tx, int ty);
    QSize sizeHint() const override { return QSize(240, 240); }
signals:
    void positionClicked(const QString& pos);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    QString posAt(const QPoint& p) const;
    core::Editor& ed;
    int m_tilesetIdx = -1, m_tx = -1, m_ty = -1;
};

class WangEditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WangEditorWidget(core::Editor& ed, QWidget* parent = nullptr);

    void refresh();
    bool editMode() const;
    /// Define o Autotile dono do contexto Terrain/Wang. O widget deixa de ser
    /// uma propriedade global e passa a editar apenas o recurso selecionado.
    void setAutotileContext(int tilesetIdx, const QString& autotileId);
    void clearAutotileContext();
    bool hasAutotileContext() const;
    /// Aplica a cor ativa numa posicao do tile indicado (clique na paleta).
    void applyLabel(int tilesetIdx, int tx, int ty, const QString& position);

signals:
    void statusMessage(const QString& text);
    void editModeChanged(bool on);
    void requestLocateTile(int tilesetIdx, int tx, int ty);

private:
    void rebuildSetCombo();
    QPixmap tileIcon(int tilesetIdx, int tx, int ty, int size) const;
    /// Porte de saveWangPreset(): cria um preset com os rótulos da cor ativa
    /// encontrados dentro do bloco selecionado na paleta.
    void savePresetFromSelection();
    void deleteSelectedPreset();
    void rebuildColorList();
    void updateDiagnostics();
    const core::TilesetAutotile* contextAutotile() const;
    core::WangSet* set() { return ed.activeWangSet(); }

    core::Editor& ed;
    QLabel*       m_contextLabel = nullptr;
    QWidget*      m_contextActions = nullptr;
    QPushButton*  m_createTerrain = nullptr;
    QPushButton*  m_bindTerrain = nullptr;
    QPushButton*  m_clearTerrain = nullptr;
    QComboBox*    m_setCombo = nullptr;
    QComboBox*    m_setType = nullptr;
    QListWidget*  m_colorList = nullptr;
    QComboBox*    m_presetCombo = nullptr;
    QCheckBox*    m_editMode = nullptr;
    QCheckBox*    m_eraseMode = nullptr;
    QCheckBox*    m_manualMode = nullptr;
    QProgressBar* m_coverage = nullptr;
    QLabel*       m_diagLabel = nullptr;
    QLabel*       m_selectedTileLabel = nullptr;
    WangTileCanvas* m_canvas = nullptr;
    int m_contextTilesetIdx = -1;
    QString m_contextAutotileId;
    bool m_updating = false;
};

} // namespace ui
