#pragma once

#include "core/Editor.h"
#include "core/TilesetOps.h"

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QVector>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QSpinBox;
class QTimer;
QT_END_NAMESPACE

namespace ui {

class TilesetPreview;

class NewTilesetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewTilesetDialog(core::Editor& ed, QWidget* parent = nullptr);
    core::Tileset result() const { return m_result; }
private:
    void pickImage();
    void updatePreview();
    QImage processedImage() const;
    void setChromaColor(const QColor& c);
    void updateChromaUi();
    core::Editor& ed;
    QImage m_image;
    QString m_path;
    core::Tileset m_result;
    QLineEdit* m_name = nullptr;
    QSpinBox *m_tw = nullptr, *m_th = nullptr, *m_spacing = nullptr, *m_margin = nullptr;
    QLabel* m_info = nullptr;
    TilesetPreview* m_preview = nullptr;
    QPushButton* m_okBtn = nullptr;
    QCheckBox* m_chromaOn = nullptr;
    QPushButton* m_chromaSwatch = nullptr;
    QPushButton* m_chromaPick = nullptr;
    QPushButton* m_chromaCorner = nullptr;
    QSpinBox* m_chromaTol = nullptr;
    QLabel* m_chromaInfo = nullptr;
    QColor m_chromaColor;
};

class GenerateAutotileDialog : public QDialog
{
    Q_OBJECT
public:
    GenerateAutotileDialog(core::Editor& ed, int srcTilesetIdx, int tx, int ty, int cols, int rows, QWidget* parent = nullptr);
private:
    void rebuildAtlas();
    void doCreateAutotile();
    void doSavePng();
    core::Editor& ed;
    int m_srcIdx, m_tx, m_ty, m_cols, m_rows;
    core::autotile::Converter conv;
    QImage m_atlas;
    QComboBox* m_mode = nullptr;
    QLabel *m_srcPreview = nullptr, *m_outPreview = nullptr;
    QLabel* m_info = nullptr;
};

class CombinedTilesetDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { CreateTileset, AddImage };
    explicit CombinedTilesetDialog(core::Editor& ed, QWidget* parent = nullptr,
                                   Mode mode = Mode::CreateTileset,
                                   int fixedTileWidth = 0, int fixedTileHeight = 0);
    core::Tileset result() const { return m_result; }
private:
    void addImages();
    void updateInfo();
    QVector<core::NamedImage> processedImages() const;
    void setChromaColor(const QColor& c);
    void updateChromaUi();
    core::Editor& ed;
    Mode m_mode = Mode::CreateTileset;
    QVector<core::NamedImage> m_images;
    core::Tileset m_result;
    QListWidget* m_list = nullptr;
    QLineEdit* m_name = nullptr;
    QSpinBox *m_tw = nullptr, *m_th = nullptr;
    QComboBox* m_direction = nullptr;
    QComboBox* m_tilesetCategory = nullptr;
    QLabel* m_info = nullptr;
    TilesetPreview* m_preview = nullptr;
    QCheckBox* m_chromaOn = nullptr;
    QPushButton* m_chromaSwatch = nullptr;
    QPushButton* m_chromaPick = nullptr;
    QCheckBox* m_chromaPerImage = nullptr;
    QSpinBox* m_chromaTol = nullptr;
    QLabel* m_chromaInfo = nullptr;
    QColor m_chromaColor;
};

class AtcInputView : public QWidget
{
    Q_OBJECT
public:
    explicit AtcInputView(core::autotile::Converter& conv, QWidget* parent = nullptr);
    QSize sizeHint() const override;
signals:
    void changed();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    core::autotile::Converter& conv;
};

class AutoTileConverterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AutoTileConverterDialog(core::Editor& ed, QWidget* parent = nullptr);
private:
    void pickImage();
    void refreshAll();
    void refreshOutputPreview();
    void updateSourceFrame(bool clearSelectionIfGeometryChanges = true);
    QVector<QImage> convertedAnimationFrames(QString* error = nullptr) const;
    QImage packedAnimationOutput(QVector<QPoint>* relativeFrameOrigins = nullptr, QString* error = nullptr) const;
    QString registerAnimation(core::Tileset& ts, int baseCol, int baseRow, const QVector<QPoint>& relativeOrigins, const QSize& frameTiles);
    void savePng();
    void importAutotiles();
    core::Editor& ed;
    core::autotile::Converter conv;
    QImage m_sourceImage;
    AtcInputView* m_input = nullptr;
    QLabel* m_output = nullptr;
    QLabel* m_info = nullptr;
    QSpinBox* m_tileSize = nullptr;
    QComboBox* m_tileSizePreset = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_category = nullptr;
    QCheckBox* m_animated = nullptr;
    QSpinBox* m_frameCount = nullptr;
    QComboBox* m_frameAxis = nullptr;
    QDoubleSpinBox* m_animationFps = nullptr;
    QCheckBox* m_animationLoop = nullptr;
    QCheckBox* m_animationPingPong = nullptr;
    QComboBox* m_animationSync = nullptr;
    QTimer* m_animationPreviewTimer = nullptr;
    qint64 m_animationPreviewMs = 0;
};

} // namespace ui
