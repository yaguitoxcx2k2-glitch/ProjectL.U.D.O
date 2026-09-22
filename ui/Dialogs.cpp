#include "Dialogs.h"
#include "EventCommandClipboard.h"
#include "EventEditorModel.h"

#include "AssetBrowser.h"
#include "AudioPicker.h"
#include "CommandCatalog.h"
#include "CommandPresetStore.h"
#include "PluginCommandComposer.h"
#include "CommandPreviewDialog.h"
#include "CommandInspector.h"
#include "PreviewFramework.h"
#include "MainWindow.h"
#include "NarrativePreviewWidget.h"
#include "ConditionBuilderDialog.h"
#include "MapPreviewRenderer.h"
#include "MoveRouteDialog.h"
#include "PictureDialogs.h"
#include "ScreenToneDialog.h"
#include "RpgBattleDesignDialog.h"
#include "TextEffectsEditorWidget.h"
#include "Icons.h"

#include "core/ProjectIO.h"
#include "core/EventCommandCodec.h"
#include "core/EventCommandValidator.h"
#include "core/CommandWorkflow.h"
#include "core/ProjectValidator.h"
#include "core/CommonEventPackage.h"
#include "core/ProjectReferenceIndex.h"
#include "core/GameValueRegistry.h"
#include "core/ConditionTree.h"
#include "core/LudoCommandSystem.h"
#include "core/FilterSystem.h"
#include "core/AutoTileTables.h"
#include "core/TilesetCatalog.h"
#include "core/Wang.h"
#include "core/Renderer.h"
#include "core/Weather.h"

#include "game/Subtitle.h"
#include "game/TextBox.h"
#include "game/TextDraw.h"
#include "game/GameWorld.h"
#include "game/BattleRules.h"
#include "game/ui/UiTheme.h"
#include "game/ui/UiStyleResolver.h"
#include "game/ui/UiPainterRenderer.h"
#include "game/ui/GameUiLayer.h"

#include <QCheckBox>
#include <QBrush>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QHeaderView>
#include <QHash>
#include <QMenu>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QIcon>
#include <QElapsedTimer>
#include <QEvent>
#include <QScrollArea>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTimer>
#include <functional>
#include <memory>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QApplication>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QShortcut>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <cmath>
#include <QUuid>
#include <optional>
#include <utility>

using namespace core;

namespace ui {
static int chooseInputIcon(core::Editor& ed,int current,QWidget* parent);
static QIcon inputIconPreview(const core::Editor& ed,int index);

// --------------------------------------------------------- TilesetPreview
/// Mostra a imagem com a grade sobreposta (equivalente ao .preview-box do HTML).
class TilesetPreview : public QWidget
{
public:
    explicit TilesetPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(160);
        setStyleSheet(QStringLiteral("background:#1a1a1a"));
    }
    void setImage(const QImage& img) { m_img = img; update(); }
    void setGrid(int tw, int th, int spacing, int margin)
    {
        m_tw = tw; m_th = th; m_spacing = spacing; m_margin = margin;
        update();
    }
    /// Xadrez atras da imagem, para enxergar o que ficou transparente.
    void setCheckerboard(bool on) { m_checker = on; update(); }
    /// Modo conta-gotas: o proximo clique devolve a cor do pixel pelo callback.
    void setEyedropper(bool on)
    {
        m_eyedropper = on;
        setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    }
    bool eyedropper() const { return m_eyedropper; }
    std::function<void(const QColor&)> onPick;

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (!m_eyedropper || m_img.isNull() || !m_dst.contains(e->pos())) return;
        const double fx = (e->position().x() - m_dst.x()) / m_dst.width();
        const double fy = (e->position().y() - m_dst.y()) / m_dst.height();
        const int px = qBound(0, int(fx * m_img.width()),  m_img.width()  - 1);
        const int py = qBound(0, int(fy * m_img.height()), m_img.height() - 1);
        setEyedropper(false);
        if (onPick) onPick(m_img.pixelColor(px, py));
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#1a1a1a"));
        if (m_img.isNull()) {
            p.setPen(QColor("#666"));
            p.drawText(rect(), Qt::AlignCenter, tr("(sem imagem)"));
            return;
        }
        const double s = qMin(double(width()) / m_img.width(), double(height()) / m_img.height());
        const QRectF dst((width() - m_img.width() * s) / 2, (height() - m_img.height() * s) / 2,
                         m_img.width() * s, m_img.height() * s);
        m_dst = dst;
        if (m_checker) {
            const int c = 8;
            for (int y = 0; y * c < dst.height(); ++y)
                for (int x = 0; x * c < dst.width(); ++x) {
                    const QRectF cell(dst.x() + x * c, dst.y() + y * c,
                                      qMin(double(c), dst.right() - (dst.x() + x * c)),
                                      qMin(double(c), dst.bottom() - (dst.y() + y * c)));
                    p.fillRect(cell, ((x + y) % 2) ? QColor("#3a3a3a") : QColor("#2c2c2c"));
                }
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(dst, m_img);
        if (m_tw > 0 && m_th > 0 && m_tw * s > 3) {
            p.setPen(QPen(QColor(255, 255, 255, 50), 1));
            for (double x = m_margin; x <= m_img.width(); x += m_tw + m_spacing)
                p.drawLine(QPointF(dst.x() + x * s, dst.top()), QPointF(dst.x() + x * s, dst.bottom()));
            for (double y = m_margin; y <= m_img.height(); y += m_th + m_spacing)
                p.drawLine(QPointF(dst.left(), dst.y() + y * s), QPointF(dst.right(), dst.y() + y * s));
        }
    }
private:
    QImage m_img;
    int m_tw = 0, m_th = 0, m_spacing = 0, m_margin = 0;
    bool   m_checker = false;
    bool   m_eyedropper = false;
    QRectF m_dst;
};

// --------------------------------------- destino visual de teletransporte
/// Preview clicavel de qualquer mapa do projeto. Renderiza o MapDoc indicado
/// diretamente, portanto abrir este seletor nunca troca a aba ativa do editor.
class MapDestinationPreview : public QWidget
{
public:
    explicit MapDestinationPreview(const Editor& editorRef, QWidget* parent = nullptr)
        : QWidget(parent), ed(editorRef)
    {
        setMinimumSize(420, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(tr("Clique ou arraste para escolher o tile de chegada."));
    }

    QSize sizeHint() const override { return QSize(640, 390); }

    void setMapId(const QString& id)
    {
        if (m_mapId == id && !m_cache.isNull()) return;
        m_mapId = id;
        m_cache = QImage();
        m_mapArea = QRectF();
        clampSelection();
        update();
    }

    void setSelection(const QPoint& cell)
    {
        m_selection = cell;
        clampSelection();
        update();
    }

    QPoint selection() const { return m_selection; }

    void setMarkerLabel(const QString& label)
    {
        m_markerLabel = label;
        update();
    }

    std::function<void(const QPoint&)> onCellPicked;

protected:
    void resizeEvent(QResizeEvent*) override
    {
        m_cache = QImage();
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        pickAt(event->position());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) return;
        pickAt(event->position());
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#15171b"));

        const MapDoc* doc = ed.mapById(m_mapId);
        if (!doc || doc->map.width <= 0 || doc->map.height <= 0) {
            painter.setPen(QColor("#8b9098"));
            painter.drawText(rect(), Qt::AlignCenter, tr("Mapa de destino indisponível"));
            return;
        }

        m_mapArea = fittedMapArea(doc->map);
        if (m_mapArea.isEmpty()) return;
        if (m_cache.isNull()) {
            m_cache = renderMapPreview(ed, *doc, m_mapArea.size().toSize());
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(m_mapArea, m_cache);
        painter.setPen(QPen(QColor(0, 0, 0, 190), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_mapArea);

        const double cellW = m_mapArea.width() / doc->map.width;
        const double cellH = m_mapArea.height() / doc->map.height;
        if (qMin(cellW, cellH) >= 8.0) {
            painter.setPen(QPen(QColor(255, 255, 255, 35), 1));
            for (int column = 1; column < doc->map.width; ++column) {
                const double px = m_mapArea.left() + column * cellW;
                painter.drawLine(QPointF(px, m_mapArea.top()), QPointF(px, m_mapArea.bottom()));
            }
            for (int row = 1; row < doc->map.height; ++row) {
                const double py = m_mapArea.top() + row * cellH;
                painter.drawLine(QPointF(m_mapArea.left(), py), QPointF(m_mapArea.right(), py));
            }
        }

        QPoint marker = m_selection;
        QColor markerColor("#45a9ff");
        marker.setX(qBound(0, marker.x(), doc->map.width - 1));
        marker.setY(qBound(0, marker.y(), doc->map.height - 1));
        const QRectF markerRect(m_mapArea.left() + marker.x() * cellW,
                                m_mapArea.top() + marker.y() * cellH, cellW, cellH);
        painter.setPen(QPen(markerColor.lighter(145), 2));
        painter.setBrush(QColor(markerColor.red(), markerColor.green(), markerColor.blue(), 95));
        painter.drawRect(markerRect.adjusted(1, 1, -1, -1));
        painter.setBrush(markerColor);
        painter.setPen(Qt::NoPen);
        const QPointF center = markerRect.center();
        const double radius = qBound(3.0, qMin(cellW, cellH) * 0.20, 9.0);
        painter.drawEllipse(center, radius, radius);

        painter.setPen(QColor("#e6e9ee"));
        painter.drawText(QRectF(8, height() - 22, width() - 16, 18),
                         Qt::AlignCenter,
                         tr("%1: (%2, %3)").arg(m_markerLabel)
                             .arg(marker.x()).arg(marker.y()));
    }

private:
    QRectF fittedMapArea(const MapInfo& info) const
    {
        const QRectF available = QRectF(rect()).adjusted(8, 8, -8, -30);
        if (available.isEmpty()) return QRectF();
        const double scale = qMin(available.width() / qMax(1, info.pixelWidth()),
                                  available.height() / qMax(1, info.pixelHeight()));
        const QSizeF size(info.pixelWidth() * scale, info.pixelHeight() * scale);
        return QRectF(available.center() - QPointF(size.width() / 2, size.height() / 2), size);
    }

    void clampSelection()
    {
        if (const MapDoc* doc = ed.mapById(m_mapId)) {
            m_selection.setX(qBound(0, m_selection.x(), qMax(0, doc->map.width - 1)));
            m_selection.setY(qBound(0, m_selection.y(), qMax(0, doc->map.height - 1)));
        }
    }

    void pickAt(const QPointF& position)
    {
        if (!m_mapArea.contains(position)) return;
        const MapDoc* doc = ed.mapById(m_mapId);
        if (!doc) return;
        const double fx = (position.x() - m_mapArea.left()) / m_mapArea.width();
        const double fy = (position.y() - m_mapArea.top()) / m_mapArea.height();
        const QPoint cell(qBound(0, int(fx * doc->map.width), doc->map.width - 1),
                          qBound(0, int(fy * doc->map.height), doc->map.height - 1));
        if (cell == m_selection) return;
        m_selection = cell;
        update();
        if (onCellPicked) onCellPicked(cell);
    }

    const Editor& ed;
    QString m_mapId;
    QString m_markerLabel = tr("destino");
    QPoint m_selection;
    QRectF m_mapArea;
    QImage m_cache;
};

StartPositionDialog::StartPositionDialog(const Editor& editorRef, const QString& mapId,
                                         const QPoint& position, QWidget* parent)
    : QDialog(parent), ed(editorRef), m_position(position)
{
    setWindowTitle(tr("Início do jogo"));
    resize(760, 680);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_map = new QComboBox(this);
    for (const MapDoc& map : ed.docs) m_map->addItem(map.name, map.id);
    int initialIndex = m_map->findData(mapId);
    if (initialIndex < 0) initialIndex = 0;
    m_map->setCurrentIndex(initialIndex);
    form->addRow(tr("Mapa inicial"), m_map);
    m_location = new QLabel(this);
    m_location->setStyleSheet(QStringLiteral("font-weight:600;color:#dfe8f5"));
    form->addRow(tr("Posição do jogador"), m_location);
    layout->addLayout(form);
    auto* hint = new QLabel(tr("Clique ou arraste sobre a prévia. Este ponto será usado ao iniciar um novo jogo e em Testar Jogo (F6)."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    layout->addWidget(hint);
    m_preview = new MapDestinationPreview(ed, this);
    m_preview->setMarkerLabel(tr("início"));
    layout->addWidget(m_preview, 1);

    const auto refresh = [this] {
        m_preview->setMapId(selectedMapId());
        m_preview->setSelection(m_position);
        m_position = m_preview->selection();
        m_location->setText(tr("Posição (%1, %2)").arg(m_position.x()).arg(m_position.y()));
    };
    m_preview->onCellPicked = [this](const QPoint& cell) {
        m_position = cell;
        m_location->setText(tr("Posição (%1, %2)").arg(cell.x()).arg(cell.y()));
    };
    connect(m_map, &QComboBox::currentIndexChanged, this, [refresh](int) { refresh(); });
    refresh();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString StartPositionDialog::selectedMapId() const
{
    return m_map ? m_map->currentData().toString() : QString();
}

// ------------------------------------------------------- NewTilesetDialog
NewTilesetDialog::NewTilesetDialog(Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Novo tileset"));
    resize(560, 620);
    auto* v = new QVBoxLayout(this);

    auto* pick = new QPushButton(tr("Escolher imagem…"), this);
    v->addWidget(pick);
    auto* hint = new QLabel(tr("PNG com transparência recomendado. Atlas 512×512 do atlas 512×512 "
                               "é detectado automaticamente."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    m_preview = new TilesetPreview(this);
    v->addWidget(m_preview, 1);

    auto* form = new QFormLayout;
    m_name = new QLineEdit(tr("Tileset %1").arg(ed.tilesets.size() + 1), this);
    const int defaultTileSize = core::rpgMakerDefaultAuthoringTileSize(ed.rpgMakerEngine);
    m_tw = new QSpinBox(this); m_tw->setRange(1, 512); m_tw->setValue(defaultTileSize);
    m_th = new QSpinBox(this); m_th->setRange(1, 512); m_th->setValue(defaultTileSize);
    m_spacing = new QSpinBox(this); m_spacing->setRange(0, 64);
    m_margin = new QSpinBox(this); m_margin->setRange(0, 64);
    form->addRow(tr("Nome"), m_name);
    form->addRow(tr("Tile W (px)"), m_tw);
    form->addRow(tr("Tile H (px)"), m_th);
    form->addRow(tr("Espaçamento"), m_spacing);
    form->addRow(tr("Margem"), m_margin);
    v->addLayout(form);

    // ---- cor de transparência (chroma key) -------------------------------
    auto* chromaBox = new QGroupBox(tr("Cor de transparência"), this);
    auto* cv = new QVBoxLayout(chromaBox);
    cv->setContentsMargins(8, 8, 8, 8);
    auto* chromaHint = new QLabel(tr("Para sheets antigas que vêm com fundo sólido (magenta, ciano…) "
                                     "em vez de canal alfa."), chromaBox);
    chromaHint->setWordWrap(true);
    chromaHint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    cv->addWidget(chromaHint);

    m_chromaOn = new QCheckBox(tr("Remover uma cor de fundo"), chromaBox);
    cv->addWidget(m_chromaOn);

    auto* row1 = new QHBoxLayout;
    m_chromaSwatch = new QPushButton(tr("Escolher cor…"), chromaBox);
    m_chromaSwatch->setToolTip(tr("Abre o seletor de cores."));
    m_chromaPick = new QPushButton(icons::get(QStringLiteral("eyedropper")), tr("Conta-gotas"), chromaBox);
    m_chromaPick->setCheckable(true);
    m_chromaPick->setToolTip(tr("Clique aqui e depois no fundo da imagem, na prévia acima."));
    m_chromaCorner = new QPushButton(tr("Pixel (0,0)"), chromaBox);
    m_chromaCorner->setToolTip(tr("Usa a cor do canto superior-esquerdo — a convenção mais comum."));
    row1->addWidget(m_chromaSwatch, 1);
    row1->addWidget(m_chromaPick);
    row1->addWidget(m_chromaCorner);
    cv->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(tr("Tolerância:"), chromaBox));
    m_chromaTol = new QSpinBox(chromaBox);
    m_chromaTol->setRange(0, 255);
    m_chromaTol->setValue(0);
    m_chromaTol->setToolTip(tr("0 = só a cor exata. Aumente se sobrar uma franja em volta dos "
                               "sprites (imagens com anti-aliasing ou salvas em JPEG)."));
    row2->addWidget(m_chromaTol);
    row2->addStretch(1);
    cv->addLayout(row2);

    m_chromaInfo = new QLabel(chromaBox);
    m_chromaInfo->setWordWrap(true);
    m_chromaInfo->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    cv->addWidget(m_chromaInfo);
    v->addWidget(chromaBox);

    connect(m_chromaOn, &QCheckBox::toggled, this, [this](bool) { updateChromaUi(); updatePreview(); });
    connect(m_chromaTol, &QSpinBox::valueChanged, this, [this](int) { updatePreview(); });
    connect(m_chromaSwatch, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_chromaColor.isValid() ? m_chromaColor : QColor("#ff00ff"),
                                                this, tr("Cor de transparência"));
        if (c.isValid()) setChromaColor(c);
    });
    connect(m_chromaCorner, &QPushButton::clicked, this, [this] {
        if (m_image.isNull()) return;
        setChromaColor(m_image.pixelColor(0, 0));
    });
    connect(m_chromaPick, &QPushButton::toggled, this, [this](bool on) {
        m_preview->setEyedropper(on);
        if (on) m_chromaInfo->setText(tr("Clique no fundo da imagem, na prévia acima."));
    });
    m_preview->onPick = [this](const QColor& c) {
        m_chromaPick->setChecked(false);
        setChromaColor(c);
    };

    m_info = new QLabel(tr("Selecione uma imagem para começar."), this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(m_info);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(tr("Criar tileset"));
    m_okBtn = box->button(QDialogButtonBox::Ok);
    m_okBtn->setEnabled(false);
    v->addWidget(box);

    connect(pick, &QPushButton::clicked, this, &NewTilesetDialog::pickImage);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (m_image.isNull()) return;
        m_result = makeTileset(processedImage(), m_name->text(), m_tw->value(), m_th->value(),
                               m_spacing->value(), m_margin->value(), m_path);
        if (m_chromaOn->isChecked() && m_chromaColor.isValid()) {
            m_result.chromaApplied = true;
            m_result.chromaColor = m_chromaColor;
            m_result.chromaTolerance = m_chromaTol->value();
        }
        accept();
    });
    for (QSpinBox* s : { m_tw, m_th, m_spacing, m_margin })
        connect(s, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
}

void NewTilesetDialog::pickImage()
{
    const QString path = AssetBrowserDialog::chooseImage(ed, this, QStringLiteral("Tilesets"));
    if (path.isEmpty()) return;
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Erro"), tr("Não foi possível ler a imagem."));
        return;
    }
    m_image = img;
    m_path = ed.projectRelativePath(path);
    if (m_name->text().startsWith(tr("Tileset ")))
        m_name->setText(QFileInfo(path).completeBaseName());
    // Heuristica legada do VX (32 px). Nunca deve substituir a grade nativa
    // de um projeto MV, que permanece 48x48.
    if (core::rpgMakerDefaultAuthoringTileSize(ed.rpgMakerEngine) == 32 &&
        img.width() == 512 && img.height() == 512) {
        m_tw->setValue(32);
        m_th->setValue(32);
    }
    m_okBtn->setEnabled(true);
    // Sugestão: se o canto (0,0) for opaco, provavelmente é a cor de fundo.
    const QColor corner = img.pixelColor(0, 0);
    if (corner.alpha() == 255) {
        m_chromaColor = corner;
        m_chromaInfo->setText(tr("Sugestão: a cor do pixel (0,0) é %1. Marque a caixa para removê-la.")
                                  .arg(corner.name().toUpper()));
    }
    updateChromaUi();
    updatePreview();
}

QImage NewTilesetDialog::processedImage() const
{
    if (m_image.isNull() || !m_chromaOn->isChecked() || !m_chromaColor.isValid()) return m_image;
    QImage copy = m_image;
    applyChromaKey(copy, m_chromaColor, m_chromaTol->value());
    return copy;
}

void NewTilesetDialog::setChromaColor(const QColor& c)
{
    if (!c.isValid()) return;
    m_chromaColor = c;
    if (!m_chromaOn->isChecked()) m_chromaOn->setChecked(true);   // já liga ao escolher
    updateChromaUi();
    updatePreview();
}

void NewTilesetDialog::updateChromaUi()
{
    // Os botoes de escolher cor ficam sempre disponiveis quando ha imagem:
    // escolher uma cor JA liga a remocao (era confuso ter de marcar a caixa
    // antes de poder usar o conta-gotas).
    const bool on = m_chromaOn->isChecked();
    const bool hasImage = !m_image.isNull();
    m_chromaSwatch->setEnabled(hasImage);
    m_chromaPick->setEnabled(hasImage);
    m_chromaCorner->setEnabled(hasImage);
    m_chromaTol->setEnabled(on);
    m_preview->setCheckerboard(on);
    if (m_chromaColor.isValid()) {
        const bool dark = m_chromaColor.lightness() < 128;
        m_chromaSwatch->setStyleSheet(
            QStringLiteral("background:%1;color:%2").arg(m_chromaColor.name(),
                                                         dark ? "#fff" : "#000"));
        m_chromaSwatch->setText(m_chromaColor.name().toUpper());
    } else {
        m_chromaSwatch->setStyleSheet(QString());
        m_chromaSwatch->setText(tr("Escolher cor…"));
    }
}

void NewTilesetDialog::updatePreview()
{
    const QImage shown = processedImage();
    m_preview->setImage(shown);
    m_preview->setGrid(m_tw->value(), m_th->value(), m_spacing->value(), m_margin->value());
    if (m_image.isNull()) return;

    const Tileset t = makeTileset(shown, m_name->text(), m_tw->value(), m_th->value(),
                                  m_spacing->value(), m_margin->value());
    m_info->setText(tr("Imagem %1×%2 px → grade %3 × %4 = %5 tiles%6")
                        .arg(m_image.width()).arg(m_image.height())
                        .arg(t.columns).arg(t.rows).arg(t.tilecount)
                        .arg(t.isVX512 ? tr("\n⚠ Detectado atlas 512×512 (atlas 512×512).") : QString()));

    if (!m_chromaOn->isChecked()) {
        m_chromaInfo->setText(m_image.isNull() ? QString()
                                               : tr("Desligado — a imagem entra como está."));
        return;
    }
    if (!m_chromaColor.isValid()) {
        m_chromaInfo->setText(tr("Escolha a cor do fundo (conta-gotas, pixel (0,0) ou seletor)."));
        return;
    }
    const int n = countChromaKeyPixels(m_image, m_chromaColor, m_chromaTol->value());
    const double total = double(m_image.width()) * m_image.height();
    m_chromaInfo->setText(tr("Pixels que ficariam transparentes: %1 — %2% da imagem.")
                              .arg(n).arg(total > 0 ? n * 100.0 / total : 0, 0, 'f', 1));
}

// -------------------------------------------------- CombinedTilesetDialog
CombinedTilesetDialog::CombinedTilesetDialog(Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Novo Tileset"));
    resize(600, 680);
    auto* v = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Crie um tileset a partir de uma ou mais imagens. Cada imagem entra como "
                               "<b>bloco inteiro</b>. Depois de criado, novas partes podem ser posicionadas "
                               "manualmente no Gerenciador de Tilesets."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    auto* pick = new QPushButton(icons::get(QStringLiteral("add")), tr("Escolher imagens…"), this);
    v->addWidget(pick);

    m_list = new QListWidget(this);
    m_list->setMaximumHeight(140);
    v->addWidget(m_list);

    m_preview = new TilesetPreview(this);
    v->addWidget(m_preview, 1);

    auto* form = new QFormLayout;
    m_name = new QLineEdit(tr("Novo Tileset"), this);
    const int defaultTileSize = core::rpgMakerDefaultAuthoringTileSize(ed.rpgMakerEngine);
    m_tw = new QSpinBox(this); m_tw->setRange(1, 512); m_tw->setValue(defaultTileSize);
    m_th = new QSpinBox(this); m_th->setRange(1, 512); m_th->setValue(defaultTileSize);
    m_direction = new QComboBox(this);
    m_direction->addItem(tr("Horizontal (lado a lado)"), QStringLiteral("horizontal"));
    m_direction->addItem(tr("Vertical (uma embaixo da outra)"), QStringLiteral("vertical"));
    form->addRow(tr("Nome"), m_name);
    form->addRow(tr("Tile W (px)"), m_tw);
    form->addRow(tr("Tile H (px)"), m_th);
    form->addRow(tr("Direção"), m_direction);
    v->addLayout(form);

    // ---- cor de transparência ---------------------------------------------
    auto* chromaBox = new QGroupBox(tr("Cor de transparência"), this);
    auto* cv = new QVBoxLayout(chromaBox);
    cv->setContentsMargins(8, 8, 8, 8);
    m_chromaOn = new QCheckBox(tr("Remover uma cor de fundo das imagens"), chromaBox);
    cv->addWidget(m_chromaOn);

    auto* crow = new QHBoxLayout;
    m_chromaSwatch = new QPushButton(tr("Escolher cor…"), chromaBox);
    m_chromaPick = new QPushButton(icons::get(QStringLiteral("eyedropper")), tr("Conta-gotas"), chromaBox);
    m_chromaPick->setCheckable(true);
    m_chromaPick->setToolTip(tr("Clique aqui e depois no fundo, na prévia do atlas."));
    crow->addWidget(m_chromaSwatch, 1);
    crow->addWidget(m_chromaPick);
    cv->addLayout(crow);

    m_chromaPerImage = new QCheckBox(tr("Usar o pixel (0,0) de cada imagem"), chromaBox);
    m_chromaPerImage->setToolTip(tr("Cada imagem tem o próprio fundo removido pela cor do seu canto "
                                    "superior-esquerdo — útil quando uma vem com fundo magenta e "
                                    "outra com ciano."));
    cv->addWidget(m_chromaPerImage);

    auto* trow = new QHBoxLayout;
    trow->addWidget(new QLabel(tr("Tolerância:"), chromaBox));
    m_chromaTol = new QSpinBox(chromaBox);
    m_chromaTol->setRange(0, 255);
    m_chromaTol->setToolTip(tr("0 = só a cor exata. Aumente se sobrar franja em volta dos sprites."));
    trow->addWidget(m_chromaTol);
    trow->addStretch(1);
    cv->addLayout(trow);

    m_chromaInfo = new QLabel(chromaBox);
    m_chromaInfo->setWordWrap(true);
    m_chromaInfo->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    cv->addWidget(m_chromaInfo);
    v->addWidget(chromaBox);

    connect(m_chromaOn, &QCheckBox::toggled, this, [this](bool) { updateChromaUi(); updateInfo(); });
    connect(m_chromaPerImage, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !m_chromaOn->isChecked()) m_chromaOn->setChecked(true);
        updateChromaUi();
        updateInfo();
    });
    connect(m_chromaTol, &QSpinBox::valueChanged, this, [this](int) { updateInfo(); });
    connect(m_chromaSwatch, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_chromaColor.isValid() ? m_chromaColor : QColor("#ff00ff"),
                                                this, tr("Cor de transparência"));
        if (c.isValid()) setChromaColor(c);
    });
    connect(m_chromaPick, &QPushButton::toggled, this, [this](bool on) {
        m_preview->setEyedropper(on);
        if (on) m_chromaInfo->setText(tr("Clique no fundo, na prévia do atlas acima."));
    });
    m_preview->onPick = [this](const QColor& c) {
        m_chromaPick->setChecked(false);
        m_chromaPerImage->setChecked(false);
        setChromaColor(c);
    };

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(m_info);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(tr("Criar Tileset"));
    v->addWidget(box);

    connect(pick, &QPushButton::clicked, this, &CombinedTilesetDialog::addImages);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (m_images.isEmpty()) {
            QMessageBox::warning(this, tr("Atenção"), tr("Escolha ao menos uma imagem."));
            return;
        }
        m_result = makeCombinedTileset(processedImages(), m_name->text(),
                                       m_tw->value(), m_th->value(),
                                       m_direction->currentData().toString());
        if (m_chromaOn->isChecked()) {
            m_result.chromaApplied = true;
            m_result.chromaColor = m_chromaPerImage->isChecked() ? QColor() : m_chromaColor;
            m_result.chromaTolerance = m_chromaTol->value();
        }
        accept();
    });
    connect(m_tw, &QSpinBox::valueChanged, this, [this] { updateInfo(); });
    connect(m_th, &QSpinBox::valueChanged, this, [this] { updateInfo(); });
    connect(m_direction, &QComboBox::currentIndexChanged, this, [this] { updateInfo(); });
    updateInfo();
}

void CombinedTilesetDialog::addImages()
{
    const QStringList paths = AssetBrowserDialog::chooseImages(ed, this, QStringLiteral("Tilesets"));
    for (const QString& p : paths) {
        QImage img(p);
        if (img.isNull()) continue;
        m_images.push_back(NamedImage{ img, QFileInfo(p).completeBaseName() });
    }
    m_list->clear();
    for (const NamedImage& ni : m_images)
        m_list->addItem(tr("%1 — %2×%3 px").arg(ni.name).arg(ni.img.width()).arg(ni.img.height()));
    // Sugestão: se o canto da primeira imagem for opaco, provavelmente é o fundo.
    if (!m_chromaColor.isValid() && !m_images.isEmpty() && !m_images.first().img.isNull()) {
        const QColor corner = m_images.first().img.pixelColor(0, 0);
        if (corner.alpha() == 255) m_chromaColor = corner;
    }
    updateChromaUi();
    updateInfo();
}

QVector<NamedImage> CombinedTilesetDialog::processedImages() const
{
    if (!m_chromaOn->isChecked()) return m_images;
    QVector<NamedImage> out;
    out.reserve(m_images.size());
    for (const NamedImage& ni : m_images) {
        NamedImage copy = ni;
        // Ou a cor do canto de cada imagem, ou a mesma cor para todas.
        const QColor key = m_chromaPerImage->isChecked()
                               ? (copy.img.isNull() ? QColor() : copy.img.pixelColor(0, 0))
                               : m_chromaColor;
        if (key.isValid()) applyChromaKey(copy.img, key, m_chromaTol->value());
        out.push_back(copy);
    }
    return out;
}

void CombinedTilesetDialog::setChromaColor(const QColor& c)
{
    if (!c.isValid()) return;
    m_chromaColor = c;
    if (!m_chromaOn->isChecked()) m_chromaOn->setChecked(true);
    updateChromaUi();
    updateInfo();
}

void CombinedTilesetDialog::updateChromaUi()
{
    const bool on = m_chromaOn->isChecked();
    const bool perImage = m_chromaPerImage->isChecked();
    const bool hasImages = !m_images.isEmpty();
    // Como no diálogo de novo tileset: escolher a cor (ou marcar "por imagem")
    // JÁ liga a remoção — não faz sentido exigir a caixa antes.
    m_chromaSwatch->setEnabled(hasImages && !perImage);
    m_chromaPick->setEnabled(hasImages && !perImage);
    m_chromaPerImage->setEnabled(hasImages);
    m_chromaTol->setEnabled(on);
    m_preview->setCheckerboard(on);
    if (m_chromaColor.isValid() && !perImage) {
        const bool dark = m_chromaColor.lightness() < 128;
        m_chromaSwatch->setStyleSheet(QStringLiteral("background:%1;color:%2")
                                          .arg(m_chromaColor.name(), dark ? "#fff" : "#000"));
        m_chromaSwatch->setText(m_chromaColor.name().toUpper());
    } else {
        m_chromaSwatch->setStyleSheet(QString());
        m_chromaSwatch->setText(perImage ? tr("(por imagem)") : tr("Escolher cor…"));
    }
}

void CombinedTilesetDialog::updateInfo()
{
    if (m_images.isEmpty()) {
        m_info->setText(tr("Nenhuma imagem escolhida."));
        m_preview->setImage(QImage());
        if (m_chromaInfo) m_chromaInfo->clear();
        return;
    }
    const QVector<NamedImage> imgs = processedImages();
    const Tileset t = makeCombinedTileset(imgs, m_name->text(), m_tw->value(), m_th->value(),
                                          m_direction->currentData().toString());
    m_preview->setImage(t.image);
    m_preview->setGrid(t.tilewidth, t.tileheight, 0, 0);
    m_info->setText(tr("Imagens: %1 → atlas %2×%3 px · grade %4×%5 = %6 tiles")
                        .arg(m_images.size()).arg(t.imagewidth).arg(t.imageheight)
                        .arg(t.columns).arg(t.rows).arg(t.tilecount));

    if (!m_chromaInfo) return;
    if (!m_chromaOn->isChecked()) {
        m_chromaInfo->setText(tr("Desligado — as imagens entram como estão."));
        return;
    }
    if (m_chromaPerImage->isChecked()) {
        QStringList cores;
        for (const NamedImage& ni : m_images)
            if (!ni.img.isNull()) cores << ni.img.pixelColor(0, 0).name().toUpper();
        cores.removeDuplicates();
        m_chromaInfo->setText(tr("Cada imagem usa o próprio pixel (0,0) — cores detectadas: %1")
                                  .arg(cores.join(QStringLiteral(", "))));
        return;
    }
    if (!m_chromaColor.isValid()) {
        m_chromaInfo->setText(tr("Escolha a cor do fundo (conta-gotas ou seletor)."));
        return;
    }
    int total = 0;
    for (const NamedImage& ni : m_images)
        total += countChromaKeyPixels(ni.img, m_chromaColor, m_chromaTol->value());
    m_chromaInfo->setText(tr("Pixels que ficariam transparentes no conjunto: %1.").arg(total));
}

// ------------------------------------------------------------ AtcInputView
AtcInputView::AtcInputView(autotile::Converter& c, QWidget* parent)
    : QWidget(parent), conv(c)
{
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Clique nos blocos de autotile da imagem para marcá-los.\n"
                  "Clique de novo em um bloco marcado para removê-lo."));
}

QSize AtcInputView::sizeHint() const
{
    if (conv.img.isNull()) return QSize(320, 240);
    return QSize(qMax(320, conv.img.width()), qMax(240, conv.img.height()));
}

void AtcInputView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#141414"));
    if (conv.img.isNull()) {
        p.setPen(QColor("#666"));
        p.drawText(rect(), Qt::AlignCenter, tr("Escolha uma imagem para começar."));
        return;
    }

    const int ox = qMax(0, (width() - conv.img.width()) / 2);
    const int oy = qMax(0, (height() - conv.img.height()) / 2);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(ox, oy, conv.img);

    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    for (int x = 0; x <= conv.img.width(); x += conv.tileSize)
        p.drawLine(ox + x, oy, ox + x, oy + conv.img.height());
    for (int y = 0; y <= conv.img.height(); y += conv.tileSize)
        p.drawLine(ox, oy + y, ox + conv.img.width(), oy + y);

    for (const autotile::Selection& s : conv.selections) {
        QRect r = conv.selRect(s.x, s.y, s.mode);
        r.translate(ox, oy);
        QColor c = QColor::fromHsv(atc::hueForMode(s.mode), 200, 255);
        p.setPen(QPen(c, 2));
        c.setAlpha(60);
        p.setBrush(c);
        p.drawRect(r);
    }
}

void AtcInputView::mousePressEvent(QMouseEvent* e)
{
    if (conv.img.isNull()) return;
    const int ox = qMax(0, (width() - conv.img.width()) / 2);
    const int oy = qMax(0, (height() - conv.img.height()) / 2);
    const QPoint imagePos = e->pos() - QPoint(ox, oy);
    if (!QRect(QPoint(0, 0), conv.img.size()).contains(imagePos)) return;
    conv.toggleAt(imagePos.x(), imagePos.y());
    update();
    emit changed();
}

// -------------------------------------------------- AutoTileConverterDialog
AutoTileConverterDialog::AutoTileConverterDialog(Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Importar Autotile"));
    resize(940, 680);
    setMinimumSize(820, 600);
    auto* v = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Escolha a imagem, marque os blocos e importe. Cada bloco marcado vira um Autotile."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    // Duas áreas reais, sem coluna vazia entre configuração e prévia.
    // O divisor mantém a configuração compacta e entrega o espaço restante à imagem.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
    auto* controlsHost = new QWidget(splitter);
    auto* controls = new QVBoxLayout(controlsHost);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(6);
    auto* controlsScroll = new QScrollArea(this);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScroll->setMinimumWidth(330);
    controlsScroll->setWidget(controlsHost);
    splitter->addWidget(controlsScroll);

    auto* previewHost = new QWidget(splitter);
    previewHost->setMinimumWidth(380);
    auto* previews = new QVBoxLayout(previewHost);
    previews->setContentsMargins(0, 0, 0, 0);
    previews->setSpacing(6);
    splitter->addWidget(previewHost);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>{350, 570});
    v->addWidget(splitter, 1);

    auto* pick = new QPushButton(tr("Escolher imagem…"), controlsHost);
    controls->addWidget(pick);

    auto* form = new QFormLayout;
    m_tileSize = new QSpinBox(this);
    m_tileSize->setRange(2, 512);
    m_tileSize->setSingleStep(1);
    const int defaultTileSize = core::rpgMakerDefaultAuthoringTileSize(ed.rpgMakerEngine);
    // O conversor possui estado proprio. Inicialize o modelo junto com a UI:
    // apenas mudar o QSpinBox nao emite valueChanged durante a construcao.
    conv.tileSize = defaultTileSize;
    m_tileSize->setValue(defaultTileSize);
    m_tileSizePreset = new QComboBox(this);
    m_tileSizePreset->addItem(tr("16 px"), 16);
    m_tileSizePreset->addItem(tr("32 px"), 32);
    m_tileSizePreset->addItem(tr("48 px"), 48);
    m_tileSizePreset->addItem(tr("64 px"), 64);
    m_tileSizePreset->addItem(tr("Personalizado"), -1);
    m_tileSizePreset->setCurrentIndex(m_tileSizePreset->findData(defaultTileSize));
    auto* tileSizeRow = new QWidget(this);
    auto* tileSizeLayout = new QHBoxLayout(tileSizeRow);
    tileSizeLayout->setContentsMargins(0, 0, 0, 0);
    tileSizeLayout->setSpacing(6);
    tileSizeLayout->addWidget(m_tileSizePreset);
    tileSizeLayout->addWidget(m_tileSize, 1);
    m_name = new QLineEdit(QStringLiteral("autotile"), this);
    m_category = new QComboBox(this);
    m_category->setEditable(true);
    m_category->setInsertPolicy(QComboBox::NoInsert);
    m_category->setPlaceholderText(tr("Ex.: Água, Natureza, Caminhos"));
    QStringList categories;
    for (const TilesetAutotile& autotile : ed.autotiles) {
        const QString category = autotile.category.trimmed();
        if (!category.isEmpty() && !categories.contains(category, Qt::CaseInsensitive)) categories << category;
    }
    categories.sort(Qt::CaseInsensitive);
    m_category->addItems(categories);
    m_category->setCurrentIndex(-1);
    form->addRow(tr("Tamanho do tile"), tileSizeRow);
    form->addRow(tr("Nome"), m_name);
    form->addRow(tr("Categoria"), m_category);
    controls->addLayout(form);

    auto* modeBox = new QGroupBox(tr("Formato de origem"), this);
    auto* mh = new QGridLayout(modeBox);
    struct ModeDef { int id; const char* label; const char* tip; };
    static const ModeDef modes[] = {
        { 1, "2×3", "Formato 2×3 (normalmente água/chão)" },
        { 5, "3×4", "Formato 3×4" },
        { 2, "2×2", "Formato 2×2 (normalmente telhados/paredes)" },
        { 3, "2×2 — cópia", "Copia tiles 1:1 em blocos 2×2" },
        { 4, "1×1 — cópia", "Copia um tile 1:1" }
    };
    int modeIndex = 0;
    for (const ModeDef& m : modes) {
        auto* rb = new QRadioButton(QString::fromUtf8(m.label), modeBox);
        rb->setToolTip(QString::fromUtf8(m.tip));
        if (m.id == 1) rb->setChecked(true);
        mh->addWidget(rb, modeIndex / 2, modeIndex % 2);
        ++modeIndex;
        connect(rb, &QRadioButton::toggled, this, [this, id = m.id](bool on) {
            if (on) { conv.mode = id; refreshAll(); }
        });
    }
    controls->addWidget(modeBox);

    auto* layoutRow = new QHBoxLayout;
    layoutRow->addWidget(new QLabel(tr("Organização:"), this));
    auto* layoutCombo = new QComboBox(this);
    layoutCombo->addItem(tr("Vertical (um abaixo do outro)"),
                         int(core::autotile::Layout::Vertical));
    layoutCombo->addItem(tr("Horizontal (lado a lado)"),
                         int(core::autotile::Layout::Horizontal));
    layoutCombo->addItem(tr("Como na imagem de entrada"),
                         int(core::autotile::Layout::AsInput));
    layoutCombo->setToolTip(tr("Com vários autotiles marcados, define se os blocos convertidos "
                               "saem empilhados para baixo (padrão) ou lado a lado.\n"
                               "A direção afeta apenas a prévia/arquivo combinado; na importação cada bloco vira um Autotile independente."));
    layoutRow->addWidget(layoutCombo, 1);
    controls->addLayout(layoutRow);
    connect(layoutCombo, &QComboBox::currentIndexChanged, this, [this, layoutCombo](int i) {
        conv.layout = core::autotile::Layout(layoutCombo->itemData(i).toInt());
        refreshAll();
    });

    auto* animationBox = new QGroupBox(tr("Autotile animado"), this);
    auto* animationForm = new QFormLayout(animationBox);
    m_animated = new QCheckBox(tr("A imagem tem vários quadros"), animationBox);
    m_frameCount = new QSpinBox(animationBox); m_frameCount->setRange(2, 32); m_frameCount->setValue(3);
    m_frameAxis = new QComboBox(animationBox);
    m_frameAxis->addItem(tr("Horizontal (lado a lado)"), QStringLiteral("horizontal"));
    m_frameAxis->addItem(tr("Vertical (um abaixo do outro)"), QStringLiteral("vertical"));
    m_animationFps = new QDoubleSpinBox(animationBox); m_animationFps->setRange(0.1, 60.0); m_animationFps->setDecimals(1); m_animationFps->setValue(6.0); m_animationFps->setSuffix(tr(" quadros/s"));
    m_animationLoop = new QCheckBox(tr("Repetir"), animationBox); m_animationLoop->setChecked(true);
    m_animationPingPong = new QCheckBox(tr("Ida e volta"), animationBox);
    m_animationSync = new QComboBox(animationBox);
    m_animationSync->addItem(tr("Juntos — recomendado para água, lava e Autotiles conectados"), true);
    m_animationSync->addItem(tr("Cada célula começa em um momento diferente"), false);
    animationForm->addRow(m_animated);
    animationForm->addRow(tr("Quadros:"), m_frameCount);
    animationForm->addRow(tr("Organização da imagem:"), m_frameAxis);
    animationForm->addRow(tr("Velocidade:"), m_animationFps);
    auto* behaviorRow = new QHBoxLayout; behaviorRow->addWidget(m_animationLoop); behaviorRow->addWidget(m_animationPingPong); behaviorRow->addStretch(1);
    animationForm->addRow(tr("Reprodução:"), behaviorRow);
    animationForm->addRow(tr("Sincronização:"), m_animationSync);
    auto* animationHint = new QLabel(tr("O primeiro quadro define o terreno. Os demais mudam apenas a aparência."), animationBox);
    animationHint->setWordWrap(true); animationHint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    animationForm->addRow(animationHint);
    controls->addWidget(animationBox);
    controls->addStretch(1);

    auto setAnimationEnabled = [this](bool enabled) {
        m_frameCount->setEnabled(enabled); m_frameAxis->setEnabled(enabled);
        m_animationFps->setEnabled(enabled); m_animationLoop->setEnabled(enabled);
        m_animationPingPong->setEnabled(enabled); m_animationSync->setEnabled(enabled);
    };
    setAnimationEnabled(false);
    connect(m_animated, &QCheckBox::toggled, this, [this, setAnimationEnabled](bool on) {
        setAnimationEnabled(on); m_animationPreviewMs = 0; updateSourceFrame(true); refreshAll();
    });
    connect(m_frameCount, &QSpinBox::valueChanged, this, [this](int){ m_animationPreviewMs = 0; updateSourceFrame(true); refreshAll(); });
    connect(m_frameAxis, &QComboBox::currentIndexChanged, this, [this](int){ m_animationPreviewMs = 0; updateSourceFrame(true); refreshAll(); });
    connect(m_animationFps, &QDoubleSpinBox::valueChanged, this, [this](double){ m_animationPreviewMs = 0; refreshOutputPreview(); });
    connect(m_animationLoop, &QCheckBox::toggled, this, [this](bool){ m_animationPreviewMs = 0; refreshOutputPreview(); });
    connect(m_animationPingPong, &QCheckBox::toggled, this, [this](bool){ m_animationPreviewMs = 0; refreshOutputPreview(); });
    connect(m_animationSync, &QComboBox::currentIndexChanged, this, [this](int){ refreshOutputPreview(); });

    auto* sourceTitle = new QLabel(tr("<b>Imagem</b> — clique nos blocos para marcar"), previewHost);
    previews->addWidget(sourceTitle);
    auto* inputArea = new QScrollArea(previewHost);
    inputArea->setWidgetResizable(true);
    inputArea->setAlignment(Qt::AlignCenter);
    inputArea->setMinimumHeight(250);
    m_input = new AtcInputView(conv, inputArea);
    inputArea->setWidget(m_input);
    previews->addWidget(inputArea, 2);

    m_info = new QLabel(tr("Selecione uma imagem para começar."), previewHost);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    previews->addWidget(m_info);

    auto* outRow = new QHBoxLayout;
    outRow->addWidget(new QLabel(tr("<b>Prévia convertida</b>"), this));
    auto* clearBtn = new QPushButton(icons::get(QStringLiteral("eraser")), tr("Limpar seleção"), this);
    outRow->addWidget(clearBtn);
    outRow->addStretch(1);
    previews->addLayout(outRow);

    auto* outArea = new QScrollArea(previewHost);
    outArea->setMinimumHeight(140);
    outArea->setWidgetResizable(false);
    m_output = new QLabel(outArea);
    m_output->setAlignment(Qt::AlignCenter);
    outArea->setWidget(m_output);
    previews->addWidget(outArea, 1);
    auto* previewRule = new QLabel(tr("Esta é a aparência que será importada."), previewHost);
    previewRule->setWordWrap(true);
    previewRule->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    previews->addWidget(previewRule);

    auto* actions = new QHBoxLayout;
    auto* saveBtn = new QPushButton(icons::get(QStringLiteral("save")), tr("Salvar imagem…"), this);
    saveBtn->setToolTip(tr("Grava o atlas convertido como PNG sem alterar o projeto."));
    auto* importBtn = new QPushButton(icons::get(QStringLiteral("import")), tr("Importar Autotile"), this);
    importBtn->setDefault(true);
    importBtn->setToolTip(tr("Adiciona os blocos marcados à biblioteca de Autotiles."));
    auto* close = new QPushButton(tr("Cancelar"), this);
    actions->addWidget(saveBtn);
    actions->addStretch(1);
    actions->addWidget(importBtn);
    actions->addWidget(close);
    v->addLayout(actions);

    auto* note = new QLabel(tr("A importação não altera tilesets ou terrenos existentes."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(note);

    connect(pick, &QPushButton::clicked, this, &AutoTileConverterDialog::pickImage);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        conv.selections.clear();
        refreshAll();
    });
    connect(m_input, &AtcInputView::changed, this, &AutoTileConverterDialog::refreshAll);
    connect(m_tileSizePreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        const int preset = m_tileSizePreset->itemData(index).toInt();
        if (preset > 0 && m_tileSize->value() != preset) m_tileSize->setValue(preset);
    });
    connect(m_tileSize, &QSpinBox::valueChanged, this, [this](int v) {
        conv.tileSize = v;
        if (m_tileSizePreset) {
            const int presetIndex = m_tileSizePreset->findData(v);
            const int desired = presetIndex >= 0 ? presetIndex : m_tileSizePreset->findData(-1);
            if (desired >= 0 && m_tileSizePreset->currentIndex() != desired) {
                const QSignalBlocker blocker(m_tileSizePreset);
                m_tileSizePreset->setCurrentIndex(desired);
            }
        }
        refreshAll();
    });
    connect(saveBtn, &QPushButton::clicked, this, &AutoTileConverterDialog::savePng);
    connect(importBtn, &QPushButton::clicked, this, &AutoTileConverterDialog::importAutotiles);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    m_animationPreviewTimer = new QTimer(this);
    m_animationPreviewTimer->setInterval(50);
    connect(m_animationPreviewTimer, &QTimer::timeout, this, [this] {
        if (!m_animated || !m_animated->isChecked()) return;
        m_animationPreviewMs += m_animationPreviewTimer->interval();
        refreshOutputPreview();
    });
    m_animationPreviewTimer->start();

    refreshAll();
}

void AutoTileConverterDialog::pickImage()
{
    const QString path = AssetBrowserDialog::chooseImage(ed, this, QStringLiteral("Autotiles"));
    if (path.isEmpty()) return;
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Erro"), tr("Não foi possível ler a imagem."));
        return;
    }
    m_sourceImage = img.convertToFormat(QImage::Format_ARGB32);
    conv.selections.clear();
    conv.baseName = QFileInfo(path).completeBaseName();
    m_name->setText(conv.baseName);
    m_animationPreviewMs = 0;
    updateSourceFrame(false);
    refreshAll();
}

void AutoTileConverterDialog::updateSourceFrame(bool clearSelectionIfGeometryChanges)
{
    QImage next;
    if (!m_sourceImage.isNull()) {
        if (!m_animated || !m_animated->isChecked()) {
            next = m_sourceImage;
        } else {
            const int count = qMax(2, m_frameCount->value());
            const bool horizontal = m_frameAxis->currentData().toString() == QLatin1String("horizontal");
            const bool divisible = horizontal ? (m_sourceImage.width() % count == 0)
                                              : (m_sourceImage.height() % count == 0);
            if (divisible) {
                const int fw = horizontal ? m_sourceImage.width() / count : m_sourceImage.width();
                const int fh = horizontal ? m_sourceImage.height() : m_sourceImage.height() / count;
                next = m_sourceImage.copy(0, 0, fw, fh);
            }
        }
    }
    if (clearSelectionIfGeometryChanges && next.size() != conv.img.size()) conv.selections.clear();
    conv.img = next;
    if (m_input) {
        const QSize s = conv.img.isNull() ? QSize(320, 240) : conv.img.size();
        m_input->setFixedSize(s);
        m_input->updateGeometry();
    }
}

QVector<QImage> AutoTileConverterDialog::convertedAnimationFrames(QString* error) const
{
    QVector<QImage> frames;
    if (conv.selections.isEmpty()) {
        if (error) *error = tr("Marque ao menos um bloco de Autotile no primeiro quadro da animação.");
        return frames;
    }
    if (!m_animated || !m_animated->isChecked()) {
        const QImage out = conv.buildOutput();
        if (!out.isNull()) frames.push_back(out);
        return frames;
    }
    if (m_sourceImage.isNull()) {
        if (error) *error = tr("Carregue uma imagem antes de configurar a animação.");
        return frames;
    }
    const int count = qMax(2, m_frameCount->value());
    const bool horizontal = m_frameAxis->currentData().toString() == QLatin1String("horizontal");
    if ((horizontal && m_sourceImage.width() % count != 0) ||
        (!horizontal && m_sourceImage.height() % count != 0)) {
        if (error) *error = horizontal
            ? tr("A largura da imagem (%1 px) precisa ser dividida igualmente entre os %2 quadros da animação.").arg(m_sourceImage.width()).arg(count)
            : tr("A altura da imagem (%1 px) precisa ser dividida igualmente entre os %2 quadros da animação.").arg(m_sourceImage.height()).arg(count);
        return frames;
    }
    const int fw = horizontal ? m_sourceImage.width() / count : m_sourceImage.width();
    const int fh = horizontal ? m_sourceImage.height() : m_sourceImage.height() / count;
    for (int i = 0; i < count; ++i) {
        autotile::Converter frameConv = conv;
        const QRect r(horizontal ? i * fw : 0, horizontal ? 0 : i * fh, fw, fh);
        frameConv.img = m_sourceImage.copy(r);
        const QImage out = frameConv.buildOutput();
        if (out.isNull()) {
            if (error) *error = tr("Não foi possível preparar o quadro %1 da animação.").arg(i + 1);
            frames.clear();
            return frames;
        }
        if (!frames.isEmpty() && out.size() != frames.first().size()) {
            if (error) *error = tr("Os quadros da animação não ficaram com o mesmo tamanho. Verifique a organização da imagem.");
            frames.clear();
            return frames;
        }
        frames.push_back(out);
    }
    return frames;
}

QImage AutoTileConverterDialog::packedAnimationOutput(QVector<QPoint>* relativeFrameOrigins,
                                                       QString* error) const
{
    const QVector<QImage> frames = convertedAnimationFrames(error);
    if (relativeFrameOrigins) relativeFrameOrigins->clear();
    if (frames.isEmpty()) return QImage();
    if (frames.size() == 1) {
        if (relativeFrameOrigins) relativeFrameOrigins->push_back(QPoint(0, 0));
        return frames.first();
    }
    const int w = frames.first().width();
    const int h = frames.first().height();
    QImage packed(w, h * frames.size(), QImage::Format_ARGB32);
    packed.fill(Qt::transparent);
    QPainter painter(&packed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int i = 0; i < frames.size(); ++i) {
        painter.drawImage(0, i * h, frames.at(i));
        if (relativeFrameOrigins)
            relativeFrameOrigins->push_back(QPoint(0, (i * h) / qMax(1, conv.tileSize)));
    }
    return packed;
}

QString AutoTileConverterDialog::registerAnimation(Tileset& ts, int baseCol, int baseRow,
                                                    const QVector<QPoint>& relativeOrigins,
                                                    const QSize& frameTiles)
{
    if (relativeOrigins.size() < 2 || !frameTiles.isValid()) return QString();
    AnimatedAutotile anim;
    anim.name = m_name->text().trimmed().isEmpty() ? tr("Autotile animado") : m_name->text().trimmed();
    anim.baseX = baseCol + relativeOrigins.first().x();
    anim.baseY = baseRow + relativeOrigins.first().y();
    anim.cols = frameTiles.width();
    anim.rows = frameTiles.height();
    for (const QPoint& relative : relativeOrigins)
        anim.frameOrigins.push_back(QPoint(baseCol + relative.x(), baseRow + relative.y()));
    anim.fps = m_animationFps->value();
    anim.loop = m_animationLoop->isChecked();
    anim.pingPong = m_animationPingPong->isChecked();
    anim.synchronized = m_animationSync->currentData().toBool();
    const QString id = anim.id;
    ts.animatedAutotiles.push_back(anim);
    return id;
}

void AutoTileConverterDialog::refreshOutputPreview()
{
    if (!m_output) return;
    QString error;
    const QVector<QImage> frames = convertedAnimationFrames(&error);
    if (frames.isEmpty()) {
        m_output->clear();
        m_output->setText(error.isEmpty() ? tr("Sem saída.") : error);
        m_output->setMinimumSize(260, 100);
        return;
    }
    int frame = 0;
    if (frames.size() > 1) {
        AnimatedAutotile preview;
        preview.fps = m_animationFps->value();
        preview.loop = m_animationLoop->isChecked();
        preview.pingPong = m_animationPingPong->isChecked();
        preview.synchronized = true;
        for (int i = 0; i < frames.size(); ++i) preview.frameOrigins.push_back(QPoint(0, i));
        frame = animatedAutotileFrame(preview, m_animationPreviewMs, 0);
    }
    const QImage& out = frames.at(qBound(0, frame, frames.size() - 1));
    m_output->setText(QString());
    m_output->setPixmap(QPixmap::fromImage(out));
    m_output->setFixedSize(out.size());
}

void AutoTileConverterDialog::refreshAll()
{
    if (m_input) {
        m_input->setMinimumSize(m_input->sizeHint());
        m_input->updateGeometry();
        m_input->update();
    }
    QString info = conv.info();
    if (m_animated && m_animated->isChecked()) {
        QString error;
        const QVector<QImage> frames = convertedAnimationFrames(&error);
        if (!error.isEmpty()) info += tr("<br><b>⚠ Animação:</b> %1").arg(error.toHtmlEscaped());
        else if (!frames.isEmpty())
            info += tr("<br><b>Animação:</b> %1 quadros · %2 quadros/s · %3%4 · As conexões automáticas usam o primeiro quadro como referência.")
                .arg(frames.size()).arg(m_animationFps->value(), 0, 'f', 1)
                .arg(m_animationSync->currentData().toBool() ? tr("sincronizada") : tr("início diferente por célula"))
                .arg(m_animationPingPong->isChecked() ? tr(" · ida e volta") : QString());
    }
    m_info->setText(info);
    refreshOutputPreview();

}

void AutoTileConverterDialog::savePng()
{
    QString error;
    const QImage output = packedAnimationOutput(nullptr, &error);
    if (output.isNull()) {
        QMessageBox::warning(this, tr("Atenção"), error.isEmpty() ? tr("Marque ao menos um bloco de autotile.") : error);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Salvar atlas"),
                                                      m_name->text() + QStringLiteral(".png"),
                                                      tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    if (!output.save(path, "PNG"))
        QMessageBox::warning(this, tr("Erro"), tr("Falha ao gravar o arquivo."));
}

void AutoTileConverterDialog::importAutotiles()
{
    if (conv.selections.isEmpty()) {
        QMessageBox::information(this, tr("Importar Autotile"), tr("Marque pelo menos um bloco de Autotile na prévia."));
        return;
    }

    struct PendingAutotile {
        Tileset backing;
        QString name;
        QString animationId;
        QSize frameTiles;
        int frameCount = 1;
    };
    QVector<PendingAutotile> pending;
    const auto allSelections = conv.selections;
    pending.reserve(allSelections.size());
    const QString baseName = m_name->text().trimmed().isEmpty() ? tr("Autotile") : m_name->text().trimmed();

    // Fase 1 — somente memória. Qualquer erro aborta sem tocar no projeto,
    // Conjunto de conexõess, Terrains ou Tilesets normais.
    for (int i = 0; i < allSelections.size(); ++i) {
        conv.selections.clear();
        conv.selections.push_back(allSelections.at(i));
        QString buildError;
        QVector<QPoint> relativeOrigins;
        const QImage out = packedAnimationOutput(&relativeOrigins, &buildError);
        QString frameError;
        const QVector<QImage> frames = convertedAnimationFrames(&frameError);
        if (out.isNull() || frames.isEmpty()) {
            conv.selections = allSelections;
            QMessageBox::warning(this, tr("Importar Autotile"),
                                 !buildError.isEmpty() ? buildError : frameError);
            return;
        }
        PendingAutotile item;
        item.name = allSelections.size() == 1 ? baseName : tr("%1 %2").arg(baseName).arg(i + 1);
        item.backing = makeCombinedTileset({ NamedImage{out, item.name} }, item.name,
                                           conv.tileSize, conv.tileSize, QStringLiteral("vertical"));
        item.backing.internalAutotileAtlas = true;
        item.frameTiles = QSize(frames.first().width() / qMax(1, conv.tileSize),
                                frames.first().height() / qMax(1, conv.tileSize));
        item.animationId = registerAnimation(item.backing, 0, 0, relativeOrigins, item.frameTiles);
        if (!item.backing.animatedAutotiles.isEmpty())
            item.backing.animatedAutotiles.last().name = item.name;
        item.frameCount = frames.size();
        pending.push_back(item);
    }
    conv.selections = allSelections;

    // Presets customizados salvos ficam disponíveis mesmo que o usuário nunca
    // tenha aberto a aba Wang nesta sessão. O loader é idempotente.
    io::loadWangPresetsFromSettings(ed);

    // Fase 2 — commit atomico. O backing e oculto e nao participa da paleta
    // de Tilesets normais. Cada selecao vira um recurso Autotile independente.
    const int normalTileset = ed.activeTilesetIdx;
    const TilesetSelection normalSelection = ed.tsSel;
    const int addedStart = ed.tilesets.size();
    const int autotileStart = ed.autotiles.size();
    const int wangSetStart = ed.wangSets.size();
    const int wangPresetStart = ed.wangPresets.size();
    int wangConfigured = 0;
    QStringList wangPresetNames;
    auto rollbackImport = [&] {
        while (ed.wangPresets.size() > wangPresetStart) ed.wangPresets.removeLast();
        while (ed.wangSets.size() > wangSetStart) ed.wangSets.removeLast();
        while (ed.autotiles.size() > autotileStart) ed.autotiles.removeLast();
        while (ed.tilesets.size() > addedStart) ed.removeTileset(ed.tilesets.size() - 1);
        ed.activeTilesetIdx = normalTileset;
        ed.tsSel = normalSelection;
    };
    QString lastId;
    for (PendingAutotile& item : pending) {
        item.backing.firstgid = ed.nextFirstGid();
        ed.tilesets.push_back(item.backing);
        const int ownerIdx = ed.tilesets.size() - 1;
        const QPoint preview(0, qMax(0, item.frameTiles.height() - 1));
        lastId = registerTilesetAutotile(ed, ownerIdx, item.name,
                                         QRect(0, 0, item.frameTiles.width(), item.frameTiles.height()),
                                         item.animationId, QString(), -1, true, preview);
        if (!lastId.isEmpty() && m_category)
            setTilesetAutotileCategory(ed, ownerIdx, lastId, m_category->currentText().trimmed().left(128));
        if (lastId.isEmpty()) {
            rollbackImport();
            QMessageBox::warning(this, tr("Importar Autotile"), tr("Não foi possível importar. O projeto não foi alterado."));
            return;
        }

        AutotileWangAutoConfigResult wangSetup;
        const bool configured = autoConfigureTilesetAutotileWang(ed, ownerIdx, lastId, &wangSetup);
        const bool requiresAutomaticWang = autotileGridRequiresAutomaticWang(
            item.frameTiles.width(), item.frameTiles.height());
        if (requiresAutomaticWang && !configured) {
            rollbackImport();
            QMessageBox::warning(this, tr("Importar Autotile"),
                                 !wangSetup.failureMessage.isEmpty() ? wangSetup.failureMessage :
                                 tr("Não foi possível configurar automaticamente o terreno do atlas %1×%2. A importação foi cancelada.")
                                     .arg(item.frameTiles.width()).arg(item.frameTiles.height()));
            return;
        }
        if (configured && !wangSetup.alreadyConfigured) {
            ++wangConfigured;
            if (!wangSetup.presetName.isEmpty() && !wangPresetNames.contains(wangSetup.presetName))
                wangPresetNames.push_back(wangSetup.presetName);
        }
    }
    ed.reindexTilesetGids();
    ed.activeTilesetIdx = normalTileset;
    ed.tsSel = normalSelection;
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.selectionChanged();

    QString success = pending.size() == 1
        ? tr("“%1” foi adicionado à biblioteca global de Autotiles.\n\nA miniatura da paleta usa uma peça do primeiro quadro da animação.").arg(pending.first().name)
        : tr("%1 Autotiles foram adicionados separadamente à biblioteca global.\n\nTilesets normais e conexões automáticas existentes permaneceram intactos.").arg(pending.size());
    if (wangConfigured > 0) {
        success += tr("\n\nRecursos com conexões automáticas configuradas: %1.").arg(wangConfigured);
        if (!wangPresetNames.isEmpty())
            success += tr("\nModelo usado: %1").arg(wangPresetNames.join(QStringLiteral(", ")));
    }
    QMessageBox::information(this, tr("Autotile importado"), success);
    accept();
}


// ------------------------------------------------ GenerateAutotileDialog
GenerateAutotileDialog::GenerateAutotileDialog(Editor& editorRef, int srcTilesetIdx,
                                               int tx, int ty, int cols, int rows,
                                               QWidget* parent)
    : QDialog(parent), ed(editorRef), m_srcIdx(srcTilesetIdx),
      m_tx(tx), m_ty(ty), m_cols(cols), m_rows(rows)
{
    setWindowTitle(tr("Gerar autotile a partir da seleção"));
    resize(720, 620);
    auto* v = new QVBoxLayout(this);

    const Tileset* src = ed.tilesetAt(m_srcIdx);
    auto* hint = new QLabel(tr("Converte o bloco <b>%1×%2</b> selecionado em “%3” (a partir do tile "
                               "%4,%5) no atlas bitmask, sem precisar reimportar o chipset.")
                                .arg(m_cols).arg(m_rows)
                                .arg(src ? src->name : QString()).arg(m_tx).arg(m_ty), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    // ---- modo ------------------------------------------------------------
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Modo do autotile:"), this));
    m_mode = new QComboBox(this);
    for (int m : { atc::RPGM_XP_3x4, atc::RPGM_2x3, atc::RPGM_2x2, atc::COPY_2x2, atc::COPY_1x1 }) {
        const QSize b = autotile::blocksInSelection(m_cols, m_rows, m);
        QString label = autotile::modeName(m);
        if (b.isEmpty()) label += tr("  — não divide a seleção");
        else if (b.width() * b.height() > 1)
            label += tr("  — %1 autotiles").arg(b.width() * b.height());
        m_mode->addItem(label, m);
    }
    m_mode->setToolTip(tr("Detectado pelo tamanho da seleção, mas você pode trocar.\n"
                          "Confira no preview antes de inserir."));
    modeRow->addWidget(m_mode, 1);
    v->addLayout(modeRow);

    // ---- previews --------------------------------------------------------
    auto* prevRow = new QHBoxLayout;
    auto* srcBox = new QGroupBox(tr("Seleção (origem)"), this);
    auto* sv = new QVBoxLayout(srcBox);
    m_srcPreview = new QLabel(srcBox);
    m_srcPreview->setAlignment(Qt::AlignCenter);
    m_srcPreview->setStyleSheet(QStringLiteral("background:#1a1a1a"));
    m_srcPreview->setMinimumSize(140, 150);
    sv->addWidget(m_srcPreview);
    auto* outBox = new QGroupBox(tr("Atlas gerado"), this);
    auto* ov = new QVBoxLayout(outBox);
    auto* outScroll = new QScrollArea(outBox);
    outScroll->setWidgetResizable(false);
    outScroll->setMinimumHeight(150);
    m_outPreview = new QLabel(outScroll);
    m_outPreview->setAlignment(Qt::AlignCenter);
    outScroll->setWidget(m_outPreview);
    ov->addWidget(outScroll);
    prevRow->addWidget(srcBox);
    prevRow->addWidget(outBox, 1);
    v->addLayout(prevRow);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(m_info);

    // Autotile e recurso separado: nao existe destino em Tileset normal.
    auto* resourceBox = new QGroupBox(tr("Recurso Autotile"), this);
    auto* rv = new QVBoxLayout(resourceBox);
    auto* resourceInfo = new QLabel(tr("O atlas gerado será armazenado em backing interno e aparecerá somente na biblioteca de Autotiles. Nenhum Tileset normal será alterado."), resourceBox);
    resourceInfo->setWordWrap(true);
    resourceInfo->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    rv->addWidget(resourceInfo);
    v->addWidget(resourceBox);

    auto* btns = new QHBoxLayout;
    auto* saveBtn = new QPushButton(icons::get(QStringLiteral("save")), tr("Salvar imagem…"), this);
    auto* createBtn = new QPushButton(icons::get(QStringLiteral("add")), tr("Criar Autotile"), this);
    createBtn->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancelar"), this);
    btns->addWidget(saveBtn);
    btns->addStretch(1);
    btns->addWidget(createBtn);
    btns->addWidget(cancel);
    v->addLayout(btns);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int) { rebuildAtlas(); });
    connect(saveBtn, &QPushButton::clicked, this, &GenerateAutotileDialog::doSavePng);
    connect(createBtn, &QPushButton::clicked, this, &GenerateAutotileDialog::doCreateAutotile);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    // modo sugerido pelo tamanho da seleção
    const int suggested = autotile::suggestModeForSelection(m_cols, m_rows);
    for (int i = 0; i < m_mode->count(); ++i)
        if (m_mode->itemData(i).toInt() == suggested) { m_mode->setCurrentIndex(i); break; }
    rebuildAtlas();
}

void GenerateAutotileDialog::rebuildAtlas()
{
    const Tileset* src = ed.tilesetAt(m_srcIdx);
    if (!src) return;
    const int mode = m_mode->currentData().toInt();
    const QSize blocks = autotile::blocksInSelection(m_cols, m_rows, mode);

    conv = autotile::fromTilesetBlock(*src, m_tx, m_ty, m_cols, m_rows, mode);
    m_atlas = conv.buildOutput();

    m_srcPreview->setPixmap(QPixmap::fromImage(conv.img));
    m_outPreview->setPixmap(QPixmap::fromImage(m_atlas));
    m_outPreview->setFixedSize(m_atlas.size());

    if (blocks.isEmpty()) {
        m_info->setText(tr("<span style='color:#ff6b6b'>A seleção %1×%2 não divide em blocos de "
                           "%3 — escolha outro modo ou refaça a seleção.</span>")
                            .arg(m_cols).arg(m_rows).arg(autotile::modeName(mode)));
    } else {
        const int n = blocks.width() * blocks.height();
        m_info->setText(tr("Autotiles: %1 · tile de %2 px · atlas de saída %3×%4 px "
                           "(%5×%6 tiles)%7")
                            .arg(n).arg(conv.tileSize)
                            .arg(m_atlas.width()).arg(m_atlas.height())
                            .arg(m_atlas.width() / qMax(1, conv.tileSize))
                            .arg(m_atlas.height() / qMax(1, conv.tileSize))
                            .arg(n > 1 ? tr(" · empilhados na vertical") : QString()));
    }
}

void GenerateAutotileDialog::doCreateAutotile()
{
    if (m_atlas.isNull()) return;
    const Tileset* src = ed.tilesetAt(m_srcIdx);
    const int mode = m_mode->currentData().toInt();
    const QSize blocks = autotile::blocksInSelection(m_cols, m_rows, mode);
    if (blocks.isEmpty()) {
        QMessageBox::information(this, tr("Gerar Autotile"), tr("A seleção não pode ser dividida no formato escolhido."));
        return;
    }
    const int count = blocks.width() * blocks.height();
    const int tile = qMax(1, conv.tileSize);
    const int outCols = qMax(1, m_atlas.width() / tile);
    const int totalRows = qMax(1, m_atlas.height() / tile);
    if (totalRows % count != 0) {
        QMessageBox::warning(this, tr("Gerar Autotile"), tr("O atlas gerado não pôde ser separado em recursos independentes."));
        return;
    }
    const int rowsPerResource = totalRows / count;
    const int resourceHeightPx = rowsPerResource * tile;

    struct Pending { Tileset backing; QString name; };
    QVector<Pending> pending;
    pending.reserve(count);
    const QString baseName = tr("%1 autotile").arg(src ? src->name : tr("Novo"));
    for (int i = 0; i < count; ++i) {
        const QImage image = m_atlas.copy(0, i * resourceHeightPx, m_atlas.width(), resourceHeightPx);
        const QString name = count == 1 ? baseName : tr("%1 %2").arg(baseName).arg(i + 1);
        Tileset backing = makeTileset(image, name, tile, tile, 0, 0);
        backing.internalAutotileAtlas = true;
        pending.push_back(Pending{backing, name});
    }

    io::loadWangPresetsFromSettings(ed);
    const int normalTileset = ed.activeTilesetIdx;
    const TilesetSelection normalSelection = ed.tsSel;
    const int addedStart = ed.tilesets.size();
    const int autotileStart = ed.autotiles.size();
    const int wangSetStart = ed.wangSets.size();
    const int wangPresetStart = ed.wangPresets.size();
    int wangConfigured = 0;
    auto rollbackCreate = [&] {
        while (ed.wangPresets.size() > wangPresetStart) ed.wangPresets.removeLast();
        while (ed.wangSets.size() > wangSetStart) ed.wangSets.removeLast();
        while (ed.autotiles.size() > autotileStart) ed.autotiles.removeLast();
        while (ed.tilesets.size() > addedStart) ed.removeTileset(ed.tilesets.size() - 1);
        ed.activeTilesetIdx = normalTileset;
        ed.tsSel = normalSelection;
    };
    QString firstId;
    for (Pending& item : pending) {
        item.backing.firstgid = ed.nextFirstGid();
        ed.tilesets.push_back(item.backing);
        const int ownerIdx = ed.tilesets.size() - 1;
        const QString id = registerTilesetAutotile(ed, ownerIdx, item.name,
                                                   QRect(0, 0, outCols, rowsPerResource),
                                                   QString(), QString(), -1, true,
                                                   QPoint(0, qMax(0, rowsPerResource - 1)));
        if (id.isEmpty()) {
            rollbackCreate();
            QMessageBox::warning(this, tr("Gerar Autotile"), tr("Não foi possível registrar todos os recursos; a operação foi revertida."));
            return;
        }
        AutotileWangAutoConfigResult wangSetup;
        const bool configured = autoConfigureTilesetAutotileWang(ed, ownerIdx, id, &wangSetup);
        const bool requiresAutomaticWang =
            autotileGridRequiresAutomaticWang(outCols, rowsPerResource);
        if (requiresAutomaticWang && !configured) {
            rollbackCreate();
            QMessageBox::warning(this, tr("Gerar Autotile"),
                                 !wangSetup.failureMessage.isEmpty() ? wangSetup.failureMessage :
                                 tr("Esta folha %1×%2 precisa das regras de conexão automática, mas a configuração falhou. A operação foi desfeita para não deixar o recurso incompleto.")
                                     .arg(outCols).arg(rowsPerResource));
            return;
        }
        if (configured && !wangSetup.alreadyConfigured) ++wangConfigured;
        if (firstId.isEmpty()) firstId = id;
    }
    ed.reindexTilesetGids();
    ed.activeTilesetIdx = normalTileset;
    ed.tsSel = normalSelection;
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.selectionChanged();
    QString createdMessage = count == 1
        ? tr("“%1” foi adicionado à biblioteca global. O Tileset de origem não foi alterado.").arg(baseName)
        : tr("%1 Autotiles independentes foram adicionados à biblioteca global. O Tileset de origem não foi alterado.").arg(count);
    if (wangConfigured > 0)
        createdMessage += tr("\n\nConexões automáticas configuradas em %1 recurso(s).").arg(wangConfigured);
    QMessageBox::information(this, tr("Autotile criado"), createdMessage);
    accept();
}

void GenerateAutotileDialog::doSavePng()
{
    if (m_atlas.isNull()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Salvar atlas"),
                                                      QStringLiteral("autotile.png"),
                                                      tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    if (!m_atlas.save(path, "PNG"))
        QMessageBox::warning(this, tr("Erro"), tr("Falha ao gravar o arquivo."));
}

// ------------------------------------------------------- ScaledExportDialog
ScaledExportDialog::ScaledExportDialog(Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Exportar para engines (escala reduzida)"));
    resize(560, 480);
    auto* v = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Gera um pacote pronto para outra engine com tudo em escala "
                               "reduzida: a imagem do mapa, o <b>.tmj</b> (já com o tile menor) "
                               "e os <b>tilesets redimensionados</b>. O redimensionamento é "
                               "nearest-neighbor, sem borrar o pixel art."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    auto* form = new QFormLayout;
    m_tile = new QSpinBox(this);
    m_tile->setRange(1, 512);
    m_tile->setValue(16);
    m_tile->setSuffix(tr(" px"));
    m_tile->setToolTip(tr("Tamanho do tile na saída. O mapa atual usa %1 px.")
                           .arg(ed.mapInfo().tileWidth));
    form->addRow(tr("Tile de saída"), m_tile);

    auto* dirRow = new QHBoxLayout;
    m_dir = new QLineEdit(this);
    m_dir->setPlaceholderText(tr("pasta de destino…"));
    auto* browse = new QPushButton(icons::get(QStringLiteral("open")), tr("Escolher…"), this);
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);
    form->addRow(tr("Destino"), dirRow);
    v->addLayout(form);

    auto* itemsBox = new QGroupBox(tr("O que gerar"), this);
    auto* iv = new QVBoxLayout(itemsBox);
    m_optPng = new QCheckBox(tr("mapa.png — imagem do mapa inteiro"), itemsBox);
    m_optTmj = new QCheckBox(tr("mapa.tmj — Tiled JSON com o tile reduzido"), itemsBox);
    m_optTilesets = new QCheckBox(tr("tilesets/*.png — imagens dos tilesets redimensionadas"), itemsBox);
    m_optStar = new QCheckBox(tr("chao.png + acima.png — separados pelos tiles ★"), itemsBox);
    m_optRm2k = new QCheckBox(tr("Converter taxas de cores para o formato clássico de 256 cores"), itemsBox);
    m_optRm2k->setToolTip(tr("Grava todos os PNGs com 256 cores indexadas (8 bits), que é o que o "
                             "RM2K/2K3 aceita — truecolor de 24 bits é recusado pelo editor.\n"
                             "O índice 0 da paleta fica preto e é a cor transparente do engine.\n"
                             "Sem dithering, para não sujar o pixel art."));
    m_optPng->setChecked(true);
    m_optTmj->setChecked(true);
    m_optTilesets->setChecked(true);
    iv->addWidget(m_optPng);
    iv->addWidget(m_optTmj);
    iv->addWidget(m_optTilesets);
    iv->addWidget(m_optStar);
    auto* sep = new QFrame(itemsBox);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color:#444"));
    iv->addWidget(sep);
    iv->addWidget(m_optRm2k);
    auto* rmHint = new QLabel(tr("O RM2K/2K3 usa tiles de 16×16 — combine com o tamanho de saída "
                                 "acima. A montagem do ChipSet 480×256 não é feita aqui."), itemsBox);
    rmHint->setWordWrap(true);
    rmHint->setStyleSheet(QStringLiteral("color:#999;font-size:11px;margin-left:20px"));
    iv->addWidget(rmHint);
    v->addWidget(itemsBox);
    connect(m_optRm2k, &QCheckBox::toggled, this, [this](bool) { updateSummary(); });

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);

    m_warnings = new QLabel(this);
    m_warnings->setWordWrap(true);
    m_warnings->setStyleSheet(QStringLiteral("color:#ffd45e;font-size:11px"));
    v->addWidget(m_warnings);
    v->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(tr("Exportar"));
    v->addWidget(box);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Pasta de destino"));
        if (!d.isEmpty()) m_dir->setText(d);
    });
    connect(m_tile, &QSpinBox::valueChanged, this, [this](int) { updateSummary(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, &ScaledExportDialog::doExport);
    updateSummary();
}

void ScaledExportDialog::updateSummary()
{
    const MapInfo& info = ed.mapInfo();
    const double scale = double(m_tile->value()) / qMax(1, info.tileWidth);
    m_summary->setText(tr("Tile %1 px → <b>%2 px</b> (escala %3%)<br>"
                          "Mapa: %4×%5 tiles · %6×%7 px → <b>%8×%9 px</b>")
                           .arg(info.tileWidth).arg(m_tile->value())
                           .arg(scale * 100, 0, 'f', 1)
                           .arg(info.width).arg(info.height)
                           .arg(info.pixelWidth()).arg(info.pixelHeight())
                           .arg(int(info.pixelWidth() * scale)).arg(int(info.pixelHeight() * scale)));

    QStringList w = io::validateScaledExport(ed, m_tile->value());
    if (m_optRm2k && m_optRm2k->isChecked() && m_tile->value() != 16)
        w << tr("O formato clássico de 256 cores usa tiles de 16×16, mas a saída está em %1 px.")
                 .arg(m_tile->value());
    m_warnings->setText(w.isEmpty() ? QString()
                                    : QStringLiteral("⚠ ") + w.join(QStringLiteral("\n⚠ ")));
}

void ScaledExportDialog::doExport()
{
    if (m_dir->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Destino"), tr("Escolha a pasta de destino."));
        return;
    }
    io::ScaledExportOptions opt;
    opt.outDir = m_dir->text().trimmed();
    opt.targetTileSize = m_tile->value();
    opt.mapPng = m_optPng->isChecked();
    opt.tmj = m_optTmj->isChecked();
    opt.tilesets = m_optTilesets->isChecked();
    opt.starPair = m_optStar->isChecked();
    opt.rm2kColors = m_optRm2k->isChecked();
    opt.rm2kIndex0 = QColor(Qt::black);
    opt.rm2kDither = false;

    QString err;
    io::ScaledExportResult res;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = io::exportScaledPackage(ed, opt, &res, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, tr("Erro"), err); return; }

    QString msg = tr("Pacote gerado em:\n%1\n\nEscala %2% · mapa %3×%4 px\n\nArquivos:\n")
                      .arg(opt.outDir).arg(res.scale * 100, 0, 'f', 1)
                      .arg(res.mapPixels.width()).arg(res.mapPixels.height());
    for (const QString& f : res.files) msg += QStringLiteral("  • ") % f % QLatin1Char('\n');
    if (!res.colorReport.isEmpty()) {
        msg += tr("\nCores (formato clássico de 256 cores):\n");
        for (const QString& c : res.colorReport) msg += QStringLiteral("  • ") % c % QLatin1Char('\n');
        if (res.anyLossy)
            msg += tr("\nAlgumas imagens tinham mais de 256 cores e foram aproximadas.\n");
    }
    if (!res.warnings.isEmpty())
        msg += tr("\nAvisos:\n  ! ") + res.warnings.join(QStringLiteral("\n  ! "));
    QMessageBox::information(this, tr("Exportado"), msg);
    accept();
}

// ---------------------------------------------------------- ShortcutsDialog
ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Atalhos de teclado"));
    resize(520, 560);
    auto* v = new QVBoxLayout(this);
    auto* text = new QTextBrowser(this);
    text->setHtml(tr(
        "<h3>Ferramentas</h3><table cellpadding=4>"
        "<tr><td><b>B</b></td><td>Pincel / carimbo</td></tr>"
        "<tr><td><b>E</b></td><td>Borracha</td></tr>"
        "<tr><td><b>U</b></td><td>Balde de tinta</td></tr>"
        "<tr><td><b>R</b></td><td>Retângulo</td></tr>"
        "<tr><td><b>C</b></td><td>Círculo / elipse</td></tr>"
        "<tr><td><b>L</b></td><td>Linha / caminho</td></tr>"
        "<tr><td><b>Autotile</b></td><td>Selecionar na paleta ativa Terrain automaticamente</td></tr>"
        "<tr><td><b>V</b></td><td>Selecionar objeto</td></tr>"
        "<tr><td><b>F</b></td><td>Colocar objeto</td></tr>"
        "</table>"
        "<h3>Modos</h3><table cellpadding=4>"
        "<tr><td><b>G</b></td><td>Mostrar/ocultar grade</td></tr>"
        "<tr><td><b>H</b></td><td>Focar camada ativa</td></tr>"
        "<tr><td><b>S</b></td><td>Snap to grid (objetos)</td></tr>"
        "<tr><td><b>P</b></td><td>Colocar por cima (empilhar)</td></tr>"
        "<tr><td><b>Q</b></td><td>Modo aleatório 🎲</td></tr>"
        "<tr><td><b>;</b></td><td>Preview fantasma</td></tr>"
        "<tr><td><b>Esc</b></td><td>Sair dos modos ★ / ✖ / 🎲</td></tr>"
        "</table>"
        "<h3>Edição</h3><table cellpadding=4>"
        "<tr><td><b>Ctrl+Z</b> / <b>Ctrl+Shift+Z</b></td><td>Desfazer / refazer</td></tr>"
        "<tr><td><b>Ctrl+S</b> / <b>Ctrl+O</b></td><td>Salvar / abrir projeto</td></tr>"
        "<tr><td><b>Ctrl+N</b></td><td>Nova aba de mapa</td></tr>"
        "<tr><td><b>Ctrl+T</b></td><td>Novo tileset</td></tr>"
        "<tr><td><b>Ctrl+C</b> / <b>Ctrl+V</b></td><td>Copiar / colar objetos</td></tr>"
        "<tr><td><b>Delete</b></td><td>Excluir objetos selecionados</td></tr>"
        "<tr><td><b>Setas</b></td><td>Mover objeto 1 px (Ctrl = 10 px)</td></tr>"
        "</table>"
        "<h3>Navegação</h3><table cellpadding=4>"
        "<tr><td><b>Ctrl + roda</b></td><td>Zoom no cursor</td></tr>"
        "<tr><td><b>Botão do meio</b></td><td>Arrastar a vista</td></tr>"
        "<tr><td><b>Botão direito</b></td><td>Usar o tile/Autotile colocado sob o cursor</td></tr>"
        "<tr><td><b>Alt + clique</b></td><td>Mesmo picker semântico (atalho compatível)</td></tr>"
        "<tr><td><b>Ctrl + clique/arraste no mapa</b></td><td>Apagar durante a pintura, inclusive Terrain/Autotile</td></tr>"
        "<tr><td><b>Ctrl + clique na paleta</b></td><td>Adicionar ao pool aleatório</td></tr>"
        "<tr><td><b>Ctrl+0 / Ctrl+1</b></td><td>Ajustar à janela / zoom 100%</td></tr>"
        "</table>"));
    v->addWidget(text);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

// ============================================================================
//  Legendas — preview, comando e ajustes globais
// ============================================================================
namespace {

/// Rolar a página não pode trocar o valor de um combo/spin que passa sob o
/// cursor — foi assim que eu troquei "segue o jogador" por "segue um evento"
/// sem querer durante o teste. Regra: só aceita a roda se o controle estiver
/// com o foco.
class SemRodaSemFoco : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject* alvo, QEvent* e) override
    {
        if (e->type() != QEvent::Wheel) return QObject::eventFilter(alvo, e);
        auto* w = qobject_cast<QWidget*>(alvo);
        if (w && !w->hasFocus()) { e->ignore(); return true; }
        return QObject::eventFilter(alvo, e);
    }
};

/// Aplica a regra acima em todos os combos e spins do diálogo.
void protegerDaRoda(QWidget* raiz)
{
    auto* filtro = new SemRodaSemFoco(raiz);
    const auto combos = raiz->findChildren<QComboBox*>();
    for (QComboBox* c : combos) { c->setFocusPolicy(Qt::StrongFocus); c->installEventFilter(filtro); }
    const auto spins = raiz->findChildren<QAbstractSpinBox*>();
    for (QAbstractSpinBox* s : spins) { s->setFocusPolicy(Qt::StrongFocus); s->installEventFilter(filtro); }
}

} // namespace

SubtitlePreview::SubtitlePreview(core::Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    setFixedSize(ed.gameResolution);
    m_clock = new QElapsedTimer;
    m_clock->start();
    // Os efeitos são animados: sem um timer, o preview mostraria um quadro
    // congelado e ninguém saberia como a onda/tremida realmente ficam.
    auto* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    t->start(33);
}

QSize SubtitlePreview::sizeHint() const { return ed.gameResolution; }

void SubtitlePreview::setRequest(const QString& text, const QString& speaker,
                                 const QString& position, const QString& textAlign,
                                 int offsetX, int offsetY)
{
    m_text = text;
    m_speaker = speaker;
    m_pos = position;
    m_align = textAlign;
    m_offsetX = offsetX;
    m_offsetY = offsetY;
    update();
}

void SubtitlePreview::setStyleOverride(const core::SubtitleStyle& style)
{
    m_styleOverride = style;
    m_hasStyleOverride = true;
    update();
}

void SubtitlePreview::clearStyleOverride()
{
    m_hasStyleOverride = false;
    update();
}

void SubtitlePreview::setTextEffects(const core::TextEffectStack& effects,
                                     const core::TextGradientSpec& gradient)
{
    m_effects = effects;
    m_gradient = gradient;
    if (m_clock) m_clock->restart();
    update();
}

void SubtitlePreview::paintEvent(QPaintEvent*)
{
    const core::SubtitleStyle& st = m_hasStyleOverride ? m_styleOverride : ed.subtitleStyle;
    QPainter p(this);
    p.fillRect(rect(), QColor("#14161c"));
    p.setPen(QPen(QColor(70, 75, 90), 1, Qt::DashLine));
    p.drawRect(rect().adjusted(1, 1, -2, -2));

    QString texto = m_text;
    const game::SubtitleCodes cod = game::extractSubtitleCodes(texto);
    Q_UNUSED(cod);

    QFont f = font();
    if (!st.fontFamily.isEmpty()) f.setFamily(st.fontFamily);
    f.setPixelSize(qMax(6, st.fontSize));
    f.setBold(st.fontBold);
    f.setItalic(st.fontItalic);
    const QFontMetricsF fm(f);
    QFont fnome = f;
    fnome.setPixelSize(qMax(6, st.nameFontSize));
    fnome.setBold(true);
    const QFontMetricsF fmNome(fnome);

    const double limite = qMin(double(st.maxWidth),
                               width() - 2.0 * st.marginHorizontal - 2.0 * st.paddingH);
    const QVector<game::TextPage> pgs = game::layoutMessage(texto, f, qMax(40.0, limite), 99);
    if (pgs.isEmpty()) {
        p.setPen(QColor("#8a8a8a"));
        p.drawText(rect(), Qt::AlignCenter, tr("(legenda vazia)"));
        return;
    }
    const QSizeF textMeasure = game::measurePage(pgs[0], f, &ed.iconSet);
    const bool temNome = !m_speaker.isEmpty();
    const double larguraNome = temNome ? fmNome.horizontalAdvance(m_speaker) : 0.0;
    const double cw = qMax(textMeasure.width(), larguraNome) + 2 * st.paddingH;
    const double ch = textMeasure.height() + (temNome ? fmNome.height() : 0.0) + 2 * st.paddingV;

    const core::SubtitleAlign align = core::subtitleAlignFromId(m_align);
    const core::SubtitlePosition pos = core::subtitlePositionFromId(m_pos);
    double x = (width() - cw) / 2.0;
    if (st.boxAlign == core::SubtitleAlign::Left)  x = st.marginHorizontal;
    if (st.boxAlign == core::SubtitleAlign::Right) x = width() - cw - st.marginHorizontal;
    double y = height() - ch - st.marginBottom;
    if (pos == core::SubtitlePosition::Top)    y = st.marginBottom;
    if (pos == core::SubtitlePosition::Middle) y = (height() - ch) / 2.0;

    p.translate(x + st.offsetX + m_offsetX, y + st.offsetY + m_offsetY);
    const QRectF caixa(0, 0, cw, ch);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    if (st.bgStyle != core::SubtitleBg::None) {
        if (st.bgStyle == core::SubtitleBg::Gradient) {
            QLinearGradient g(caixa.topLeft(), caixa.bottomLeft());
            QColor c2 = st.bgColor;
            c2.setAlpha(qMax(0, c2.alpha() / 3));
            g.setColorAt(0.0, st.bgColor);
            g.setColorAt(1.0, c2);
            p.setBrush(g);
        } else if (st.bgStyle == core::SubtitleBg::Glass) {
            QColor c = st.bgColor;
            c.setAlpha(qMin(255, c.alpha() / 2 + 40));
            p.setBrush(c);
        } else {
            p.setBrush(st.bgColor);
        }
        p.drawRoundedRect(caixa, st.bgRadius, st.bgRadius);
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    double ty = st.paddingV;
    if (temNome) {
        p.setFont(fnome);
        p.setPen(st.nameColor);
        QRectF nameArea(st.paddingH, ty, cw - 2.0 * st.paddingH, fmNome.height());
        Qt::Alignment nameFlags = Qt::AlignVCenter | Qt::AlignLeft;
        if (st.nameAlign == core::SubtitleAlign::Center) nameFlags = Qt::AlignVCenter | Qt::AlignHCenter;
        if (st.nameAlign == core::SubtitleAlign::Right) nameFlags = Qt::AlignVCenter | Qt::AlignRight;
        p.drawText(nameArea, nameFlags, m_speaker);
        ty += fmNome.height();
    }
    game::TextDrawOpts opt;
    opt.color = st.fontColor;
    opt.outlineColor = st.outlineColor;
    opt.outlineWidth = st.outlineWidth;
    opt.shadow = st.shadow;
    opt.shadowColor = st.shadowColor;
    opt.shadowOffset = QPointF(st.shadowOffsetX, st.shadowOffsetY);
    opt.icons = &ed.iconSet;
    opt.time = m_clock ? m_clock->elapsed() / 1000.0 : 0.0;
    opt.effects = m_effects;
    opt.gradient = m_gradient;
    if (align == core::SubtitleAlign::Center) opt.align = game::TextAlign::Center;
    else if (align == core::SubtitleAlign::Right) opt.align = game::TextAlign::Right;
    else opt.align = game::TextAlign::Left;
    game::drawTextPage(p, pgs[0], f, QRectF(st.paddingH, ty, cw - 2.0 * st.paddingH, textMeasure.height()), opt);
}

SubtitleCommandDialog::SubtitleCommandDialog(core::Editor& editorRef, core::EventCommand& cmd,
                                             QWidget* parent)
    : QDialog(parent), ed(editorRef), m_cmd(cmd)
{
    setWindowTitle(tr("Mostrar legenda"));
    // RC2.64: sem preview lateral, o comando não precisa ocupar a largura
    // da resolução do jogo + uma segunda coluna inteira.
    resize(780, qMin(860, qMax(680, ed.gameResolution.height() + 80)));
    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);
    auto* scroll = new QScrollArea(this);
    scroll->setMinimumWidth(540);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* v = new QVBoxLayout(page);
    scroll->setWidget(page);
    body->addWidget(scroll);

    auto* hint = new QLabel(tr("Legenda não é a caixa de mensagem: ela tem <b>duração própria</b>, "
                               "pode aparecer várias ao mesmo tempo e pode <b>seguir um "
                               "personagem</b>. Os ajustes de aparência ficam em "
                               "<i>Jogar ▸ Configurações do Jogo… ▸ Jogabilidade</i>."), page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    const QVariantMap& pr = cmd.params;
    m_edit = new QPlainTextEdit(pr.value(QStringLiteral("text")).toString(), page);
    m_edit->setMinimumHeight(90);
    v->addWidget(m_edit);

    auto* codes = new QHBoxLayout;
    auto addCode = [&](const QString& rotulo, const QString& codigo, const QString& dica) {
        auto* b = new QPushButton(rotulo, page);
        b->setToolTip(dica);
        connect(b, &QPushButton::clicked, this, [this, codigo] {
            m_edit->insertPlainText(codigo);
            m_edit->setFocus();
        });
        codes->addWidget(b);
    };
    addCode(tr("Cor"), QStringLiteral("\\c[1]"), tr("\\c[n] muda a cor"));
    addCode(tr("Onda"), QStringLiteral("\\WV[4,6]"), tr("\\WV[amplitude,velocidade]"));
    addCode(tr("Tremida"), QStringLiteral("\\SK[3]"), tr("\\SK[intensidade]"));
    addCode(tr("Arco-íris"), QStringLiteral("\\RB[90]"), tr("\\RB[velocidade]"));
    addCode(tr("Flash"), QStringLiteral("\\FL[255,0,0,30]"), tr("\\FL[r,g,b,velocidade]"));
    addCode(tr("Glitch"), QStringLiteral("\\GL[8,3]"), tr("\\GL[intensidade,frequência]"));
    addCode(tr("Normal"), QStringLiteral("\\X"), tr("\\X desliga o efeito daqui em diante"));
    codes->addStretch(1);
    v->addLayout(codes);

    auto* form = new QFormLayout;
    m_speaker = new QLineEdit(pr.value(QStringLiteral("speaker")).toString(), page);
    form->addRow(tr("Quem fala"), m_speaker);
    m_localizationKey = new QLineEdit(core::normalizeLocalizationKey(pr.value(QStringLiteral("localizationKey")).toString()), page);
    m_localizationKey->setPlaceholderText(tr("Ex.: cutscene.intro.subtitle.1"));
    m_speakerLocalizationKey = new QLineEdit(core::normalizeLocalizationKey(pr.value(QStringLiteral("speakerLocalizationKey")).toString()), page);
    m_speakerLocalizationKey->setPlaceholderText(tr("Ex.: npc.hero.name"));
    form->addRow(tr("Chave do texto (opcional)"), m_localizationKey);
    form->addRow(tr("Chave de quem fala (opcional)"), m_speakerLocalizationKey);

    m_track=new QComboBox(page);m_track->addItem(tr("Diálogo"),QStringLiteral("dialogue"));m_track->addItem(tr("Notificação"),QStringLiteral("notification"));m_track->addItem(tr("Sistema"),QStringLiteral("system"));m_track->addItem(tr("Personalizada 1"),QStringLiteral("custom1"));m_track->addItem(tr("Personalizada 2"),QStringLiteral("custom2"));m_track->setCurrentIndex(qMax(0,m_track->findData(pr.value(QStringLiteral("track"),QStringLiteral("dialogue")).toString())));form->addRow(tr("Canal de legenda"),m_track);

    m_pos = new QComboBox(page);
    m_pos->addItem(tr("Embaixo"), QStringLiteral("bottom"));
    m_pos->addItem(tr("No meio"), QStringLiteral("middle"));
    m_pos->addItem(tr("Em cima"), QStringLiteral("top"));
    m_pos->setCurrentIndex(qMax(0, m_pos->findData(
        pr.value(QStringLiteral("position"), core::subtitlePositionId(ed.subtitleStyle.position)).toString())));
    form->addRow(tr("Posição"), m_pos);

    m_align = new QComboBox(page);
    m_align->addItem(tr("Centralizado"), QStringLiteral("center"));
    m_align->addItem(tr("Esquerda"), QStringLiteral("left"));
    m_align->addItem(tr("Direita"), QStringLiteral("right"));
    m_align->setCurrentIndex(qMax(0, m_align->findData(
        pr.value(QStringLiteral("textAlign"), core::subtitleAlignId(ed.subtitleStyle.textAlign)).toString())));
    form->addRow(tr("Alinhamento do texto"), m_align);

    m_anchor = new QComboBox(page);
    m_anchor->addItem(tr("Fixa na tela"), QStringLiteral("screen"));
    m_anchor->addItem(tr("Segue o jogador"), QStringLiteral("player"));
    m_anchor->addItem(tr("Segue um evento"), QStringLiteral("event"));
    m_anchor->setCurrentIndex(qMax(0, m_anchor->findData(
        pr.value(QStringLiteral("anchor"), QStringLiteral("screen")).toString())));
    form->addRow(tr("Ancoragem"), m_anchor);

    m_anchorEvent = new QLineEdit(pr.value(QStringLiteral("anchorEventId")).toString(), page);
    m_anchorEvent->setPlaceholderText(tr("nome ou id do evento"));
    form->addRow(tr("Evento da âncora"), m_anchorEvent);

    auto* offsets = new QHBoxLayout;
    m_offsetX = new QSpinBox(page);
    m_offsetX->setRange(-4096, 4096);
    m_offsetX->setSuffix(QStringLiteral(" px"));
    m_offsetX->setValue(pr.value(QStringLiteral("offsetX")).toInt());
    m_offsetY = new QSpinBox(page);
    m_offsetY->setRange(-4096, 4096);
    m_offsetY->setSuffix(QStringLiteral(" px"));
    m_offsetY->setValue(pr.value(QStringLiteral("offsetY")).toInt());
    offsets->addWidget(new QLabel(tr("X:"), page));
    offsets->addWidget(m_offsetX);
    offsets->addWidget(new QLabel(tr("Y:"), page));
    offsets->addWidget(m_offsetY);
    offsets->addStretch(1);
    form->addRow(tr("Ajuste da posição da legenda"), offsets);

    m_duration = new QSpinBox(page);
    m_duration->setRange(0, 6000);
    m_duration->setSuffix(tr(" quadros (60 = 1 s)"));
    m_duration->setSpecialValueText(tr("automática pelo texto"));
    m_duration->setValue(pr.value(QStringLiteral("duration"), ed.subtitleStyle.defaultDuration).toInt());
    form->addRow(tr("Duração"), m_duration);

    m_waitInput = new QCheckBox(tr("Só sai quando o jogador confirmar"), page);
    m_waitInput->setChecked(pr.value(QStringLiteral("waitForInput"),
                                     ed.subtitleStyle.waitForInput).toBool());
    m_waitEnd = new QCheckBox(tr("O evento espera esta legenda terminar"), page);
    m_waitEnd->setChecked(pr.value(QStringLiteral("waitForEnd"), true).toBool());
    m_typewriter = new QCheckBox(tr("Digitar letra a letra"), page);
    m_typewriter->setChecked(pr.value(QStringLiteral("typewriter"),
                                      ed.subtitleStyle.typewriter).toBool());
    form->addRow(QString(), m_waitInput);
    form->addRow(QString(), m_waitEnd);
    form->addRow(QString(), m_typewriter);

    auto* trans = new QHBoxLayout;
    m_transIn = new QComboBox(page);
    m_transOut = new QComboBox(page);
    const QVector<core::SubtitleTransition> todas = {
        core::SubtitleTransition::None, core::SubtitleTransition::SlideUp,
        core::SubtitleTransition::SlideDown, core::SubtitleTransition::SlideLeft,
        core::SubtitleTransition::SlideRight, core::SubtitleTransition::ZoomIn,
        core::SubtitleTransition::ZoomOut, core::SubtitleTransition::Bounce,
        core::SubtitleTransition::FlipX, core::SubtitleTransition::FlipY };
    const QStringList transitionLabels={tr("Nenhuma"),tr("Deslizar para cima"),tr("Deslizar para baixo"),tr("Deslizar para a esquerda"),tr("Deslizar para a direita"),tr("Aproximar"),tr("Afastar"),tr("Quicar"),tr("Virar na horizontal"),tr("Virar na vertical")};
    for(int i=0;i<todas.size();++i){const QString id=core::subtitleTransitionId(todas.at(i));const QString label=transitionLabels.value(i,id);m_transIn->addItem(label,id);m_transOut->addItem(label,id);}
    m_transIn->setCurrentIndex(qMax(0, m_transIn->findData(
        pr.value(QStringLiteral("transitionIn"),
                 core::subtitleTransitionId(ed.subtitleStyle.transitionIn)).toString())));
    m_transOut->setCurrentIndex(qMax(0, m_transOut->findData(
        pr.value(QStringLiteral("transitionOut"),
                 core::subtitleTransitionId(ed.subtitleStyle.transitionOut)).toString())));
    trans->addWidget(new QLabel(tr("Entrada:"), page));
    trans->addWidget(m_transIn);
    trans->addWidget(new QLabel(tr("Saída:"), page));
    trans->addWidget(m_transOut);
    trans->addStretch(1);
    form->addRow(tr("Transições"), trans);

    m_voice = new QLineEdit(pr.value(QStringLiteral("voiceFile")).toString(), page);
    m_voice->setPlaceholderText(tr("caminho do arquivo de voz (opcional)"));
    auto* voiceRow = new QHBoxLayout;
    voiceRow->addWidget(m_voice);
    auto* pickVoice = new QPushButton(tr("Procurar…"), page);
    voiceRow->addWidget(pickVoice);
    form->addRow(tr("Voz da fala"), voiceRow);
    connect(pickVoice, &QPushButton::clicked, this, [this] {
        QString source = m_voice->text();
        int volume = 100;
        if (chooseGameAudio(ed, this, tr("Selecionar Voice"), source, volume,
                            QStringLiteral("Voice")))
            m_voice->setText(source);
    });
    v->addLayout(form);
    m_textEffects = new TextEffectsEditorWidget(
        ed, core::TextEffectStack::fromVariantMap(pr.value(QStringLiteral("textEffects")).toMap()),
        core::TextGradientSpec::fromVariantMap(pr.value(QStringLiteral("textGradient")).toMap()), page);
    m_textEffects->setSampleText(m_edit->toPlainText());
    v->addWidget(m_textEffects);
    connect(m_textEffects, &TextEffectsEditorWidget::changed, this, [this] { refresh(); });

    auto* previewScroll = new QScrollArea(this);
    previewScroll->setWidgetResizable(false);
    m_preview = new SubtitlePreview(ed, previewScroll);
    previewScroll->setWidget(m_preview);
    m_info = new QLabel(this);
    m_info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    auto* previewDialog = new CommandPreviewDialog(
        tr("Prévia da legenda · %1 × %2 px").arg(ed.gameResolution.width()).arg(ed.gameResolution.height()),
        previewScroll, this, m_info);
    auto* previewButton = new QPushButton(tr("Ver prévia"), page);
    previewButton->setToolTip(tr("Abre uma prévia em uma janela separada."));
    v->addWidget(previewButton);
    connect(previewButton, &QPushButton::clicked, previewDialog, &CommandPreviewDialog::present);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(box);
    m_box = box;
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    protegerDaRoda(this);
    connect(m_edit, &QPlainTextEdit::textChanged, this, &SubtitleCommandDialog::refresh);
    connect(m_speaker, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
    connect(m_pos, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_track, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_anchor, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_offsetX, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(m_offsetY, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(this, &QDialog::accepted, this, [this] {
        const bool queued=m_cmd.type==QLatin1String("subtitle.enqueue");m_cmd.type = queued?QStringLiteral("subtitle.enqueue"):QStringLiteral("subtitle.show");
        QVariantMap p;
        p[QStringLiteral("text")] = m_edit->toPlainText();
        p[QStringLiteral("track")]=m_track->currentData().toString();
        const QString textKey=core::normalizeLocalizationKey(m_localizationKey->text()); if(!textKey.isEmpty())p[QStringLiteral("localizationKey")]=textKey;
        if (!m_speaker->text().isEmpty()) p[QStringLiteral("speaker")] = m_speaker->text();
        const QString speakerKey=core::normalizeLocalizationKey(m_speakerLocalizationKey->text()); if(!speakerKey.isEmpty())p[QStringLiteral("speakerLocalizationKey")]=speakerKey;
        p[QStringLiteral("position")] = m_pos->currentData().toString();
        p[QStringLiteral("textAlign")] = m_align->currentData().toString();
        const QString anc = m_anchor->currentData().toString();
        if (anc != QLatin1String("screen")) {
            p[QStringLiteral("anchor")] = anc;
            if (anc == QLatin1String("event"))
                p[QStringLiteral("anchorEventId")] = m_anchorEvent->text().trimmed();
        }
        p[QStringLiteral("duration")] = m_duration->value();
        p[QStringLiteral("waitForInput")] = m_waitInput->isChecked();
        p[QStringLiteral("waitForEnd")] = m_waitEnd->isChecked();
        p[QStringLiteral("typewriter")] = m_typewriter->isChecked();
        p[QStringLiteral("offsetX")] = m_offsetX->value();
        p[QStringLiteral("offsetY")] = m_offsetY->value();
        p[QStringLiteral("transitionIn")] = m_transIn->currentData().toString();
        p[QStringLiteral("transitionOut")] = m_transOut->currentData().toString();
        if (!m_voice->text().isEmpty()) p[QStringLiteral("voiceFile")] = m_voice->text();
        const auto effects = m_textEffects->effects();
        const auto gradient = m_textEffects->gradient();
        if (effects.enabled()) p[QStringLiteral("textEffects")] = effects.toVariantMap();
        if (gradient.enabled()) p[QStringLiteral("textGradient")] = gradient.toVariantMap();
        m_cmd.params = p;
    });
    refresh();
}

void SubtitleCommandDialog::refresh()
{
    m_anchorEvent->setEnabled(m_anchor->currentData().toString() == QLatin1String("event"));
    m_preview->setTextEffects(m_textEffects->effects(), m_textEffects->gradient());
    m_preview->setStyleOverride(ed.subtitleStyle.resolvedForTrack(core::subtitleTrackFromId(m_track->currentData().toString())));
    m_preview->setRequest(m_edit->toPlainText(), m_speaker->text(),
                          m_pos->currentData().toString(), m_align->currentData().toString(),
                          m_offsetX->value(), m_offsetY->value());
    m_textEffects->setSampleText(m_edit->toPlainText());
    const int dur = m_duration->value();
    m_info->setText(dur == 0
        ? tr("Duração automática: texto ÷ caracteres/s, respeitando o mínimo da track.")
        : tr("Duração: %1 quadros ≈ %2 s (+ %3 de fade).")
              .arg(dur).arg(QString::number(dur / 60.0, 'f', 1))
              .arg(QString::number((ed.subtitleStyle.fadeInFrames
                                    + ed.subtitleStyle.fadeOutFrames) / 60.0, 'f', 1)));
}

SubtitleStyleDialog::SubtitleStyleDialog(core::Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef), m_target(&editorRef.subtitleStyle), m_markProjectDirty(true)
{
    build();
}

SubtitleStyleDialog::SubtitleStyleDialog(core::Editor& editorRef, core::SubtitleStyle& target, QWidget* parent)
    : QDialog(parent), ed(editorRef), m_target(&target), m_markProjectDirty(false)
{
    build();
}

void SubtitleStyleDialog::build()
{
    setWindowTitle(tr("Configurar legendas"));
    resize(qMin(1600, ed.gameResolution.width() + 680),
           qMin(920, qMax(700, ed.gameResolution.height() + 100)));
    core::SubtitleStyle st = m_target ? *m_target : ed.subtitleStyle;

    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* form = new QFormLayout(page);
    scroll->setWidget(page);
    scroll->setMinimumWidth(520);
    body->addWidget(scroll, 0);

    auto spin = [&](int valor, int min, int max, const QString& sufixo = QString()) {
        auto* s = new QSpinBox(page);
        s->setRange(min, max);
        s->setValue(valor);
        if (!sufixo.isEmpty()) s->setSuffix(sufixo);
        return s;
    };
    // Botão de cor com amostra: só o código hexadecimal não diz nada a
    // ninguém, ainda mais com alfa no meio (#AARRGGBB).
    auto pintarBotao = [](QPushButton* b, const QColor& c) {
        QPixmap pm(18, 18);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.fillRect(0, 0, 18, 18, QColor("#2a2a2a"));
            for (int y = 0; y < 18; y += 6)              // xadrez: mostra o alfa
                for (int x = 0; x < 18; x += 6)
                    if (((x / 6) + (y / 6)) % 2) p.fillRect(x, y, 6, 6, QColor("#3d3d3d"));
            p.fillRect(0, 0, 18, 18, c);
            p.setPen(QColor("#666"));
            p.drawRect(0, 0, 17, 17);
        }
        b->setIcon(QIcon(pm));
        b->setText(QStringLiteral("%1  (alfa %2)").arg(c.name(QColor::HexRgb)).arg(c.alpha()));
    };
    auto corBotao = [&, pintarBotao](QColor* alvo) {
        auto* b = new QPushButton(page);
        b->setStyleSheet(QStringLiteral("text-align:left;padding-left:8px"));
        pintarBotao(b, *alvo);
        connect(b, &QPushButton::clicked, this, [this, b, alvo, pintarBotao] {
            const QColor c = QColorDialog::getColor(*alvo, this, tr("Cor"),
                                                    QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            *alvo = c;
            pintarBotao(b, c);
        });
        return b;
    };

    auto* dur = spin(st.defaultDuration, 10, 6000, tr(" quadros"));
    auto* fin = spin(st.fadeInFrames, 0, 300, tr(" quadros"));
    auto* fout = spin(st.fadeOutFrames, 0, 300, tr(" quadros"));
    auto* fontFamily=new QComboBox(page);fontFamily->addItem(tr("(fonte principal do projeto)"),QString());
    for(const core::ProjectFont& pf:ed.projectFonts){QFontDatabase::addApplicationFont(QDir(ed.projectRoot()).filePath(pf.sourcePath));if(fontFamily->findData(pf.family)<0)fontFamily->addItem(pf.family,pf.family);}
    fontFamily->setCurrentIndex(qMax(0,fontFamily->findData(st.fontFamily)));
    auto* fsize = spin(st.fontSize, 8, 96, tr(" px"));
    auto* nsize = spin(st.nameFontSize, 8, 96, tr(" px"));
    auto* owidth = spin(st.outlineWidth, 0, 8, tr(" px"));
    auto* maxw = spin(st.maxWidth, 100, 4000, tr(" px"));
    auto* padH = spin(st.paddingH, 0, 200, tr(" px"));
    auto* padV = spin(st.paddingV, 0, 200, tr(" px"));
    auto* marB = spin(st.marginBottom, 0, 400, tr(" px"));
    auto* marH = spin(st.marginHorizontal, 0, 400, tr(" px"));
    auto* radius = spin(st.bgRadius, 0, 60, tr(" px"));
    auto* maxSim = spin(st.maxSimultaneous, 1, 10);
    auto* tySpeed = spin(int(st.typewriterSpeed), 5, 300, tr(" letras/s"));

    auto* bg = new QComboBox(page);
    bg->addItem(tr("Sólido"), QStringLiteral("solid"));
    bg->addItem(tr("Gradiente"), QStringLiteral("gradient"));
    bg->addItem(tr("Vidro"), QStringLiteral("glass"));
    bg->addItem(tr("Nenhum"), QStringLiteral("none"));
    bg->setCurrentIndex(qMax(0, bg->findData(core::subtitleBgId(st.bgStyle))));

    auto* pos = new QComboBox(page);
    pos->addItem(tr("Embaixo"), QStringLiteral("bottom"));
    pos->addItem(tr("No meio"), QStringLiteral("middle"));
    pos->addItem(tr("Em cima"), QStringLiteral("top"));
    pos->setCurrentIndex(qMax(0, pos->findData(core::subtitlePositionId(st.position))));

    auto* talign = new QComboBox(page);
    talign->addItem(tr("Centralizado"), QStringLiteral("center"));
    talign->addItem(tr("Esquerda"), QStringLiteral("left"));
    talign->addItem(tr("Direita"), QStringLiteral("right"));
    talign->setCurrentIndex(qMax(0, talign->findData(core::subtitleAlignId(st.textAlign))));
    auto* nameAlign = new QComboBox(page);
    nameAlign->addItem(tr("Esquerda"), QStringLiteral("left"));
    nameAlign->addItem(tr("Centro"), QStringLiteral("center"));
    nameAlign->addItem(tr("Direita"), QStringLiteral("right"));
    nameAlign->setCurrentIndex(qMax(0, nameAlign->findData(core::subtitleAlignId(st.nameAlign))));

    auto* negrito = new QCheckBox(tr("Negrito"), page);
    negrito->setChecked(st.fontBold);
    auto* italico = new QCheckBox(tr("Itálico"), page);
    italico->setChecked(st.fontItalic);
    auto* sombra = new QCheckBox(tr("Sombra no texto"), page);
    sombra->setChecked(st.shadow);
    auto* typew = new QCheckBox(tr("Digitar letra a letra por padrão"), page);
    typew->setChecked(st.typewriter);
    auto* espera = new QCheckBox(tr("Esperar o jogador confirmar por padrão"), page);
    espera->setChecked(st.waitForInput);
    auto* congela = new QCheckBox(tr("Congelar o mapa enquanto houver legenda"), page);
    congela->setChecked(st.freezeMap);
    auto* indicador = new QCheckBox(tr("Mostrar ▼ ao esperar confirmação"), page);
    indicador->setChecked(st.inputIndicator);

    // Membros (não locais!): os lambdas abaixo rodam DEPOIS do construtor.
    m_fontColor = st.fontColor;
    m_nameColor = st.nameColor;
    m_outlineColor = st.outlineColor;
    m_bgColor = st.bgColor;

    form->addRow(tr("Duração padrão"), dur);
    form->addRow(tr("Fade de entrada"), fin);
    form->addRow(tr("Fade de saída"), fout);
    form->addRow(tr("Fonte"), fontFamily);
    form->addRow(tr("Tamanho da fonte"), fsize);
    auto* fontColorButton = corBotao(&m_fontColor);
    form->addRow(tr("Cor do texto"), fontColorButton);
    form->addRow(QString(), negrito);
    form->addRow(QString(), italico);
    form->addRow(tr("Contorno"), owidth);
    auto* outlineColorButton = corBotao(&m_outlineColor);
    form->addRow(tr("Cor do contorno"), outlineColorButton);
    form->addRow(QString(), sombra);
    form->addRow(tr("Tamanho do nome"), nsize);
    auto* nameColorButton = corBotao(&m_nameColor);
    form->addRow(tr("Cor do nome"), nameColorButton);
    form->addRow(tr("Alinhamento do nome"), nameAlign);
    form->addRow(tr("Fundo"), bg);
    auto* bgColorButton = corBotao(&m_bgColor);
    form->addRow(tr("Cor do fundo"), bgColorButton);
    form->addRow(tr("Cantos do fundo"), radius);
    form->addRow(tr("Largura máxima"), maxw);
    form->addRow(tr("Padding horizontal"), padH);
    form->addRow(tr("Padding vertical"), padV);
    form->addRow(tr("Margem de baixo"), marB);
    form->addRow(tr("Margem lateral"), marH);
    form->addRow(tr("Posição"), pos);
    form->addRow(tr("Alinhamento do texto"), talign);
    form->addRow(tr("Legendas simultâneas"), maxSim);
    form->addRow(tr("Velocidade da digitação"), tySpeed);
    form->addRow(QString(), typew);
    form->addRow(QString(), espera);
    form->addRow(QString(), congela);
    form->addRow(QString(), indicador);

    auto trackEdits=std::make_shared<QHash<QString,core::SubtitleTrackSettings>>(st.tracks);auto currentTrack=std::make_shared<QString>(QStringLiteral("dialogue"));auto* track=new QComboBox(page);track->addItem(tr("Diálogo"),QStringLiteral("dialogue"));track->addItem(tr("Notificação"),QStringLiteral("notification"));track->addItem(tr("Sistema"),QStringLiteral("system"));track->addItem(tr("Personalizada 1"),QStringLiteral("custom1"));track->addItem(tr("Personalizada 2"),QStringLiteral("custom2"));auto* trackMax=spin(st.trackSettings(core::SubtitleTrack::Dialogue).maxSimultaneous,1,20);auto* trackMin=spin(st.trackSettings(core::SubtitleTrack::Dialogue).minDurationFrames,1,36000,tr(" quadros"));auto* trackCps=spin(int(st.trackSettings(core::SubtitleTrack::Dialogue).charsPerSecond),1,1000,tr(" caracteres/s"));auto* copyTrackStyle=new QPushButton(tr("Usar o visual atual nesta track"),page);auto* resetTrackStyle=new QPushButton(tr("Herdar visual global"),page);auto* trackButtons=new QHBoxLayout;trackButtons->addWidget(copyTrackStyle);trackButtons->addWidget(resetTrackStyle);form->addRow(tr("Configurar track"),track);form->addRow(tr("Limite desta track"),trackMax);form->addRow(tr("Duração automática mínima"),trackMin);form->addRow(tr("Leitura automática"),trackCps);form->addRow(tr("Estilo desta track"),trackButtons);
    auto saveTrack=[=]{core::SubtitleTrackSettings settings=trackEdits->value(*currentTrack);settings.maxSimultaneous=trackMax->value();settings.minDurationFrames=trackMin->value();settings.charsPerSecond=trackCps->value();trackEdits->insert(*currentTrack,settings);};auto loadTrack=[=](const QString&id){const core::SubtitleTrackSettings settings=trackEdits->value(id,st.trackSettings(core::subtitleTrackFromId(id)));trackMax->setValue(settings.maxSimultaneous);trackMin->setValue(settings.minDurationFrames);trackCps->setValue(qRound(settings.charsPerSecond));};connect(track,&QComboBox::currentIndexChanged,this,[=](int){saveTrack();*currentTrack=track->currentData().toString();loadTrack(*currentTrack);});connect(copyTrackStyle,&QPushButton::clicked,this,[=]{saveTrack();core::SubtitleTrackSettings settings=trackEdits->value(*currentTrack);QJsonObject visual=st.toJson();visual.remove(QStringLiteral("tracks"));settings.styleOverrides=visual;trackEdits->insert(*currentTrack,settings);});connect(resetTrackStyle,&QPushButton::clicked,this,[=]{saveTrack();core::SubtitleTrackSettings settings=trackEdits->value(*currentTrack);settings.styleOverrides={};trackEdits->insert(*currentTrack,settings);});

    // Preview sempre à direita, seguindo a regra visual da LUDO para comandos
    // e configurações que alteram aparência. Ele recebe uma cópia temporária
    // do estilo, então mexer nos controles não modifica o projeto antes do OK.
    auto* previewColumn = new QVBoxLayout;
    auto* previewTitle = new QLabel(tr("Prévia · %1 × %2 px")
        .arg(ed.gameResolution.width()).arg(ed.gameResolution.height()), this);
    previewColumn->addWidget(previewTitle);
    auto* previewScroll = new QScrollArea(this);
    previewScroll->setWidgetResizable(false);
    auto* stylePreview = new SubtitlePreview(ed, previewScroll);
    previewScroll->setWidget(stylePreview);
    previewColumn->addWidget(previewScroll, 1);
    auto* previewHint = new QLabel(tr("Exemplo em tempo real — o projeto só é alterado ao confirmar."), this);
    previewHint->setWordWrap(true);
    previewHint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    previewColumn->addWidget(previewHint);
    body->addLayout(previewColumn, 1);

    auto makeStyle = [=]() {
        core::SubtitleStyle n = m_target ? *m_target : ed.subtitleStyle;
        n.defaultDuration = dur->value();
        n.fadeInFrames = fin->value();
        n.fadeOutFrames = fout->value();
        n.fontFamily = fontFamily->currentData().toString();
        n.fontSize = fsize->value();
        n.fontColor = m_fontColor;
        n.fontBold = negrito->isChecked();
        n.fontItalic = italico->isChecked();
        n.outlineWidth = owidth->value();
        n.outlineColor = m_outlineColor;
        n.shadow = sombra->isChecked();
        n.nameFontSize = nsize->value();
        n.nameColor = m_nameColor;
        n.nameAlign = core::subtitleAlignFromId(nameAlign->currentData().toString());
        n.bgStyle = core::subtitleBgFromId(bg->currentData().toString());
        n.bgColor = m_bgColor;
        n.bgRadius = radius->value();
        n.maxWidth = maxw->value();
        n.paddingH = padH->value();
        n.paddingV = padV->value();
        n.marginBottom = marB->value();
        n.marginHorizontal = marH->value();
        n.position = core::subtitlePositionFromId(pos->currentData().toString());
        n.textAlign = core::subtitleAlignFromId(talign->currentData().toString());
        n.maxSimultaneous = maxSim->value();
        n.typewriterSpeed = tySpeed->value();
        n.typewriter = typew->isChecked();
        n.waitForInput = espera->isChecked();
        n.freezeMap = congela->isChecked();
        n.inputIndicator = indicador->isChecked();
        saveTrack();n.tracks=*trackEdits;
        return n;
    };
    auto refreshPreview = [=]() {
        const core::SubtitleStyle n = makeStyle();
        stylePreview->setStyleOverride(n);
        stylePreview->setRequest(tr("Assim a legenda vai aparecer no jogo."), tr("Personagem"),
                                 core::subtitlePositionId(n.position), core::subtitleAlignId(n.textAlign));
    };
    const auto spins = page->findChildren<QSpinBox*>();
    for (QSpinBox* spinBox : spins) connect(spinBox, &QSpinBox::valueChanged, this, [refreshPreview](int){ refreshPreview(); });
    const auto combos = page->findChildren<QComboBox*>();
    for (QComboBox* combo : combos) connect(combo, &QComboBox::currentIndexChanged, this, [refreshPreview](int){ refreshPreview(); });
    const auto checks = page->findChildren<QCheckBox*>();
    for (QCheckBox* check : checks) connect(check, &QCheckBox::toggled, this, [refreshPreview](bool){ refreshPreview(); });
    for (QPushButton* colorButton : {fontColorButton, outlineColorButton, nameColorButton, bgColorButton})
        connect(colorButton, &QPushButton::clicked, this, [refreshPreview]{ QTimer::singleShot(0, refreshPreview); });
    refreshPreview();

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(box);
    protegerDaRoda(this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(this, &QDialog::accepted, this, [=] {
        const core::SubtitleStyle n = makeStyle();
        if (m_target) *m_target = n;
        if (m_markProjectDirty) ed.markDirty();
    });
}

// ============================================================================
//  Banco de dados: interruptores e variáveis
// ============================================================================
GameDataDialog::GameDataDialog(core::Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Switches, variáveis e strings"));
    resize(980, 580);
    auto* v = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Aqui você define <b>como a memória do jogo começa</b>. "
                               "Durante a partida os valores mudam pelos comandos de evento — "
                               "e o projeto nunca é alterado por jogar."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    auto* linha = new QHBoxLayout;
    auto montaTabela = [&](const QString& titulo, const QString& colValor) {
        auto* caixa = new QGroupBox(titulo, this);
        auto* cv = new QVBoxLayout(caixa);
        auto* t = new QTableWidget(caixa);
        t->setColumnCount(3);
        t->setHorizontalHeaderLabels({ tr("Nº"), tr("Nome"), colValor });
        t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        t->verticalHeader()->setVisible(false);
        cv->addWidget(t);
        auto* botoes = new QHBoxLayout;
        auto* add = new QPushButton(tr("Adicionar"), caixa);
        auto* rem = new QPushButton(tr("Remover"), caixa);
        botoes->addWidget(add);
        botoes->addWidget(rem);
        botoes->addStretch(1);
        cv->addLayout(botoes);
        linha->addWidget(caixa);
        return std::make_tuple(t, add, rem);
    };
    auto [tsw, addSw, remSw] = montaTabela(tr("Interruptores"), tr("Começa"));
    auto [tva, addVa, remVa] = montaTabela(tr("Variáveis"), tr("Começa"));
    auto [tst, addSt, remSt] = montaTabela(tr("Strings"), tr("Texto inicial"));
    m_switches = tsw;
    m_vars = tva;
    m_strings = tst;
    v->addLayout(linha, 1);

    const auto confirmReferencedRemoval=[this](core::ReferenceSymbolKind kind,const QString& id,const QString& label){
        const int uses=core::findProjectUses(ed,kind,id).size();
        if(uses<=0)return true;
        return QMessageBox::warning(this,tr("Referência em uso"),
            tr("“%1” ainda é usado no projeto.\n\nUsos encontrados: %2. Se você remover agora, esses locais deixarão de funcionar. Use Editar → Busca Global → Encontrar usos para revisá-los antes.\n\nRemover mesmo assim?").arg(label).arg(uses),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
    };

    connect(addSw, &QPushButton::clicked, this, [this] {
        int maior = 0;
        for (const core::SwitchDef& s : ed.switches) maior = qMax(maior, s.id);
        ed.switches.push_back({ maior + 1, tr("Interruptor %1").arg(maior + 1), false });
        ed.markDirty();
        recarregar();
    });
    connect(remSw, &QPushButton::clicked, this, [this,confirmReferencedRemoval] {
        const int r = m_switches->currentRow();
        if (r >= 0 && r < ed.switches.size()) {
            const core::SwitchDef def=ed.switches.at(r);
            if(!confirmReferencedRemoval(core::ReferenceSymbolKind::Switch,QString::number(def.id),def.name))return;
            ed.switches.remove(r);
            ed.markDirty();
            recarregar();
        }
    });
    connect(addVa, &QPushButton::clicked, this, [this] {
        int maior = 0;
        for (const core::VariableDef& v2 : ed.variables) maior = qMax(maior, v2.id);
        ed.variables.push_back({ maior + 1, tr("Variável %1").arg(maior + 1), 0 });
        ed.markDirty();
        recarregar();
    });
    connect(remVa, &QPushButton::clicked, this, [this,confirmReferencedRemoval] {
        const int r = m_vars->currentRow();
        if (r >= 0 && r < ed.variables.size()) {
            const core::VariableDef def=ed.variables.at(r);
            if(!confirmReferencedRemoval(core::ReferenceSymbolKind::Variable,QString::number(def.id),def.name))return;
            ed.variables.remove(r);
            ed.markDirty();
            recarregar();
        }
    });
    connect(addSt, &QPushButton::clicked, this, [this] {
        int maior = 0;
        for (const core::StringDef& value : ed.strings) maior = qMax(maior, value.id);
        ed.strings.push_back({ maior + 1, tr("String %1").arg(maior + 1), QString() });
        ed.markDirty();
        recarregar();
    });
    connect(remSt, &QPushButton::clicked, this, [this,confirmReferencedRemoval] {
        const int r = m_strings->currentRow();
        if (r >= 0 && r < ed.strings.size()) {
            const core::StringDef def=ed.strings.at(r);
            if(!confirmReferencedRemoval(core::ReferenceSymbolKind::String,QString::number(def.id),def.name))return;
            ed.strings.remove(r);
            ed.markDirty();
            recarregar();
        }
    });
    // Editar direto na tabela grava no projeto (sem botão "aplicar").
    connect(m_switches, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        const int r = it->row();
        if (r < 0 || r >= ed.switches.size()) return;
        if (it->column() == 1) ed.switches[r].name = it->text();
        if (it->column() == 2) ed.switches[r].initial = (it->checkState() == Qt::Checked);
        ed.markDirty();
    });
    connect(m_vars, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        const int r = it->row();
        if (r < 0 || r >= ed.variables.size()) return;
        if (it->column() == 1) ed.variables[r].name = it->text();
        if (it->column() == 2) ed.variables[r].initial = it->text().toInt();
        ed.markDirty();
    });
    connect(m_strings, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        const int r = it->row();
        if (r < 0 || r >= ed.strings.size()) return;
        if (it->column() == 1) ed.strings[r].name = it->text();
        if (it->column() == 2) ed.strings[r].initial = it->text().left(65535);
        ed.markDirty();
    });

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    recarregar();
}

void GameDataDialog::recarregar()
{
    QSignalBlocker b1(m_switches), b2(m_vars), b3(m_strings);
    m_switches->setRowCount(ed.switches.size());
    for (int i = 0; i < ed.switches.size(); ++i) {
        const core::SwitchDef& s = ed.switches[i];
        auto* num = new QTableWidgetItem(QString::number(s.id));
        num->setFlags(num->flags() & ~Qt::ItemIsEditable);
        m_switches->setItem(i, 0, num);
        m_switches->setItem(i, 1, new QTableWidgetItem(s.name));
        auto* val = new QTableWidgetItem(tr("ligado"));
        val->setFlags((val->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        val->setCheckState(s.initial ? Qt::Checked : Qt::Unchecked);
        m_switches->setItem(i, 2, val);
    }
    m_vars->setRowCount(ed.variables.size());
    for (int i = 0; i < ed.variables.size(); ++i) {
        const core::VariableDef& v = ed.variables[i];
        auto* num = new QTableWidgetItem(QString::number(v.id));
        num->setFlags(num->flags() & ~Qt::ItemIsEditable);
        m_vars->setItem(i, 0, num);
        m_vars->setItem(i, 1, new QTableWidgetItem(v.name));
        m_vars->setItem(i, 2, new QTableWidgetItem(QString::number(v.initial)));
    }
    m_strings->setRowCount(ed.strings.size());
    for (int i = 0; i < ed.strings.size(); ++i) {
        const core::StringDef& value = ed.strings[i];
        auto* num = new QTableWidgetItem(QString::number(value.id));
        num->setFlags(num->flags() & ~Qt::ItemIsEditable);
        m_strings->setItem(i, 0, num);
        m_strings->setItem(i, 1, new QTableWidgetItem(value.name));
        m_strings->setItem(i, 2, new QTableWidgetItem(value.initial));
    }
}

// ============================================================================
//  Comandos de lógica
// ============================================================================
namespace {

/// Combo com todos os interruptores (ou variáveis) do projeto, mostrando
/// "3 — Ponte caiu" em vez de um número solto.
QComboBox* comboDe(QWidget* pai, const QStringList& rotulos, const QVector<int>& ids, int atual)
{
    auto* c = new QComboBox(pai);
    for (int i = 0; i < rotulos.size(); ++i) c->addItem(rotulos[i], ids[i]);
    const int idx = c->findData(atual);
    c->setCurrentIndex(idx >= 0 ? idx : 0);
    return c;
}

QComboBox* comboInterruptores(QWidget* pai, const core::Editor& ed, int atual)
{
    QStringList rot;
    QVector<int> ids;
    for (const core::SwitchDef& s : ed.switches) {
        rot << QStringLiteral("%1 — %2").arg(s.id).arg(s.name);
        ids << s.id;
    }
    if (rot.isEmpty()) { rot << QObject::tr("(nenhum criado)"); ids << 1; }
    return comboDe(pai, rot, ids, atual);
}

QComboBox* comboVariaveis(QWidget* pai, const core::Editor& ed, int atual)
{
    QStringList rot;
    QVector<int> ids;
    for (const core::VariableDef& v : ed.variables) {
        rot << QStringLiteral("%1 — %2").arg(v.id).arg(v.name);
        ids << v.id;
    }
    if (rot.isEmpty()) { rot << QObject::tr("(nenhuma criada)"); ids << 1; }
    return comboDe(pai, rot, ids, atual);
}

QComboBox* comboStrings(QWidget* pai, const core::Editor& ed, int atual)
{
    QStringList rot;
    QVector<int> ids;
    for (const core::StringDef& value : ed.strings) {
        rot << QStringLiteral("%1 — %2").arg(value.id).arg(value.name);
        ids << value.id;
    }
    if (rot.isEmpty()) { rot << QObject::tr("(nenhuma String criada)"); ids << 1; }
    return comboDe(pai, rot, ids, atual);
}

core::CommonValueType gameValueTypeForKey(const QString& key)
{
    return core::gameValueType(key);
}

void populateGameValueCombo(QComboBox* combo)
{
    for (const core::GameValueDescriptor& value : core::gameValueDescriptors())
        combo->addItem(value.label, value.key);
}

} // namespace

class ChoiceCommandPreview final : public QWidget
{
public:
    explicit ChoiceCommandPreview(core::Editor& editor, QWidget* parent=nullptr) : QWidget(parent), ed(editor)
    {
        setMinimumSize(360, 210); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        clock.start();
        auto* timer=new QTimer(this);
        connect(timer,&QTimer::timeout,this,QOverload<>::of(&QWidget::update));
        timer->start(33);
    }
    void setData(const QStringList& values,const QString& position,int ox,int oy,
                 const core::TextEffectStack& textEffects={}, const core::TextGradientSpec& textGradient={},
                 const QVector<bool>& disabledValues={}, const QString& layoutMode=QStringLiteral("vertical"),
                 int gridColumns=1, int gapX=6, int gapY=4, const QString& textAlignment=QStringLiteral("left"),
                 const QString& box=QStringLiteral("theme"), const QString& family=QString(), int sizePx=0)
    {
        choices=values; pos=position; offsetX=ox; offsetY=oy; effects=textEffects; gradient=textGradient;
        disabled=disabledValues; layout=layoutMode; columns=qMax(1,gridColumns); spacingX=qMax(0,gapX); spacingY=qMax(0,gapY);
        alignment=textAlignment; boxMode=box; fontFamily=family; fontSize=sizePx;
        clock.restart(); update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this); p.fillRect(rect(), QColor("#101722"));
        const QSize logical=ed.gameResolution.isValid()?ed.gameResolution:QSize(800,600);
        const qreal scale=qMin((width()-16.0)/logical.width(),(height()-16.0)/logical.height());
        const QSizeF display(logical.width()*scale,logical.height()*scale);
        const QRectF screenRect((width()-display.width())*.5,(height()-display.height())*.5,display.width(),display.height());
        p.fillRect(screenRect,QColor("#17202c"));
        if(choices.isEmpty())return;
        game::MessageStyle style; style.font.setPixelSize(qMax(6,ed.gameUi.fontSize));
        if(!fontFamily.isEmpty())style.font.setFamily(fontFamily);if(fontSize>0)style.font.setPixelSize(qBound(6,fontSize,96));
        game::ChoiceView view;view.visible=true;view.options=choices;view.position=pos;view.offsetX=offsetX;view.offsetY=offsetY;
        view.effects=effects;view.gradient=gradient;view.effectTimeSec=clock.elapsed()/1000.0;view.disabled=disabled;
        view.layout=layout;view.columns=columns;view.spacingX=spacingX;view.spacingY=spacingY;view.alignment=alignment;view.boxMode=boxMode;view.fontFamily=fontFamily;view.fontSize=fontSize;
        for(const QString& option:choices){
            const auto pages=game::layoutMessage(option,style.font,4000.0,1,{},&ed.iconSet);
            view.richPages.push_back(pages.isEmpty()?game::TextPage():pages.first());
        }
        const auto theme=game::ui::UiTheme::fromSettings(ed.gameUi);
        const auto windowStyle=game::ui::resolveNativeStyle(theme,ed,QStringLiteral("choices"),QStringLiteral("normal"),style.font);
        style.font=windowStyle.font;if(!fontFamily.isEmpty())style.font.setFamily(fontFamily);if(fontSize>0)style.font.setPixelSize(qBound(6,fontSize,96));
        const int px=windowStyle.paddingX+2,py=windowStyle.paddingY+2;
        const auto geo=game::ui::GameUiLayer::choiceGeometry(view,style,logical,px,py);
        if(geo.first.isEmpty())return;
        game::ui::UiDrawList list;const QRectF box=geo.first;const int rowH=geo.second;
        if(boxMode!=QLatin1String("transparent"))game::ui::appendResolvedBackground(list,box,theme,windowStyle);
        const int count=choices.size();int cols=1;if(layout==QLatin1String("horizontal"))cols=count;else if(layout==QLatin1String("grid"))cols=qBound(1,columns,count);
        const int contentW=qMax(1,int(box.width())-px*2-qMax(0,cols-1)*spacingX);const int cellW=qMax(1,contentW/cols);
        int align=Qt::AlignVCenter|Qt::AlignLeft;if(alignment==QLatin1String("center"))align=Qt::AlignCenter;else if(alignment==QLatin1String("right"))align=Qt::AlignVCenter|Qt::AlignRight;
        for(int i=0;i<count;++i){
            const int col=i%cols,rowIndex=i/cols;QRectF row(box.left()+px+col*(cellW+spacingX),box.top()+py+rowIndex*(rowH+spacingY),cellW,rowH);
            const bool blocked=disabled.value(i,false);const bool selected=i==0&&!blocked;const QString state=blocked?QStringLiteral("disabled"):(selected?QStringLiteral("selected"):QStringLiteral("normal"));
            const auto itemStyle=game::ui::resolveNativeStyle(theme,ed,QStringLiteral("choice-item"),state,style.font);game::ui::appendResolvedBackground(list,row,theme,itemStyle);
            const qreal leftPad=selected?30.0:8.0;const QRectF textRect=row.adjusted(leftPad,4,-6,-4);
            if(i<view.richPages.size()&&!view.richPages.at(i).lines.isEmpty()){game::TextDrawOpts opts;opts.color=itemStyle.text;opts.icons=&ed.iconSet;opts.time=view.effectTimeSec;opts.gradient=view.gradient;opts.effects=view.effects;opts.opacity=itemStyle.opacity;opts.align=alignment==QLatin1String("center")?game::TextAlign::Center:alignment==QLatin1String("right")?game::TextAlign::Right:game::TextAlign::Left;list.addRichText(textRect,view.richPages.at(i),itemStyle.font,opts);}
            else list.addText(textRect,choices.at(i),itemStyle.font,itemStyle.text,align,itemStyle.opacity);
            if(selected)list.addText(QRectF(row.left()+4,row.top(),20,row.height()),QStringLiteral("▶"),itemStyle.font,itemStyle.text,Qt::AlignCenter,1.0);
        }
        p.save();p.translate(screenRect.topLeft());p.scale(scale,scale);game::ui::UiPainterRenderer::render(p,list);p.restore();
        p.setPen(QColor(255,255,255,50));p.setBrush(Qt::NoBrush);p.drawRect(screenRect);
    }
private:
    core::Editor& ed;QStringList choices;QString pos=QStringLiteral("center-right");int offsetX=0,offsetY=0;
    core::TextEffectStack effects;core::TextGradientSpec gradient;QVector<bool> disabled;QElapsedTimer clock;
    QString layout=QStringLiteral("vertical");int columns=1,spacingX=6,spacingY=4;QString alignment=QStringLiteral("left");
    QString boxMode=QStringLiteral("theme"),fontFamily;int fontSize=0;
};

namespace {

const core::CommonValueType* commonUiValueType(const core::CommonEvent* event, const QString& id, core::CommonValueType* storage)
{
    if (!event || id.isEmpty() || !storage) return nullptr;
    for (const core::CommonEventParameter& def : event->parameters)
        if (def.id == id) { *storage = def.type; return storage; }
    for (const core::CommonEventLocal& def : event->locals)
        if (def.id == id) { *storage = def.type; return storage; }
    return nullptr;
}

struct CommonSourceEditorState {
    core::CommonValueType type = core::CommonValueType::Number;
    QWidget* widget = nullptr;
    QComboBox* source = nullptr;
    QStackedWidget* stack = nullptr;
    QSpinBox* number = nullptr;
    QComboBox* boolean = nullptr;
    QLineEdit* text = nullptr;
    QPlainTextEdit* expression = nullptr;
    QComboBox* variable = nullptr;
    QComboBox* switchBox = nullptr;
    QComboBox* stringBox = nullptr;
    QComboBox* commonValue = nullptr;
    QComboBox* gameValue = nullptr;
    QComboBox* gameValueEvent = nullptr;
    QSpinBox* gameValuePicture = nullptr;
    QSpinBox* gameValueX = nullptr;
    QSpinBox* gameValueY = nullptr;
    QComboBox* gameValueAction = nullptr;
    QSpinBox* randomMin = nullptr;
    QSpinBox* randomMax = nullptr;
    QComboBox* stringMetricString = nullptr;
    QComboBox* stringMetricOp = nullptr;
    QLineEdit* stringMetricNeedle = nullptr;
    QComboBox* numberTextVariable = nullptr;
    QComboBox* switchTextSwitch = nullptr;
    QComboBox* database = nullptr;
    QComboBox* databaseRecord = nullptr;
    QComboBox* databaseField = nullptr;

    QVariantMap value() const {
        QVariantMap out;
        const QString kind = source ? source->currentData().toString() : QStringLiteral("constant");
        out[QStringLiteral("source")] = kind;
        if (kind == QLatin1String("variable") && variable)
            out[QStringLiteral("variableId")] = variable->currentData();
        else if (kind == QLatin1String("switch") && switchBox)
            out[QStringLiteral("switchId")] = switchBox->currentData();
        else if (kind == QLatin1String("string") && stringBox)
            out[QStringLiteral("stringId")] = stringBox->currentData();
        else if (kind == QLatin1String("commonValue") && commonValue)
            out[QStringLiteral("commonValueId")] = commonValue->currentData();
        else if (kind == QLatin1String("random") && randomMin && randomMax) {
            out[QStringLiteral("minimum")] = randomMin->value();
            out[QStringLiteral("maximum")] = randomMax->value();
        } else if (kind == QLatin1String("stringMetric") && stringMetricString && stringMetricOp) {
            out[QStringLiteral("stringId")] = stringMetricString->currentData();
            out[QStringLiteral("operation")] = stringMetricOp->currentData();
            if (stringMetricNeedle) out[QStringLiteral("needle")] = stringMetricNeedle->text();
        } else if (kind == QLatin1String("numberText") && numberTextVariable) {
            out[QStringLiteral("variableId")] = numberTextVariable->currentData();
        } else if (kind == QLatin1String("switchText") && switchTextSwitch) {
            out[QStringLiteral("switchId")] = switchTextSwitch->currentData();
        } else if (kind == QLatin1String("databaseField") && database && databaseRecord && databaseField) {
            out[QStringLiteral("databaseId")] = database->currentData();
            out[QStringLiteral("recordId")] = databaseRecord->currentData();
            out[QStringLiteral("fieldId")] = databaseField->currentData();
        } else if (kind == QLatin1String("gameValue") && gameValue) {
            const QString key = gameValue->currentData().toString();
            QVariantMap query{{QStringLiteral("key"), key}};
            if (const core::GameValueDescriptor* descriptor = core::gameValueDescriptor(key)) {
                if (descriptor->needsEvent && gameValueEvent) query[QStringLiteral("eventId")] = gameValueEvent->currentData();
                if (descriptor->needsPicture && gameValuePicture) query[QStringLiteral("number")] = gameValuePicture->value();
                if (descriptor->needsMapPosition) {
                    if (gameValueX) query[QStringLiteral("x")] = gameValueX->value();
                    if (gameValueY) query[QStringLiteral("y")] = gameValueY->value();
                }
                if (descriptor->needsAction && gameValueAction) query[QStringLiteral("action")] = gameValueAction->currentData();
            }
            out[QStringLiteral("query")] = query;
        } else if (kind == QLatin1String("expression") && expression) {
            out[QStringLiteral("expression")] = expression->toPlainText().trimmed();
        } else if (type == core::CommonValueType::Boolean && boolean)
            out[QStringLiteral("value")] = boolean->currentData();
        else if (type == core::CommonValueType::Text && text)
            out[QStringLiteral("value")] = text->text();
        else if (number)
            out[QStringLiteral("value")] = number->value();
        return out;
    }
};

std::shared_ptr<CommonSourceEditorState> makeCommonSourceEditor(
    QWidget* parent, core::Editor& ed, core::CommonValueType type,
    const QVariantMap& current, const core::CommonEvent* context,
    bool allowSignatureDefault = false, const QVariant& signatureDefault = QVariant())
{
    auto state = std::make_shared<CommonSourceEditorState>();
    state->type = type;
    state->widget = new QWidget(parent);
    auto* row = new QHBoxLayout(state->widget); row->setContentsMargins(0,0,0,0); row->setSpacing(4);
    state->source = new QComboBox(state->widget);
    state->stack = new QStackedWidget(state->widget);
    row->addWidget(state->source); row->addWidget(state->stack,1);

    auto addPage=[&](const QString& label,const QString& kind,QWidget* widget){
        state->source->addItem(label,kind); state->stack->addWidget(widget);
    };
    if(allowSignatureDefault){
        auto* defaultHint=new QLabel(QObject::tr("Usa o valor padrão definido na Assinatura."),state->widget);
        defaultHint->setWordWrap(true);
        QString valueLabel;
        if(type==core::CommonValueType::Boolean) valueLabel=signatureDefault.toBool()?QObject::tr("Ligado"):QObject::tr("Desligado");
        else valueLabel=signatureDefault.toString();
        addPage(valueLabel.isEmpty()?QObject::tr("Usar padrão"):QObject::tr("Usar padrão (%1)").arg(valueLabel),QStringLiteral("default"),defaultHint);
    }
    if(type==core::CommonValueType::Number){
        state->number=new QSpinBox(state->widget); state->number->setRange(-999999999,999999999); state->number->setValue(current.value(QStringLiteral("value"),0).toInt());
        addPage(QObject::tr("Número"),QStringLiteral("constant"),state->number);
        state->variable=comboVariaveis(state->widget,ed,current.value(QStringLiteral("variableId"),1).toInt());
        addPage(QObject::tr("Variável global"),QStringLiteral("variable"),state->variable);
        auto* randomHost = new QWidget(state->widget);
        auto* randomRow = new QHBoxLayout(randomHost); randomRow->setContentsMargins(0,0,0,0); randomRow->setSpacing(4);
        state->randomMin = new QSpinBox(randomHost); state->randomMax = new QSpinBox(randomHost);
        state->randomMin->setRange(-999999999,999999999); state->randomMax->setRange(-999999999,999999999);
        state->randomMin->setValue(current.value(QStringLiteral("minimum"),0).toInt());
        state->randomMax->setValue(current.value(QStringLiteral("maximum"),10).toInt());
        randomRow->addWidget(state->randomMin); randomRow->addWidget(new QLabel(QObject::tr("até"),randomHost)); randomRow->addWidget(state->randomMax);
        addPage(QObject::tr("Sorteio"),QStringLiteral("random"),randomHost);

        auto* metricHost = new QWidget(state->widget);
        auto* metricForm = new QFormLayout(metricHost); metricForm->setContentsMargins(0,0,0,0); metricForm->setSpacing(3);
        state->stringMetricString = comboStrings(metricHost, ed, current.value(QStringLiteral("stringId"),1).toInt());
        state->stringMetricOp = new QComboBox(metricHost);
        state->stringMetricOp->addItem(QObject::tr("Converter para Número"),QStringLiteral("toNumber"));
        state->stringMetricOp->addItem(QObject::tr("Tamanho do texto"),QStringLiteral("length"));
        state->stringMetricOp->addItem(QObject::tr("Quantidade de linhas"),QStringLiteral("lineCount"));
        state->stringMetricOp->addItem(QObject::tr("Posição de um texto"),QStringLiteral("indexOf"));
        state->stringMetricOp->addItem(QObject::tr("Contar ocorrências"),QStringLiteral("count"));
        int metricOpIndex=state->stringMetricOp->findData(current.value(QStringLiteral("operation"),QStringLiteral("toNumber")));
        if(metricOpIndex>=0)state->stringMetricOp->setCurrentIndex(metricOpIndex);
        state->stringMetricNeedle = new QLineEdit(current.value(QStringLiteral("needle")).toString(),metricHost);
        metricForm->addRow(QObject::tr("String"),state->stringMetricString);
        metricForm->addRow(QObject::tr("Operação"),state->stringMetricOp);
        metricForm->addRow(QObject::tr("Procurar"),state->stringMetricNeedle);
        auto syncMetric=[state]{const QString op=state->stringMetricOp->currentData().toString();state->stringMetricNeedle->setVisible(op==QLatin1String("indexOf")||op==QLatin1String("count"));};
        syncMetric(); QObject::connect(state->stringMetricOp,&QComboBox::currentIndexChanged,metricHost,[syncMetric](int){syncMetric();});
        addPage(QObject::tr("String → Número"),QStringLiteral("stringMetric"),metricHost);
    }else if(type==core::CommonValueType::Boolean){
        state->boolean=new QComboBox(state->widget); state->boolean->addItem(QObject::tr("Ligado"),true); state->boolean->addItem(QObject::tr("Desligado"),false); state->boolean->setCurrentIndex(current.value(QStringLiteral("value"),false).toBool()?0:1);
        addPage(QObject::tr("Ligado/Desligado"),QStringLiteral("constant"),state->boolean);
        state->switchBox=comboInterruptores(state->widget,ed,current.value(QStringLiteral("switchId"),1).toInt());
        addPage(QObject::tr("Interruptor global"),QStringLiteral("switch"),state->switchBox);
    }else{
        state->text=new QLineEdit(current.value(QStringLiteral("value")).toString(),state->widget);
        addPage(QObject::tr("Texto"),QStringLiteral("constant"),state->text);
        state->stringBox=comboStrings(state->widget,ed,current.value(QStringLiteral("stringId"),1).toInt());
        addPage(QObject::tr("Texto global"),QStringLiteral("string"),state->stringBox);
        state->numberTextVariable=comboVariaveis(state->widget,ed,current.value(QStringLiteral("variableId"),1).toInt());
        addPage(QObject::tr("Variável → Texto"),QStringLiteral("numberText"),state->numberTextVariable);
        state->switchTextSwitch=comboInterruptores(state->widget,ed,current.value(QStringLiteral("switchId"),1).toInt());
        addPage(QObject::tr("Switch → Texto"),QStringLiteral("switchText"),state->switchTextSwitch);
    }
    {
        auto* expressionHost=new QWidget(state->widget);
        auto* expressionLayout=new QVBoxLayout(expressionHost);expressionLayout->setContentsMargins(0,0,0,0);expressionLayout->setSpacing(3);
        state->expression=new QPlainTextEdit(expressionHost);state->expression->setMaximumHeight(76);
        state->expression->setPlaceholderText(QObject::tr("Ex.: clamp(v[1] * 2, 0, 999) ou party.gold() > 100 ? 1 : 0"));
        state->expression->setPlainText(current.value(QStringLiteral("expression")).toString());
        auto* expressionHelp=new QLabel(QObject::tr("Funções: abs, min, max, floor, ceil, round, clamp, lerp, sqrt, pow, sin, cos e gv(\"chave\"). Também aceita v[n], s[n], switch(n), valores do grupo, mapa, input e Pictures."),expressionHost);
        expressionHelp->setWordWrap(true);expressionHelp->setStyleSheet(QStringLiteral("color:#777;font-size:11px"));
        expressionLayout->addWidget(state->expression);expressionLayout->addWidget(expressionHelp);
        addPage(QObject::tr("Expressão"),QStringLiteral("expression"),expressionHost);
    }
    {
        auto* gameHost = new QWidget(state->widget);
        auto* gameForm = new QFormLayout(gameHost); gameForm->setContentsMargins(0,0,0,0); gameForm->setSpacing(3);
        state->gameValue = new QComboBox(gameHost);
        for (const core::GameValueDescriptor& descriptor : core::gameValueDescriptors())
            if (descriptor.type == type && descriptor.key != QLatin1String("actor.hp") &&
                descriptor.key != QLatin1String("actor.mp") && descriptor.key != QLatin1String("party.hasItem") &&
                descriptor.key != QLatin1String("timer.value")) state->gameValue->addItem(descriptor.label, descriptor.key);
        state->gameValueEvent = new QComboBox(gameHost);
        state->gameValueEvent->addItem(QObject::tr("Este evento"), QStringLiteral("self"));
        for (const core::MapEvent& ev : ed.events()) state->gameValueEvent->addItem(ev.name.isEmpty()?ev.id:ev.name, ev.id);
        state->gameValuePicture = new QSpinBox(gameHost); state->gameValuePicture->setRange(1,9999);
        state->gameValueX = new QSpinBox(gameHost); state->gameValueY = new QSpinBox(gameHost);
        state->gameValueAction = new QComboBox(gameHost);
        for(core::GameAction action:core::allGameActions()) state->gameValueAction->addItem(core::gameActionLabel(action),core::gameActionId(action));
        state->gameValueX->setRange(-9999,9999); state->gameValueY->setRange(-9999,9999);
        const QVariantMap oldQuery = current.value(QStringLiteral("query")).toMap();
        int gv = state->gameValue->findData(oldQuery.value(QStringLiteral("key")).toString()); if (gv >= 0) state->gameValue->setCurrentIndex(gv);
        int ge = state->gameValueEvent->findData(oldQuery.value(QStringLiteral("eventId"),QStringLiteral("self"))); if (ge >= 0) state->gameValueEvent->setCurrentIndex(ge);
        state->gameValuePicture->setValue(oldQuery.value(QStringLiteral("number"),1).toInt());
        state->gameValueX->setValue(oldQuery.value(QStringLiteral("x")).toInt()); state->gameValueY->setValue(oldQuery.value(QStringLiteral("y")).toInt());
        int ga=state->gameValueAction->findData(oldQuery.value(QStringLiteral("action"),QStringLiteral("confirm")));if(ga>=0)state->gameValueAction->setCurrentIndex(ga);
        gameForm->addRow(QObject::tr("Valor"), state->gameValue);
        gameForm->addRow(QObject::tr("Evento"), state->gameValueEvent);
        gameForm->addRow(QObject::tr("Imagem nº"), state->gameValuePicture);
        gameForm->addRow(QObject::tr("X"), state->gameValueX); gameForm->addRow(QObject::tr("Y"), state->gameValueY);
        gameForm->addRow(QObject::tr("Ação"), state->gameValueAction);
        auto syncGame = [state]{
            const core::GameValueDescriptor* descriptor = core::gameValueDescriptor(state->gameValue->currentData().toString());
            state->gameValueEvent->setVisible(descriptor && descriptor->needsEvent);
            state->gameValuePicture->setVisible(descriptor && descriptor->needsPicture);
            state->gameValueX->setVisible(descriptor && descriptor->needsMapPosition);
            state->gameValueY->setVisible(descriptor && descriptor->needsMapPosition);
            state->gameValueAction->setVisible(descriptor && descriptor->needsAction);
        };
        syncGame(); QObject::connect(state->gameValue,&QComboBox::currentIndexChanged,gameHost,[syncGame](int){syncGame();});
        if (state->gameValue->count() > 0) addPage(QObject::tr("Valor do Jogo"),QStringLiteral("gameValue"),gameHost);
        else gameHost->deleteLater();
    }
    {
        auto* databaseHost = new QWidget(state->widget);
        auto* databaseForm = new QFormLayout(databaseHost); databaseForm->setContentsMargins(0,0,0,0); databaseForm->setSpacing(3);
        state->database = new QComboBox(databaseHost);
        state->databaseRecord = new QComboBox(databaseHost);
        state->databaseField = new QComboBox(databaseHost);
        for (const core::CustomDatabaseDefinition& database : ed.customDatabases) {
            bool hasCompatibleField = false;
            for (const core::CustomDatabaseField& field : database.fields) {
                const core::CommonValueType fieldType = field.type == core::CustomDatabaseFieldType::Number
                    ? core::CommonValueType::Number : field.type == core::CustomDatabaseFieldType::Boolean
                        ? core::CommonValueType::Boolean : core::CommonValueType::Text;
                if (fieldType == type) { hasCompatibleField = true; break; }
            }
            if (hasCompatibleField) state->database->addItem(database.name.isEmpty()?database.id:database.name, database.id);
        }
        const QVariantMap oldDatabaseSpec = current;
        auto rebuildDatabaseFields = [state,&ed,type,oldDatabaseSpec] {
            const QString databaseId = state->database->currentData().toString();
            const core::CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
            const QString keepRecord = state->databaseRecord->currentData().toString().isEmpty()
                ? oldDatabaseSpec.value(QStringLiteral("recordId")).toString() : state->databaseRecord->currentData().toString();
            const QString keepField = state->databaseField->currentData().toString().isEmpty()
                ? oldDatabaseSpec.value(QStringLiteral("fieldId")).toString() : state->databaseField->currentData().toString();
            QSignalBlocker recordBlocker(state->databaseRecord), fieldBlocker(state->databaseField);
            state->databaseRecord->clear(); state->databaseField->clear();
            if (!database) return;
            for (const core::CustomDatabaseRecord& record : database->records)
                state->databaseRecord->addItem(record.name.isEmpty()?QObject::tr("Registro %1").arg(record.number):record.name, record.id);
            for (const core::CustomDatabaseField& field : database->fields) {
                const core::CommonValueType fieldType = field.type == core::CustomDatabaseFieldType::Number
                    ? core::CommonValueType::Number : field.type == core::CustomDatabaseFieldType::Boolean
                        ? core::CommonValueType::Boolean : core::CommonValueType::Text;
                if (fieldType == type) state->databaseField->addItem(field.name.isEmpty()?field.id:field.name, field.id);
            }
            int ri=state->databaseRecord->findData(keepRecord); if(ri>=0) state->databaseRecord->setCurrentIndex(ri);
            int fi=state->databaseField->findData(keepField); if(fi>=0) state->databaseField->setCurrentIndex(fi);
        };
        int databaseIndex=state->database->findData(current.value(QStringLiteral("databaseId")).toString());
        if(databaseIndex>=0)state->database->setCurrentIndex(databaseIndex);
        rebuildDatabaseFields();
        QObject::connect(state->database,&QComboBox::currentIndexChanged,databaseHost,[rebuildDatabaseFields](int){rebuildDatabaseFields();});
        databaseForm->addRow(QObject::tr("Banco"),state->database);
        databaseForm->addRow(QObject::tr("Registro"),state->databaseRecord);
        databaseForm->addRow(QObject::tr("Campo"),state->databaseField);
        if(state->database->count()>0) addPage(QObject::tr("Campo do Banco de Dados"),QStringLiteral("databaseField"),databaseHost);
        else databaseHost->deleteLater();
    }
    if(context){
        state->commonValue=new QComboBox(state->widget);
        for(const core::CommonEventParameter& def:context->parameters) if(def.type==type) state->commonValue->addItem(QObject::tr("Parâmetro: %1").arg(def.name),def.id);
        for(const core::CommonEventLocal& def:context->locals) if(def.type==type) state->commonValue->addItem(QObject::tr("Local: %1").arg(def.name),def.id);
        if(state->commonValue->count()>0){
            const int idx=state->commonValue->findData(current.value(QStringLiteral("commonValueId")).toString()); if(idx>=0) state->commonValue->setCurrentIndex(idx);
            addPage(QObject::tr("Parâmetro/local"),QStringLiteral("commonValue"),state->commonValue);
        }
    }
    const QString requestedSource = current.isEmpty() && allowSignatureDefault
        ? QStringLiteral("default")
        : current.value(QStringLiteral("source"),QStringLiteral("constant")).toString();
    int sourceIndex=state->source->findData(requestedSource); if(sourceIndex<0) sourceIndex=0;
    state->source->setCurrentIndex(sourceIndex); state->stack->setCurrentIndex(sourceIndex);
    QObject::connect(state->source,&QComboBox::currentIndexChanged,state->widget,[state](int index){state->stack->setCurrentIndex(index);});
    return state;
}


struct ValueTargetEditorState {
    QWidget* widget=nullptr;
    QComboBox* kind=nullptr;
    QComboBox* value=nullptr;
    core::CommonValueType type=core::CommonValueType::Number;
    const core::CommonEvent* context=nullptr;
    const core::Editor* editor=nullptr;
    QVariantMap saved;

    QVariantMap result() const {
        QVariantMap out{{QStringLiteral("target"),kind?kind->currentData():QVariant(QStringLiteral("none"))}};
        const QString k=kind?kind->currentData().toString():QStringLiteral("none");
        if(k==QLatin1String("commonValue"))out[QStringLiteral("commonValueId")]=value?value->currentData():QVariant();
        else if(k!=QLatin1String("none"))out[QStringLiteral("id")]=value?value->currentData():QVariant();
        return out;
    }
};

std::shared_ptr<ValueTargetEditorState> makeValueTargetEditor(QWidget* parent,const core::Editor& ed,
    core::CommonValueType type,const QVariantMap& saved,const core::CommonEvent* context,bool allowNone=false)
{
    auto state=std::make_shared<ValueTargetEditorState>();state->type=type;state->context=context;state->editor=&ed;state->saved=saved;
    state->widget=new QWidget(parent);auto* row=new QHBoxLayout(state->widget);row->setContentsMargins(0,0,0,0);row->setSpacing(4);
    state->kind=new QComboBox(state->widget);state->value=new QComboBox(state->widget);
    if(allowNone)state->kind->addItem(QObject::tr("Ignorar"),QStringLiteral("none"));
    if(type==core::CommonValueType::Number)state->kind->addItem(QObject::tr("Variável global"),QStringLiteral("variable"));
    else if(type==core::CommonValueType::Boolean)state->kind->addItem(QObject::tr("Interruptor global"),QStringLiteral("switch"));
    else state->kind->addItem(QObject::tr("Texto global"),QStringLiteral("string"));
    if(context){bool any=false;for(const auto& local:context->locals)if(local.type==type){any=true;break;}if(any)state->kind->addItem(QObject::tr("Local do Evento Comum"),QStringLiteral("commonValue"));}
    int kindIndex=state->kind->findData(saved.value(QStringLiteral("target")).toString());if(kindIndex<0)kindIndex=0;state->kind->setCurrentIndex(kindIndex);
    auto rebuild=[state]{
        const QString k=state->kind->currentData().toString();QSignalBlocker blocker(state->value);state->value->clear();
        if(k==QLatin1String("variable"))for(const auto& d:state->editor->variables)state->value->addItem(QStringLiteral("%1 — %2").arg(d.id).arg(d.name),d.id);
        else if(k==QLatin1String("switch"))for(const auto& d:state->editor->switches)state->value->addItem(QStringLiteral("%1 — %2").arg(d.id).arg(d.name),d.id);
        else if(k==QLatin1String("string"))for(const auto& d:state->editor->strings)state->value->addItem(QStringLiteral("%1 — %2").arg(d.id).arg(d.name),d.id);
        else if(k==QLatin1String("commonValue")&&state->context)for(const auto& d:state->context->locals)if(d.type==state->type)state->value->addItem(d.name,d.id);
        state->value->setEnabled(k!=QLatin1String("none"));
        const QVariant old=k==QLatin1String("commonValue")?state->saved.value(QStringLiteral("commonValueId")):state->saved.value(QStringLiteral("id"));
        const int i=state->value->findData(old);if(i>=0)state->value->setCurrentIndex(i);
    };
    rebuild();QObject::connect(state->kind,&QComboBox::currentIndexChanged,state->widget,[rebuild](int){rebuild();});
    row->addWidget(state->kind);row->addWidget(state->value,1);return state;
}

struct DatabaseRecordSelectorState {
    QWidget* widget=nullptr; QComboBox* mode=nullptr; QStackedWidget* stack=nullptr; QComboBox* fixed=nullptr;
    std::shared_ptr<CommonSourceEditorState> dynamic;
    const core::Editor* editor=nullptr; QString databaseId; QString savedRecordId;
    QVariantMap savedSpec;
    void refresh(const QString& id){databaseId=id;QSignalBlocker blocker(fixed);const QString keep=fixed->currentData().toString().isEmpty()?savedRecordId:fixed->currentData().toString();fixed->clear();if(const auto* db=editor->customDatabase(id))for(const auto& r:db->records)fixed->addItem(r.name.isEmpty()?QObject::tr("Registro %1").arg(r.number):r.name,r.id);int i=fixed->findData(keep);if(i>=0)fixed->setCurrentIndex(i);}
    QVariantMap recordSpec() const {return mode&&mode->currentData().toString()==QLatin1String("dynamic")&&dynamic?dynamic->value():QVariantMap();}
    QString recordId() const {return fixed?fixed->currentData().toString():QString();}
};

std::shared_ptr<DatabaseRecordSelectorState> makeDatabaseRecordSelector(QWidget* parent,core::Editor& ed,
    const QString& databaseId,const QVariantMap& savedSpec,const QString& savedRecordId,const core::CommonEvent* context)
{
    auto state=std::make_shared<DatabaseRecordSelectorState>();state->editor=&ed;state->savedSpec=savedSpec;state->savedRecordId=savedRecordId;
    state->widget=new QWidget(parent);auto* layout=new QVBoxLayout(state->widget);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);
    state->mode=new QComboBox(state->widget);state->mode->addItem(QObject::tr("Escolher registro"),QStringLiteral("fixed"));state->mode->addItem(QObject::tr("Registro vindo de um valor"),QStringLiteral("dynamic"));
    state->stack=new QStackedWidget(state->widget);state->fixed=new QComboBox(state->stack);state->stack->addWidget(state->fixed);
    QVariantMap dynamicSpec=savedSpec;if(dynamicSpec.isEmpty()){
        if(ed.strings.isEmpty())dynamicSpec={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),QString()}};
        else dynamicSpec={{QStringLiteral("source"),QStringLiteral("string")},{QStringLiteral("stringId"),ed.strings.first().id}};
    }
    state->dynamic=makeCommonSourceEditor(state->stack,ed,core::CommonValueType::Text,dynamicSpec,context);state->stack->addWidget(state->dynamic->widget);
    if(!savedSpec.isEmpty())state->mode->setCurrentIndex(1);state->stack->setCurrentIndex(state->mode->currentIndex());state->refresh(databaseId);
    QObject::connect(state->mode,&QComboBox::currentIndexChanged,state->widget,[state](int i){state->stack->setCurrentIndex(i);});
    layout->addWidget(state->mode);layout->addWidget(state->stack);return state;
}

QComboBox* customDatabaseCombo(QWidget* parent,const core::Editor& ed,const QString& current,bool runtimeOnly=false)
{
    auto* combo=new QComboBox(parent);for(const auto& db:ed.customDatabases){if(runtimeOnly&&db.mode!=core::CustomDatabaseMode::Runtime)continue;combo->addItem(QStringLiteral("%1 — %2").arg(db.number).arg(db.name.isEmpty()?db.id:db.name),db.id);}int i=combo->findData(current);if(i>=0)combo->setCurrentIndex(i);return combo;
}

QComboBox* runtimeMapCombo(QWidget* parent,const core::Editor& ed,const QString& current)
{
    auto* combo=new QComboBox(parent);combo->addItem(QObject::tr("Mapa atual (em execução)"),QString());
    for(const core::MapDoc& map:ed.docs)combo->addItem(map.name.isEmpty()?map.id:map.name,map.id);
    const int i=combo->findData(current);if(i>=0)combo->setCurrentIndex(i);return combo;
}

const core::MapDoc* runtimeSelectedMap(const core::Editor& ed,const QString& mapId)
{
    return mapId.isEmpty()?ed.doc():ed.mapById(mapId);
}

void fillRuntimeTileLayerCombo(QComboBox* combo,const core::Editor& ed,const QString& mapId,const QString& preferred=QString())
{
    if(!combo)return;const QString keep=!combo->currentData().toString().isEmpty()?combo->currentData().toString():preferred;QSignalBlocker blocker(combo);combo->clear();
    const core::MapDoc* map=runtimeSelectedMap(ed,mapId);if(!map)return;
    std::function<void(const QVector<core::LayerPtr>&,QString)> append=[&](const QVector<core::LayerPtr>& nodes,const QString& prefix){for(const core::LayerPtr& layer:nodes){if(!layer)continue;const QString label=prefix+(layer->name.isEmpty()?layer->id:layer->name);if(layer->type==core::LayerType::Tile)combo->addItem(label,layer->id);if(!layer->children.isEmpty())append(layer->children,prefix+QStringLiteral("› "));}};append(map->layers,QString());
    int i=combo->findData(keep);if(i<0&&combo->count()>0)i=0;if(i>=0)combo->setCurrentIndex(i);
}

QComboBox* runtimeTileLayerCombo(QWidget* parent,const core::Editor& ed,const QString& mapId,const QString& current)
{
    auto* combo=new QComboBox(parent);fillRuntimeTileLayerCombo(combo,ed,mapId,current);return combo;
}

QComboBox* runtimeTilesetCombo(QWidget* parent,const core::Editor& ed,const QString& current,bool allowReset=false)
{
    auto* combo=new QComboBox(parent);if(allowReset)combo->addItem(QObject::tr("(Resetar / usar original)"),QString());for(const core::Tileset& ts:ed.tilesets)combo->addItem(ts.name.isEmpty()?ts.id:ts.name,ts.id);const int i=combo->findData(current);if(i>=0)combo->setCurrentIndex(i);return combo;
}

QVariantMap numberSpecFromParam(const QVariantMap& params,const QString& key,int fallback=0)
{
    const QVariant raw=params.value(key);QVariantMap spec=raw.toMap();if(spec.isEmpty())spec={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),raw.isValid()?raw.toInt():fallback}};return spec;
}

core::CommonValueType customFieldUiType(const core::CustomDatabaseField* field)
{
    if(!field)return core::CommonValueType::Text;
    if(field->type==core::CustomDatabaseFieldType::Number)return core::CommonValueType::Number;
    if(field->type==core::CustomDatabaseFieldType::Boolean)return core::CommonValueType::Boolean;
    return core::CommonValueType::Text;
}

class ConditionTreeEditorWidget final : public QWidget
{
public:
    ConditionTreeEditorWidget(core::Editor& editor, const QVariantMap& sourceParams,
                              const core::CommonEvent* commonContext, QWidget* parent=nullptr)
        : QWidget(parent), ed(editor), context(commonContext)
    {
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* hint=new QLabel(QObject::tr("Combine condições em grupos. ‘Todas (AND)’ exige todas; ‘Qualquer (OR)’ aceita uma. Grupos podem ficar dentro de outros grupos."),this);
        hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("color:#777;font-size:11px"));layout->addWidget(hint);
        tree=new QTreeWidget(this);tree->setHeaderHidden(true);tree->setSelectionMode(QAbstractItemView::SingleSelection);layout->addWidget(tree,1);
        auto* row=new QHBoxLayout;addConditionButton=new QPushButton(QObject::tr("+ Condição"),this);addAndButton=new QPushButton(QObject::tr("+ Grupo AND"),this);addOrButton=new QPushButton(QObject::tr("+ Grupo OR"),this);editButton=new QPushButton(QObject::tr("Editar"),this);removeButton=new QPushButton(QObject::tr("Remover"),this);
        row->addWidget(addConditionButton);row->addWidget(addAndButton);row->addWidget(addOrButton);row->addStretch(1);row->addWidget(editButton);row->addWidget(removeButton);layout->addLayout(row);
        auto* modeRow=new QHBoxLayout;modeRow->addWidget(new QLabel(QObject::tr("Grupo selecionado:"),this));groupMode=new QComboBox(this);groupMode->addItem(QObject::tr("Todas (AND)"),QStringLiteral("all"));groupMode->addItem(QObject::tr("Qualquer (OR)"),QStringLiteral("any"));modeRow->addWidget(groupMode);modeRow->addStretch(1);layout->addLayout(modeRow);

        QVariantMap initial=core::conditionTreeFromCommandParams(sourceParams);
        if(!core::conditionTreeStructureProblem(initial).isEmpty()) initial=core::conditionTreeFromLegacyParams(sourceParams);
        root=appendNode(nullptr,initial);tree->expandAll();tree->setCurrentItem(root);
        syncSelection();

        QObject::connect(tree,&QTreeWidget::itemSelectionChanged,this,[this]{syncSelection();});
        QObject::connect(groupMode,&QComboBox::currentIndexChanged,this,[this](int){auto* item=tree->currentItem();if(!item||item->data(0,NodeTypeRole).toString()!=QLatin1String("group"))return;item->setData(0,NodeDataRole,groupMode->currentData());refreshItem(item);});
        QObject::connect(addConditionButton,&QPushButton::clicked,this,[this]{addCondition();});
        QObject::connect(addAndButton,&QPushButton::clicked,this,[this]{addGroup(QStringLiteral("all"));});
        QObject::connect(addOrButton,&QPushButton::clicked,this,[this]{addGroup(QStringLiteral("any"));});
        QObject::connect(editButton,&QPushButton::clicked,this,[this]{editCurrent();});
        QObject::connect(removeButton,&QPushButton::clicked,this,[this]{removeCurrent();});
        QObject::connect(tree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem*,int){editCurrent();});
    }

    QVariantMap value() const { return serialize(root); }

private:
    enum { NodeTypeRole=Qt::UserRole+20, NodeDataRole=Qt::UserRole+21 };
    core::Editor& ed; const core::CommonEvent* context=nullptr; QTreeWidget* tree=nullptr; QTreeWidgetItem* root=nullptr;
    QComboBox* groupMode=nullptr; QPushButton *addConditionButton=nullptr,*addAndButton=nullptr,*addOrButton=nullptr,*editButton=nullptr,*removeButton=nullptr;

    static QString predicateLabel(const QVariantMap& p)
    {
        const QString k=p.value(QStringLiteral("kind"),QStringLiteral("switch")).toString();
        if(k==QLatin1String("switch"))return QObject::tr("Condição · Switch %1").arg(p.value(QStringLiteral("id"),1).toInt());
        if(k==QLatin1String("selfSwitch"))return QObject::tr("Condição · Self Switch %1").arg(p.value(QStringLiteral("letter"),QStringLiteral("A")).toString());
        if(k==QLatin1String("variable"))return QObject::tr("Condição · Variável %1 %2 …").arg(p.value(QStringLiteral("id"),1).toInt()).arg(p.value(QStringLiteral("op"),QStringLiteral(">=")).toString());
        if(k==QLatin1String("string"))return QObject::tr("Condição · String %1").arg(p.value(QStringLiteral("id"),1).toInt());
        if(k==QLatin1String("commonValue"))return QObject::tr("Condição · Parâmetro/local %1").arg(p.value(QStringLiteral("id")).toString());
        if(k==QLatin1String("random"))return QObject::tr("Condição · Probabilidade %1%").arg(p.value(QStringLiteral("chance"),50).toInt());
        if(k==QLatin1String("playerPosition"))return QObject::tr("Condição · Posição do jogador");
        if(k==QLatin1String("direction"))return QObject::tr("Condição · Direção do jogador");
        if(k==QLatin1String("distance"))return QObject::tr("Condição · Distância ao evento");
        if(k==QLatin1String("routeRunning"))return QObject::tr("Condição · Rota em execução");
        if(k==QLatin1String("map"))return QObject::tr("Condição · Mapa atual");
        if(k==QLatin1String("button"))return QObject::tr("Condição · Input");
        if(k==QLatin1String("gold"))return QObject::tr("Condição · Ouro");
        if(k==QLatin1String("item"))return QObject::tr("Condição · Item no inventário");
        return QObject::tr("Condição · %1").arg(k);
    }

    QTreeWidgetItem* appendNode(QTreeWidgetItem* parent,const QVariantMap& node)
    {
        auto* item=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(tree);
        const QString type=node.value(QStringLiteral("node")).toString();item->setData(0,NodeTypeRole,type);
        if(type==QLatin1String("group")){
            item->setData(0,NodeDataRole,core::normalizedConditionGroupMode(node.value(QStringLiteral("mode")).toString()));
            for(const QVariant& child:node.value(QStringLiteral("children")).toList())appendNode(item,child.toMap());
        }else item->setData(0,NodeDataRole,node.value(QStringLiteral("condition")).toMap());
        refreshItem(item);return item;
    }
    void refreshItem(QTreeWidgetItem* item)
    {
        if(!item)return;const QString type=item->data(0,NodeTypeRole).toString();
        if(type==QLatin1String("group")){const QString mode=item->data(0,NodeDataRole).toString();item->setText(0,mode==QLatin1String("any")?QObject::tr("QUALQUER condição (OR)"):QObject::tr("TODAS as condições (AND)"));item->setExpanded(true);}
        else item->setText(0,predicateLabel(item->data(0,NodeDataRole).toMap()));
    }
    QVariantMap serialize(QTreeWidgetItem* item) const
    {
        if(!item)return {};
        if(item->data(0,NodeTypeRole).toString()==QLatin1String("condition"))return core::conditionLeafNode(item->data(0,NodeDataRole).toMap());
        QVariantList children;for(int i=0;i<item->childCount();++i)children.push_back(serialize(item->child(i)));
        return core::conditionGroupNode(item->data(0,NodeDataRole).toString(),children);
    }
    QTreeWidgetItem* selectedGroup() const
    {
        QTreeWidgetItem* item=tree->currentItem();if(!item)return root;
        if(item->data(0,NodeTypeRole).toString()==QLatin1String("group"))return item;
        return item->parent()?item->parent():root;
    }
    void syncSelection()
    {
        auto* item=tree->currentItem();const bool group=item&&item->data(0,NodeTypeRole).toString()==QLatin1String("group");
        groupMode->setEnabled(group);QSignalBlocker blocker(groupMode);if(group){int i=groupMode->findData(item->data(0,NodeDataRole));if(i>=0)groupMode->setCurrentIndex(i);}
        editButton->setEnabled(item&&item->data(0,NodeTypeRole).toString()==QLatin1String("condition"));
        removeButton->setEnabled(item&&item!=root&&item->parent()&&item->parent()->childCount()>1);
    }
    void addCondition()
    {
        QTreeWidgetItem* parent=selectedGroup();core::EventCommand leaf{QStringLiteral("if"),{{QStringLiteral("kind"),QStringLiteral("switch")},{QStringLiteral("id"),1},{QStringLiteral("value"),true}}};
        LogicCommandDialog dialog(ed,QStringLiteral("if"),leaf,this,context,false);if(dialog.exec()!=QDialog::Accepted)return;
        leaf.params.remove(QStringLiteral("createElse"));leaf.params.remove(QStringLiteral("timeoutFrames"));leaf.params.remove(QStringLiteral("conditionTree"));
        auto* item=appendNode(parent,core::conditionLeafNode(leaf.params));tree->setCurrentItem(item);syncSelection();
    }
    void addGroup(const QString& mode)
    {
        QTreeWidgetItem* parent=selectedGroup();QVariantList children{core::conditionLeafNode({{QStringLiteral("kind"),QStringLiteral("switch")},{QStringLiteral("id"),1},{QStringLiteral("value"),true}})};
        auto* item=appendNode(parent,core::conditionGroupNode(mode,children));tree->setCurrentItem(item);tree->expandAll();syncSelection();
    }
    void editCurrent()
    {
        auto* item=tree->currentItem();if(!item||item->data(0,NodeTypeRole).toString()!=QLatin1String("condition"))return;
        core::EventCommand leaf{QStringLiteral("if"),item->data(0,NodeDataRole).toMap()};LogicCommandDialog dialog(ed,QStringLiteral("if"),leaf,this,context,false);if(dialog.exec()!=QDialog::Accepted)return;
        leaf.params.remove(QStringLiteral("createElse"));leaf.params.remove(QStringLiteral("timeoutFrames"));leaf.params.remove(QStringLiteral("conditionTree"));item->setData(0,NodeDataRole,leaf.params);refreshItem(item);syncSelection();
    }
    void removeCurrent()
    {
        auto* item=tree->currentItem();if(!item||item==root||!item->parent()||item->parent()->childCount()<=1)return;
        auto* parent=item->parent();delete item;tree->setCurrentItem(parent);syncSelection();
    }
};

} // namespace

LogicCommandDialog::LogicCommandDialog(core::Editor& editorRef, const QString& tipo,
                                       core::EventCommand& cmd, QWidget* parent,
                                       const core::CommonEvent* commonContext,
                                       bool allowConditionTree)
    : QDialog(parent), ed(editorRef), m_cmd(cmd), m_commonContext(commonContext)
{
    m_cmd.type = tipo;
    auto* v = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    v->addLayout(form);
    const QVariantMap& p = cmd.params;

    if (tipo == QLatin1String("dialogue.fastForward")) {
        setWindowTitle(tr("Avanço rápido de diálogos"));
        auto* speed = new QDoubleSpinBox(this); speed->setRange(1.0, 20.0); speed->setDecimals(1);
        speed->setSuffix(tr("×")); speed->setValue(qBound(1.0, p.value(QStringLiteral("speed"), 1.0).toDouble(), 20.0));
        form->addRow(tr("Velocidade:"), speed);
        auto* note = new QLabel(tr("Acelera somente a revelação das letras. Pausas, esperas e confirmações continuam sendo respeitadas."), this); note->setWordWrap(true); v->addWidget(note);
        connect(this, &QDialog::accepted, this, [this, speed] { m_cmd.params = {{QStringLiteral("speed"), speed->value()}}; });
    } else if (tipo == QLatin1String("dialogue.skipMode")) {
        setWindowTitle(tr("Modo Skip de diálogos"));
        auto* enabled = new QCheckBox(tr("Revelar imediatamente as mensagens seguintes"), this);
        enabled->setChecked(p.value(QStringLiteral("enabled"), true).toBool()); form->addRow(enabled);
        auto* note = new QLabel(tr("O texto aparece instantaneamente, mas \\. \\| e \\! continuam funcionando."), this); note->setWordWrap(true); v->addWidget(note);
        connect(this, &QDialog::accepted, this, [this, enabled] { m_cmd.params = {{QStringLiteral("enabled"), enabled->isChecked()}}; });
    } else if (tipo == QLatin1String("switch.set")) {
        setWindowTitle(tr("Alterar interruptor"));
        auto* id = comboInterruptores(this, ed, p.value(QStringLiteral("id"), 1).toInt());
        auto* op = new QComboBox(this);
        op->addItem(tr("Definir como"), QStringLiteral("set"));
        op->addItem(tr("Inverter"), QStringLiteral("toggle"));
        const QString legacyOp=p.value(QStringLiteral("value"),QStringLiteral("on")).toString();
        op->setCurrentIndex(legacyOp==QLatin1String("toggle")?1:0);
        QVariantMap oldSpec=p.value(QStringLiteral("sourceSpec")).toMap();
        if(oldSpec.isEmpty()){
            if(p.value(QStringLiteral("source")).toString()==QLatin1String("commonValue"))
                oldSpec={{QStringLiteral("source"),QStringLiteral("commonValue")},{QStringLiteral("commonValueId"),p.value(QStringLiteral("commonValueId"))}};
            else oldSpec={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),legacyOp==QLatin1String("on")}};
        }
        auto source=makeCommonSourceEditor(this,ed,core::CommonValueType::Boolean,oldSpec,m_commonContext);
        form->addRow(tr("Interruptor"),id);form->addRow(tr("O que fazer"),op);form->addRow(tr("Novo estado"),source->widget);
        auto sync=[=]{source->widget->setVisible(op->currentData().toString()!=QLatin1String("toggle"));};sync();
        connect(op,&QComboBox::currentIndexChanged,this,[=](int){sync();});
        connect(this,&QDialog::accepted,this,[=]{
            m_cmd.params={{QStringLiteral("id"),id->currentData()}};
            if(op->currentData().toString()==QLatin1String("toggle"))m_cmd.params[QStringLiteral("value")]=QStringLiteral("toggle");
            else{m_cmd.params[QStringLiteral("value")]=QStringLiteral("set");m_cmd.params[QStringLiteral("sourceSpec")]=source->value();}
        });
    } else if (tipo == QLatin1String("selfSwitch.set")) {
        setWindowTitle(tr("Alterar interruptor deste evento"));
        auto* letra = new QComboBox(this);
        for (const QString& l : { QStringLiteral("A"), QStringLiteral("B"),
                                  QStringLiteral("C"), QStringLiteral("D") })
            letra->addItem(l, l);
        letra->setCurrentIndex(qMax(0, letra->findData(
            p.value(QStringLiteral("letter"), QStringLiteral("A")).toString())));
        auto* op = new QComboBox(this);
        op->addItem(tr("Ligar"), QStringLiteral("on"));
        op->addItem(tr("Desligar"), QStringLiteral("off"));
        op->addItem(tr("Inverter"), QStringLiteral("toggle"));
        op->setCurrentIndex(qMax(0, op->findData(p.value(QStringLiteral("value"),
                                                         QStringLiteral("on")).toString())));
        form->addRow(tr("Interruptor"), letra);
        form->addRow(tr("O que fazer"), op);
        auto* nota = new QLabel(tr("Serve para o evento lembrar de algo <b>só dele</b> — "
                                   "o baú que abre uma vez só, sem gastar um interruptor "
                                   "global."), this);
        nota->setWordWrap(true);
        nota->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
        v->addWidget(nota);
        connect(this, &QDialog::accepted, this, [this, letra, op] {
            m_cmd.params = { { QStringLiteral("letter"), letra->currentData() },
                             { QStringLiteral("value"), op->currentData() } };
        });
    } else if (tipo == QLatin1String("variable.set")) {
        setWindowTitle(tr("Alterar variável"));
        auto* id = comboVariaveis(this, ed, p.value(QStringLiteral("id"), 1).toInt());
        auto* useRange = new QCheckBox(tr("Alterar várias variáveis de uma vez"), this);
        const int rangeEndId = p.value(QStringLiteral("rangeEndId"), p.value(QStringLiteral("id"),1)).toInt();
        useRange->setChecked(rangeEndId != p.value(QStringLiteral("id"),1).toInt());
        auto* endId = comboVariaveis(this, ed, rangeEndId);
        auto* op = new QComboBox(this);
        const QVector<QPair<QString,QString>> operations = {
            {tr("Definir como"),QStringLiteral("=")},{tr("Somar"),QStringLiteral("+")},
            {tr("Subtrair"),QStringLiteral("-")},{tr("Multiplicar"),QStringLiteral("*")},
            {tr("Dividir"),QStringLiteral("/")},{tr("Usar resto da divisão"),QStringLiteral("%")},
            {tr("Manter o menor valor"),QStringLiteral("min")},{tr("Manter o maior valor"),QStringLiteral("max")},
            {tr("Usar valor absoluto"),QStringLiteral("abs")}
        };
        for(const auto& item:operations) op->addItem(item.first,item.second);
        op->setCurrentIndex(qMax(0,op->findData(p.value(QStringLiteral("op"),QStringLiteral("=")).toString())));
        QVariantMap oldSpec=p.value(QStringLiteral("sourceSpec")).toMap();
        if(oldSpec.isEmpty()){
            QString legacy=p.value(QStringLiteral("source"),QStringLiteral("const")).toString();
            oldSpec[QStringLiteral("source")]=legacy==QLatin1String("const")?QStringLiteral("constant"):legacy;
            oldSpec[QStringLiteral("value")]=p.value(QStringLiteral("value"),0);
            oldSpec[QStringLiteral("variableId")]=p.value(QStringLiteral("valueVariable"),1);
            oldSpec[QStringLiteral("commonValueId")]=p.value(QStringLiteral("commonValueId"));
            oldSpec[QStringLiteral("minimum")]=p.value(QStringLiteral("randomMin"),0);
            oldSpec[QStringLiteral("maximum")]=p.value(QStringLiteral("randomMax"),10);
        }
        auto source=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,oldSpec,m_commonContext);
        form->addRow(tr("Variável"),id);
        form->addRow(QString(),useRange); form->addRow(tr("Até a variável"),endId);
        form->addRow(tr("O que fazer"),op); form->addRow(tr("Valor usado"),source->widget);
        auto syncRange=[=]{endId->setVisible(useRange->isChecked());};syncRange();connect(useRange,&QCheckBox::toggled,this,[=](bool){syncRange();});
        connect(this,&QDialog::accepted,this,[=]{
            const QVariantMap spec=source->value();
            m_cmd.params={{QStringLiteral("id"),id->currentData()},{QStringLiteral("rangeEndId"),useRange->isChecked()?endId->currentData():id->currentData()},
                          {QStringLiteral("op"),op->currentData()},{QStringLiteral("sourceSpec"),spec}};
        });
    } else if (tipo == QLatin1String("string.set")) {
        setWindowTitle(tr("Alterar texto global")); resize(660,360);
        auto* id=comboStrings(this,ed,p.value(QStringLiteral("id"),1).toInt());
        auto* op=new QComboBox(this);
        const QVector<QPair<QString,QString>> operations={{tr("Definir"),"="},{tr("Adicionar ao final"),"+"},{tr("Adicionar ao início"),"prepend"},
            {tr("Inserir na posição"),"insert"},{tr("Substituir ocorrências"),"replace"},{tr("Remover ocorrências"),"remove"},{tr("Limpar"),"clear"},
            {tr("Remover espaços das pontas"),"trim"},{tr("Maiúsculas"),"upper"},{tr("Minúsculas"),"lower"},{tr("Extrair trecho"),"substring"},
            {tr("Obter caractere"),"charAt"},{tr("Obter linha"),"lineAt"},{tr("Copiar primeira linha"),"firstLine"},
            {tr("Remover primeira linha"),"cutFirstLine"},{tr("Remover primeiro caractere"),"cutFirstChar"}};
        for(const auto& item:operations)op->addItem(item.first,item.second);op->setCurrentIndex(qMax(0,op->findData(p.value(QStringLiteral("op"),QStringLiteral("=")).toString())));
        auto source=makeCommonSourceEditor(this,ed,core::CommonValueType::Text,p.value(QStringLiteral("sourceSpec")).toMap(),m_commonContext);
        auto* search=new QLineEdit(p.value(QStringLiteral("search")).toString(),this);
        auto* index=new QSpinBox(this);index->setRange(0,65535);index->setValue(qMax(0,p.value(QStringLiteral("index"),0).toInt()));
        auto* length=new QSpinBox(this);length->setRange(1,65535);length->setValue(qMax(1,p.value(QStringLiteral("length"),1).toInt()));
        form->addRow(tr("Texto global"),id);form->addRow(tr("O que fazer"),op);form->addRow(tr("Texto usado"),source->widget);form->addRow(tr("Procurar por"),search);form->addRow(tr("Posição / linha"),index);form->addRow(tr("Quantidade de caracteres"),length);
        auto sync=[=]{
            const QString k=op->currentData().toString();
            const bool usesSource=k=="="||k=="+"||k=="prepend"||k=="insert"||k=="replace"||k=="remove";
            source->widget->setVisible(usesSource);search->setVisible(k=="replace");
            index->setVisible(k=="insert"||k=="substring"||k=="charAt"||k=="lineAt");
            length->setVisible(k=="substring");
        };sync();connect(op,&QComboBox::currentIndexChanged,this,[=](int){sync();});
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("id"),id->currentData()},{QStringLiteral("op"),op->currentData()},{QStringLiteral("sourceSpec"),source->value()},{QStringLiteral("search"),search->text()},{QStringLiteral("index"),index->value()},{QStringLiteral("length"),length->value()}};});
    } else if (tipo == QLatin1String("variable.math")) {
        setWindowTitle(tr("Calcular valor da variável")); resize(760,620);
        auto* id=comboVariaveis(this,ed,p.value(QStringLiteral("id"),1).toInt());
        auto* useRange=new QCheckBox(tr("Guardar o resultado em várias variáveis"),this);
        const int oldEnd=p.value(QStringLiteral("rangeEndId"),p.value(QStringLiteral("id"),1)).toInt();useRange->setChecked(oldEnd!=p.value(QStringLiteral("id"),1).toInt());
        auto* endId=comboVariaveis(this,ed,oldEnd);
        auto* operation=new QComboBox(this);
        const QVector<QPair<QString,QString>> operations={{tr("A + B"),"add"},{tr("A - B"),"sub"},{tr("A × B"),"mul"},{tr("A ÷ B"),"div"},{tr("A módulo B"),"mod"},{tr("mín(A,B)"),"min"},{tr("máx(A,B)"),"max"},{tr("A elevado a B"),"pow"},{tr("Raiz de A"),"sqrt"},{tr("Absoluto de A"),"abs"},{tr("Arredondar A"),"round"},{tr("Arredondar A para baixo"),"floor"},{tr("Arredondar A para cima"),"ceil"},{tr("sin(A°) × 1000"),"sin"},{tr("cos(A°) × 1000"),"cos"},{tr("atan2(A,B) em graus"),"atan2"},{tr("Limitar A entre B e C"),"clamp"},{tr("Expressão visual (passo a passo)"),"expression"}};
        for(const auto& item:operations)operation->addItem(item.first,item.second);operation->setCurrentIndex(qMax(0,operation->findData(p.value(QStringLiteral("operation"),QStringLiteral("add")).toString())));
        auto a=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,p.value(QStringLiteral("a")).toMap(),m_commonContext);
        auto b=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,p.value(QStringLiteral("b")).toMap(),m_commonContext);
        auto cc=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,p.value(QStringLiteral("c")).toMap(),m_commonContext);
        form->addRow(tr("Guardar resultado em"),id);form->addRow(QString(),useRange);form->addRow(tr("Até a variável"),endId);form->addRow(tr("Cálculo"),operation);form->addRow(tr("A"),a->widget);form->addRow(tr("B"),b->widget);form->addRow(tr("C"),cc->widget);

        // Expressão visual No-Code: o autor monta uma sequência de operações
        // usando exatamente as mesmas origens tipadas do Value Resolver. A
        // ordem é explícita (de cima para baixo), evitando parser de texto ou
        // uma segunda linguagem escondida dentro dos Eventos Comuns.
        auto* expressionBox=new QGroupBox(tr("Expressão visual"),this);
        auto* expressionLayout=new QVBoxLayout(expressionBox);
        auto* expressionHint=new QLabel(tr("O resultado começa no primeiro termo e aplica cada linha seguinte na ordem. Ex.: Ataque → × 2 → − Defesa → + Bônus."),expressionBox);
        expressionHint->setWordWrap(true);expressionHint->setStyleSheet(QStringLiteral("color:#777;font-size:11px"));expressionLayout->addWidget(expressionHint);
        auto* termCount=new QSpinBox(expressionBox);termCount->setRange(1,16);
        const QVariantList savedTerms=p.value(QStringLiteral("terms")).toList();
        termCount->setValue(qBound(1, savedTerms.isEmpty() ? 2 : int(savedTerms.size()), 16));
        auto* countRow=new QHBoxLayout;countRow->addWidget(new QLabel(tr("Quantidade de termos"),expressionBox));countRow->addWidget(termCount);countRow->addStretch(1);expressionLayout->addLayout(countRow);
        QVector<QWidget*> expressionRows; QVector<QComboBox*> expressionOps;
        QVector<std::shared_ptr<CommonSourceEditorState>> expressionSources;
        expressionRows.reserve(16);expressionOps.reserve(16);expressionSources.reserve(16);
        for(int termIndex=0;termIndex<16;++termIndex){
            const QVariantMap saved=termIndex<savedTerms.size()?savedTerms.at(termIndex).toMap():QVariantMap();
            auto* rowHost=new QWidget(expressionBox);auto* rowLayout=new QHBoxLayout(rowHost);rowLayout->setContentsMargins(0,0,0,0);rowLayout->setSpacing(5);
            auto* stepOp=new QComboBox(rowHost);
            if(termIndex==0){stepOp->addItem(tr("Começar com"),QStringLiteral("start"));stepOp->setEnabled(false);}
            else{
                stepOp->addItem(QStringLiteral("+"),QStringLiteral("add"));stepOp->addItem(QStringLiteral("−"),QStringLiteral("sub"));
                stepOp->addItem(QStringLiteral("×"),QStringLiteral("mul"));stepOp->addItem(QStringLiteral("÷"),QStringLiteral("div"));
                stepOp->addItem(QStringLiteral("%"),QStringLiteral("mod"));stepOp->addItem(tr("mín"),QStringLiteral("min"));
                stepOp->addItem(tr("máx"),QStringLiteral("max"));stepOp->addItem(QStringLiteral("^"),QStringLiteral("pow"));
                const int oi=stepOp->findData(saved.value(QStringLiteral("op"),QStringLiteral("add")));if(oi>=0)stepOp->setCurrentIndex(oi);
            }
            QVariantMap sourceSpec=saved.value(QStringLiteral("sourceSpec")).toMap();
            if(sourceSpec.isEmpty())sourceSpec={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),0}};
            auto source=makeCommonSourceEditor(rowHost,ed,core::CommonValueType::Number,sourceSpec,m_commonContext);
            stepOp->setMinimumWidth(termIndex==0?105:56);rowLayout->addWidget(stepOp);rowLayout->addWidget(source->widget,1);
            expressionLayout->addWidget(rowHost);expressionRows.push_back(rowHost);expressionOps.push_back(stepOp);expressionSources.push_back(source);
        }
        v->addWidget(expressionBox);
        auto syncExpressionRows=[=]{for(int i=0;i<expressionRows.size();++i)expressionRows[i]->setVisible(i<termCount->value());};
        connect(termCount,QOverload<int>::of(&QSpinBox::valueChanged),this,[=](int){syncExpressionRows();});syncExpressionRows();

        auto sync=[=]{
            endId->setVisible(useRange->isChecked());
            const QString k=operation->currentData().toString();
            const bool expression=k==QLatin1String("expression");
            const bool unary=k=="sqrt"||k=="abs"||k=="round"||k=="floor"||k=="ceil"||k=="sin"||k=="cos";
            a->widget->setVisible(!expression);b->widget->setVisible(!expression&&!unary);cc->widget->setVisible(!expression&&k=="clamp");
            expressionBox->setVisible(expression);
        };
        sync();connect(useRange,&QCheckBox::toggled,this,[=](bool){sync();});connect(operation,&QComboBox::currentIndexChanged,this,[=](int){sync();});
        connect(this,&QDialog::accepted,this,[=]{
            QVariantMap params={{QStringLiteral("id"),id->currentData()},{QStringLiteral("rangeEndId"),useRange->isChecked()?endId->currentData():id->currentData()},{QStringLiteral("operation"),operation->currentData()},{QStringLiteral("a"),a->value()},{QStringLiteral("b"),b->value()},{QStringLiteral("c"),cc->value()}};
            if(operation->currentData().toString()==QLatin1String("expression")){
                QVariantList terms;
                for(int i=0;i<termCount->value();++i){QVariantMap term{{QStringLiteral("sourceSpec"),expressionSources.at(i)->value()}};if(i>0)term[QStringLiteral("op")]=expressionOps.at(i)->currentData();terms.push_back(term);}
                params[QStringLiteral("terms")]=terms;
            }
            m_cmd.params=params;
        });
    } else if (tipo == QLatin1String("value.get")) {
        setWindowTitle(tr("Consultar valor do jogo")); resize(720,460);
        const QVariantMap oldQuery=p.value(QStringLiteral("query")).toMap();
        auto* value=new QComboBox(this);populateGameValueCombo(value);value->setCurrentIndex(qMax(0,value->findData(oldQuery.value(QStringLiteral("key"),QStringLiteral("player.x")).toString())));
        auto* eventBox=new QComboBox(this);eventBox->addItem(tr("Este evento"),QStringLiteral("self"));for(const core::MapEvent& ev:ed.events())eventBox->addItem(ev.name.isEmpty()?ev.id:ev.name,ev.id);eventBox->setCurrentIndex(qMax(0,eventBox->findData(oldQuery.value(QStringLiteral("eventId"),QStringLiteral("self")))));
        auto* picture=new QSpinBox(this);picture->setRange(1,9999);picture->setValue(oldQuery.value(QStringLiteral("number"),1).toInt());
        auto* x=new QSpinBox(this);auto* y=new QSpinBox(this);x->setRange(-9999,9999);y->setRange(-9999,9999);x->setValue(oldQuery.value(QStringLiteral("x")).toInt());y->setValue(oldQuery.value(QStringLiteral("y")).toInt());
        auto* inputAction=new QComboBox(this);for(core::GameAction a:core::allGameActions())inputAction->addItem(core::gameActionLabel(a),core::gameActionId(a));inputAction->setCurrentIndex(qMax(0,inputAction->findData(oldQuery.value(QStringLiteral("action"),QStringLiteral("confirm")))));
        auto* targetKind=new QComboBox(this);auto* target=new QComboBox(this);
        form->addRow(tr("O que consultar:"),value);form->addRow(tr("Evento de referência:"),eventBox);form->addRow(tr("Imagem nº:"),picture);form->addRow(tr("Posição X:"),x);form->addRow(tr("Posição Y:"),y);form->addRow(tr("Ação do jogador:"),inputAction);form->addRow(tr("Salvar resultado em:"),targetKind);form->addRow(tr("Destino:"),target);
        auto rebuildTarget=[=,this]{
            const QString key=value->currentData().toString();const core::CommonValueType type=gameValueTypeForKey(key);const QString oldKind=p.value(QStringLiteral("target")).toMap().value(QStringLiteral("target"),QStringLiteral("none")).toString();
            targetKind->clear();targetKind->addItem(tr("Ignorar"),QStringLiteral("none"));
            if(type==core::CommonValueType::Number)targetKind->addItem(tr("Variável global"),QStringLiteral("variable"));else if(type==core::CommonValueType::Boolean)targetKind->addItem(tr("Interruptor global"),QStringLiteral("switch"));else targetKind->addItem(tr("Texto global"),QStringLiteral("string"));
            if(m_commonContext){bool any=false;for(const auto& d:m_commonContext->locals)if(d.type==type){any=true;break;}if(any)targetKind->addItem(tr("Local do Evento Comum"),QStringLiteral("commonValue"));}
            int i=targetKind->findData(oldKind);if(i<0)i=qMin(1,targetKind->count()-1);targetKind->setCurrentIndex(qMax(0,i));
        };
        auto fillTarget=[=,this]{target->clear();const QString kind=targetKind->currentData().toString();const core::CommonValueType type=gameValueTypeForKey(value->currentData().toString());if(kind=="variable")for(const auto& d:ed.variables)target->addItem(QStringLiteral("%1 — %2").arg(d.id).arg(d.name),d.id);else if(kind=="switch")for(const auto& d:ed.switches)target->addItem(QStringLiteral("%1 — %2").arg(d.id).arg(d.name),d.id);else if(kind=="string")for(const auto& d:ed.strings)target->addItem(QStringLiteral("%1 — %2").arg(d.id).arg(d.name),d.id);else if(kind=="commonValue"&&m_commonContext)for(const auto& d:m_commonContext->locals)if(d.type==type)target->addItem(d.name,d.id);target->setEnabled(kind!="none");const QVariantMap old=p.value(QStringLiteral("target")).toMap();const QVariant oid=kind=="commonValue"?old.value(QStringLiteral("commonValueId")):old.value(QStringLiteral("id"));int i=target->findData(oid);if(i>=0)target->setCurrentIndex(i);};
        auto sync=[=]{const core::GameValueDescriptor* descriptor=core::gameValueDescriptor(value->currentData().toString());eventBox->setVisible(descriptor&&descriptor->needsEvent);picture->setVisible(descriptor&&descriptor->needsPicture);x->setVisible(descriptor&&descriptor->needsMapPosition);y->setVisible(descriptor&&descriptor->needsMapPosition);inputAction->setVisible(descriptor&&descriptor->needsAction);};
        rebuildTarget();fillTarget();sync();connect(value,&QComboBox::currentIndexChanged,this,[=](int){rebuildTarget();fillTarget();sync();});connect(targetKind,&QComboBox::currentIndexChanged,this,[=](int){fillTarget();});
        connect(this,&QDialog::accepted,this,[=]{const QString key=value->currentData().toString();const core::GameValueDescriptor* descriptor=core::gameValueDescriptor(key);QVariantMap query{{QStringLiteral("key"),key}};if(descriptor&&descriptor->needsEvent)query[QStringLiteral("eventId")]=eventBox->currentData();if(descriptor&&descriptor->needsPicture)query[QStringLiteral("number")]=picture->value();if(descriptor&&descriptor->needsMapPosition){query[QStringLiteral("x")]=x->value();query[QStringLiteral("y")]=y->value();}if(descriptor&&descriptor->needsAction)query[QStringLiteral("action")]=inputAction->currentData();QVariantMap out{{QStringLiteral("target"),targetKind->currentData()}};if(targetKind->currentData().toString()=="commonValue")out[QStringLiteral("commonValueId")]=target->currentData();else if(targetKind->currentData().toString()!="none")out[QStringLiteral("id")]=target->currentData();m_cmd.params={{QStringLiteral("query"),query},{QStringLiteral("valueType"),core::commonValueTypeId(gameValueTypeForKey(key))},{QStringLiteral("target"),out}};});
    } else if (tipo == QLatin1String("map.runtime.tile")) {
        setWindowTitle(tr("Alterar Tile em Runtime"));resize(780,520);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());
        auto* layer=runtimeTileLayerCombo(this,ed,map->currentData().toString(),p.value(QStringLiteral("layerId")).toString());
        auto xs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("x")),m_commonContext);
        auto ys=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("y")),m_commonContext);
        auto* clear=new QCheckBox(tr("Limpar a célula (sem tile)"),this);clear->setChecked(p.value(QStringLiteral("clear"),false).toBool());
        QString savedTs=p.value(QStringLiteral("tilesetId")).toString();if(savedTs.isEmpty()&&ed.tsSel.valid()&&ed.tilesetAt(ed.tsSel.tilesetIdx))savedTs=ed.tilesetAt(ed.tsSel.tilesetIdx)->id;
        auto* tileset=runtimeTilesetCombo(this,ed,savedTs);auto* tx=new QSpinBox(this);auto* ty=new QSpinBox(this);tx->setRange(0,99999);ty->setRange(0,99999);tx->setValue(p.value(QStringLiteral("tx"),ed.tsSel.x).toInt());ty->setValue(p.value(QStringLiteral("ty"),ed.tsSel.y).toInt());
        auto* useSelected=new QPushButton(tr("Usar tile selecionado na paleta"),this);useSelected->setEnabled(ed.tsSel.valid());
        form->addRow(tr("Mapa"),map);form->addRow(tr("Camada"),layer);form->addRow(tr("X da célula"),xs->widget);form->addRow(tr("Y da célula"),ys->widget);form->addRow(QString(),clear);form->addRow(tr("Tileset"),tileset);form->addRow(tr("Tile X"),tx);form->addRow(tr("Tile Y"),ty);form->addRow(QString(),useSelected);
        auto sync=[=]{tileset->setEnabled(!clear->isChecked());tx->setEnabled(!clear->isChecked());ty->setEnabled(!clear->isChecked());};sync();
        connect(clear,&QCheckBox::toggled,this,[=](bool){sync();});connect(map,&QComboBox::currentIndexChanged,this,[=](int){fillRuntimeTileLayerCombo(layer,ed,map->currentData().toString());});
        connect(useSelected,&QPushButton::clicked,this,[=]{if(!ed.tsSel.valid())return;const core::Tileset* ts=ed.tilesetAt(ed.tsSel.tilesetIdx);if(!ts)return;int i=tileset->findData(ts->id);if(i>=0)tileset->setCurrentIndex(i);tx->setValue(ed.tsSel.x);ty->setValue(ed.tsSel.y);clear->setChecked(false);});
        auto* note=new QLabel(tr("A alteração existe somente no GameState da partida. O MapDoc do projeto permanece intacto e o Save grava apenas o delta."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("layerId"),layer->currentData()},{QStringLiteral("x"),xs->value()},{QStringLiteral("y"),ys->value()},{QStringLiteral("clear"),clear->isChecked()},{QStringLiteral("tilesetId"),tileset->currentData()},{QStringLiteral("tx"),tx->value()},{QStringLiteral("ty"),ty->value()}};});
    } else if (tipo == QLatin1String("map.runtime.fill")) {
        setWindowTitle(tr("Preencher Área em Runtime"));resize(800,610);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());auto* layer=runtimeTileLayerCombo(this,ed,map->currentData().toString(),p.value(QStringLiteral("layerId")).toString());
        auto xs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("x")),m_commonContext);auto ys=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("y")),m_commonContext);auto ws=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("width"),1),m_commonContext);auto hs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("height"),1),m_commonContext);
        auto* clear=new QCheckBox(tr("Limpar as células"),this);clear->setChecked(p.value(QStringLiteral("clear"),false).toBool());QString savedTs=p.value(QStringLiteral("tilesetId")).toString();if(savedTs.isEmpty()&&ed.tsSel.valid()&&ed.tilesetAt(ed.tsSel.tilesetIdx))savedTs=ed.tilesetAt(ed.tsSel.tilesetIdx)->id;auto* tileset=runtimeTilesetCombo(this,ed,savedTs);auto* tx=new QSpinBox(this);auto* ty=new QSpinBox(this);tx->setRange(0,99999);ty->setRange(0,99999);tx->setValue(p.value(QStringLiteral("tx"),ed.tsSel.x).toInt());ty->setValue(p.value(QStringLiteral("ty"),ed.tsSel.y).toInt());
        form->addRow(tr("Mapa"),map);form->addRow(tr("Camada"),layer);form->addRow(tr("X inicial"),xs->widget);form->addRow(tr("Y inicial"),ys->widget);form->addRow(tr("Largura"),ws->widget);form->addRow(tr("Altura"),hs->widget);form->addRow(QString(),clear);form->addRow(tr("Tileset"),tileset);form->addRow(tr("Tile X"),tx);form->addRow(tr("Tile Y"),ty);
        auto sync=[=]{tileset->setEnabled(!clear->isChecked());tx->setEnabled(!clear->isChecked());ty->setEnabled(!clear->isChecked());};sync();connect(clear,&QCheckBox::toggled,this,[=](bool){sync();});connect(map,&QComboBox::currentIndexChanged,this,[=](int){fillRuntimeTileLayerCombo(layer,ed,map->currentData().toString());});
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("layerId"),layer->currentData()},{QStringLiteral("x"),xs->value()},{QStringLiteral("y"),ys->value()},{QStringLiteral("width"),ws->value()},{QStringLiteral("height"),hs->value()},{QStringLiteral("clear"),clear->isChecked()},{QStringLiteral("tilesetId"),tileset->currentData()},{QStringLiteral("tx"),tx->value()},{QStringLiteral("ty"),ty->value()}};});
    } else if (tipo == QLatin1String("map.runtime.copy")) {
        setWindowTitle(tr("Copiar Área em Runtime"));resize(820,650);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());auto* sourceLayer=runtimeTileLayerCombo(this,ed,map->currentData().toString(),p.value(QStringLiteral("sourceLayerId")).toString());auto* targetLayer=runtimeTileLayerCombo(this,ed,map->currentData().toString(),p.value(QStringLiteral("targetLayerId")).toString());
        auto sx=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("sourceX")),m_commonContext);auto sy=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("sourceY")),m_commonContext);auto ws=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("width"),1),m_commonContext);auto hs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("height"),1),m_commonContext);auto txs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("targetX")),m_commonContext);auto tys=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("targetY")),m_commonContext);
        form->addRow(tr("Mapa"),map);form->addRow(tr("Camada de origem"),sourceLayer);form->addRow(tr("Origem X"),sx->widget);form->addRow(tr("Origem Y"),sy->widget);form->addRow(tr("Largura"),ws->widget);form->addRow(tr("Altura"),hs->widget);form->addRow(tr("Camada de destino"),targetLayer);form->addRow(tr("Destino X"),txs->widget);form->addRow(tr("Destino Y"),tys->widget);
        connect(map,&QComboBox::currentIndexChanged,this,[=](int){const QString id=map->currentData().toString();fillRuntimeTileLayerCombo(sourceLayer,ed,id);fillRuntimeTileLayerCombo(targetLayer,ed,id);});
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("sourceLayerId"),sourceLayer->currentData()},{QStringLiteral("sourceX"),sx->value()},{QStringLiteral("sourceY"),sy->value()},{QStringLiteral("width"),ws->value()},{QStringLiteral("height"),hs->value()},{QStringLiteral("targetLayerId"),targetLayer->currentData()},{QStringLiteral("targetX"),txs->value()},{QStringLiteral("targetY"),tys->value()}};});
    } else if (tipo == QLatin1String("map.runtime.passage")) {
        setWindowTitle(tr("Alterar Passagem em Runtime"));resize(720,470);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());auto xs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("x")),m_commonContext);auto ys=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("y")),m_commonContext);auto* inherit=new QCheckBox(tr("Herdar passagem dos tiles do projeto"),this);const int savedMask=p.value(QStringLiteral("mask"),-1).toInt();inherit->setChecked(savedMask<0);auto* top=new QCheckBox(tr("Bloquear cima"),this);auto* right=new QCheckBox(tr("Bloquear direita"),this);auto* bottom=new QCheckBox(tr("Bloquear baixo"),this);auto* left=new QCheckBox(tr("Bloquear esquerda"),this);if(savedMask>=0){top->setChecked(savedMask&core::Editor::SideTop);right->setChecked(savedMask&core::Editor::SideRight);bottom->setChecked(savedMask&core::Editor::SideBottom);left->setChecked(savedMask&core::Editor::SideLeft);}auto* sides=new QWidget(this);auto* sr=new QHBoxLayout(sides);sr->setContentsMargins(0,0,0,0);sr->addWidget(top);sr->addWidget(right);sr->addWidget(bottom);sr->addWidget(left);
        form->addRow(tr("Mapa"),map);form->addRow(tr("X"),xs->widget);form->addRow(tr("Y"),ys->widget);form->addRow(QString(),inherit);form->addRow(tr("Lados bloqueados"),sides);auto sync=[=]{sides->setEnabled(!inherit->isChecked());};sync();connect(inherit,&QCheckBox::toggled,this,[=](bool){sync();});
        connect(this,&QDialog::accepted,this,[=]{int mask=-1;if(!inherit->isChecked())mask=(top->isChecked()?core::Editor::SideTop:0)|(right->isChecked()?core::Editor::SideRight:0)|(bottom->isChecked()?core::Editor::SideBottom:0)|(left->isChecked()?core::Editor::SideLeft:0);m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("x"),xs->value()},{QStringLiteral("y"),ys->value()},{QStringLiteral("mask"),mask}};});
    } else if (tipo == QLatin1String("map.runtime.terrain")) {
        setWindowTitle(tr("Alterar terreno durante o jogo"));resize(720,430);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());auto xs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("x")),m_commonContext);auto ys=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("y")),m_commonContext);auto terrain=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("terrain")),m_commonContext);form->addRow(tr("Mapa"),map);form->addRow(tr("X"),xs->widget);form->addRow(tr("Y"),ys->widget);form->addRow(tr("Número do terreno (RPG Maker)"),terrain->widget);connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("x"),xs->value()},{QStringLiteral("y"),ys->value()},{QStringLiteral("terrain"),terrain->value()}};});
    } else if (tipo == QLatin1String("map.runtime.tileset")) {
        setWindowTitle(tr("Remapear Tileset em Runtime"));resize(700,390);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());auto* source=runtimeTilesetCombo(this,ed,p.value(QStringLiteral("sourceTilesetId")).toString());auto* target=runtimeTilesetCombo(this,ed,p.value(QStringLiteral("targetTilesetId")).toString(),true);form->addRow(tr("Mapa"),map);form->addRow(tr("Tileset usado no mapa"),source);form->addRow(tr("Mostrar/usar como"),target);auto* note=new QLabel(tr("Como a LUDO permite vários tilesets no mesmo mapa, esta operação é um remapeamento estável: tiles do tileset de origem passam a usar o tileset de destino mantendo Tile X/Y."),this);note->setWordWrap(true);v->addWidget(note);connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("sourceTilesetId"),source->currentData()},{QStringLiteral("targetTilesetId"),target->currentData()}};});
    } else if (tipo == QLatin1String("map.runtime.reset")) {
        setWindowTitle(tr("Resetar Alterações do Mapa Runtime"));resize(800,590);
        auto* map=runtimeMapCombo(this,ed,p.value(QStringLiteral("mapId")).toString());auto* scope=new QComboBox(this);scope->addItem(tr("Uma célula"),QStringLiteral("cell"));scope->addItem(tr("Uma área"),QStringLiteral("area"));scope->addItem(tr("Remapeamento de tileset"),QStringLiteral("tileset"));scope->addItem(tr("Mapa inteiro"),QStringLiteral("map"));scope->setCurrentIndex(qMax(0,scope->findData(p.value(QStringLiteral("scope"),QStringLiteral("cell")))));auto* layer=runtimeTileLayerCombo(this,ed,map->currentData().toString(),p.value(QStringLiteral("layerId")).toString());auto xs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("x")),m_commonContext);auto ys=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("y")),m_commonContext);auto ws=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("width"),1),m_commonContext);auto hs=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,numberSpecFromParam(p,QStringLiteral("height"),1),m_commonContext);auto* sourceTs=runtimeTilesetCombo(this,ed,p.value(QStringLiteral("sourceTilesetId")).toString());
        form->addRow(tr("Mapa"),map);form->addRow(tr("Escopo"),scope);form->addRow(tr("Camada"),layer);form->addRow(tr("X"),xs->widget);form->addRow(tr("Y"),ys->widget);form->addRow(tr("Largura"),ws->widget);form->addRow(tr("Altura"),hs->widget);form->addRow(tr("Tileset"),sourceTs);
        auto sync=[=]{const QString s=scope->currentData().toString();layer->setVisible(s==QLatin1String("cell"));xs->widget->setVisible(s==QLatin1String("cell")||s==QLatin1String("area"));ys->widget->setVisible(s==QLatin1String("cell")||s==QLatin1String("area"));ws->widget->setVisible(s==QLatin1String("area"));hs->widget->setVisible(s==QLatin1String("area"));sourceTs->setVisible(s==QLatin1String("tileset"));};sync();connect(scope,&QComboBox::currentIndexChanged,this,[=](int){sync();});connect(map,&QComboBox::currentIndexChanged,this,[=](int){fillRuntimeTileLayerCombo(layer,ed,map->currentData().toString());});
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("scope"),scope->currentData()},{QStringLiteral("layerId"),layer->currentData()},{QStringLiteral("x"),xs->value()},{QStringLiteral("y"),ys->value()},{QStringLiteral("width"),ws->value()},{QStringLiteral("height"),hs->value()},{QStringLiteral("sourceTilesetId"),sourceTs->currentData()}};});
    } else if (tipo == QLatin1String("database.get")) {
        setWindowTitle(tr("Ler campo do Banco de Dados")); resize(760,460);
        auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString());
        auto record=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("recordSpec")).toMap(),p.value(QStringLiteral("recordId")).toString(),m_commonContext);
        auto* field=new QComboBox(this);
        auto* targetStack=new QStackedWidget(this);
        auto numberTarget=makeValueTargetEditor(targetStack,ed,core::CommonValueType::Number,p.value(QStringLiteral("target")).toMap(),m_commonContext);
        auto boolTarget=makeValueTargetEditor(targetStack,ed,core::CommonValueType::Boolean,p.value(QStringLiteral("target")).toMap(),m_commonContext);
        auto textTarget=makeValueTargetEditor(targetStack,ed,core::CommonValueType::Text,p.value(QStringLiteral("target")).toMap(),m_commonContext);
        targetStack->addWidget(numberTarget->widget);targetStack->addWidget(boolTarget->widget);targetStack->addWidget(textTarget->widget);
        form->addRow(tr("Banco"),database);form->addRow(tr("Registro"),record->widget);form->addRow(tr("Campo"),field);form->addRow(tr("Guardar em"),targetStack);
        const QString savedField=p.value(QStringLiteral("fieldId")).toString();
        auto syncField=[=]{
            const QString dbId=database->currentData().toString();record->refresh(dbId);const QString keep=field->currentData().toString().isEmpty()?savedField:field->currentData().toString();QSignalBlocker b(field);field->clear();
            if(const auto* db=ed.customDatabase(dbId))for(const auto& f:db->fields)field->addItem(f.name.isEmpty()?f.id:f.name,f.id);
            int i=field->findData(keep);if(i>=0)field->setCurrentIndex(i);
            const auto* db=ed.customDatabase(dbId);const auto* f=db?core::customDatabaseFieldById(*db,field->currentData().toString()):nullptr;const auto t=customFieldUiType(f);targetStack->setCurrentIndex(t==core::CommonValueType::Number?0:t==core::CommonValueType::Boolean?1:2);
        };
        syncField();connect(database,&QComboBox::currentIndexChanged,this,[=](int){syncField();});connect(field,&QComboBox::currentIndexChanged,this,[=](int){syncField();});
        connect(this,&QDialog::accepted,this,[=]{const auto* db=ed.customDatabase(database->currentData().toString());const auto* f=db?core::customDatabaseFieldById(*db,field->currentData().toString()):nullptr;const auto t=customFieldUiType(f);const QVariantMap target=t==core::CommonValueType::Number?numberTarget->result():t==core::CommonValueType::Boolean?boolTarget->result():textTarget->result();m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("recordId"),record->recordId()},{QStringLiteral("recordSpec"),record->recordSpec()},{QStringLiteral("fieldId"),field->currentData()},{QStringLiteral("target"),target}};});
    } else if (tipo == QLatin1String("database.set")) {
        setWindowTitle(tr("Alterar campo durante o jogo")); resize(760,500);
        auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString(),true);
        auto record=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("recordSpec")).toMap(),p.value(QStringLiteral("recordId")).toString(),m_commonContext);
        auto* field=new QComboBox(this);auto* sourceStack=new QStackedWidget(this);const QVariantMap savedSource=p.value(QStringLiteral("sourceSpec")).toMap();
        auto numberSource=makeCommonSourceEditor(sourceStack,ed,core::CommonValueType::Number,savedSource,m_commonContext);auto boolSource=makeCommonSourceEditor(sourceStack,ed,core::CommonValueType::Boolean,savedSource,m_commonContext);auto textSource=makeCommonSourceEditor(sourceStack,ed,core::CommonValueType::Text,savedSource,m_commonContext);
        sourceStack->addWidget(numberSource->widget);sourceStack->addWidget(boolSource->widget);sourceStack->addWidget(textSource->widget);
        form->addRow(tr("Banco de dados:"),database);form->addRow(tr("Registro:"),record->widget);form->addRow(tr("Campo:"),field);form->addRow(tr("Novo valor:"),sourceStack);
        const QString savedField=p.value(QStringLiteral("fieldId")).toString();auto syncField=[=]{const QString dbId=database->currentData().toString();record->refresh(dbId);const QString keep=field->currentData().toString().isEmpty()?savedField:field->currentData().toString();QSignalBlocker b(field);field->clear();if(const auto* db=ed.customDatabase(dbId))for(const auto& f:db->fields)field->addItem(f.name.isEmpty()?f.id:f.name,f.id);int i=field->findData(keep);if(i>=0)field->setCurrentIndex(i);const auto* db=ed.customDatabase(dbId);const auto* f=db?core::customDatabaseFieldById(*db,field->currentData().toString()):nullptr;const auto t=customFieldUiType(f);sourceStack->setCurrentIndex(t==core::CommonValueType::Number?0:t==core::CommonValueType::Boolean?1:2);};syncField();connect(database,&QComboBox::currentIndexChanged,this,[=](int){syncField();});connect(field,&QComboBox::currentIndexChanged,this,[=](int){syncField();});
        connect(this,&QDialog::accepted,this,[=]{const auto* db=ed.customDatabase(database->currentData().toString());const auto* f=db?core::customDatabaseFieldById(*db,field->currentData().toString()):nullptr;const auto t=customFieldUiType(f);const QVariantMap source=t==core::CommonValueType::Number?numberSource->value():t==core::CommonValueType::Boolean?boolSource->value():textSource->value();m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("recordId"),record->recordId()},{QStringLiteral("recordSpec"),record->recordSpec()},{QStringLiteral("fieldId"),field->currentData()},{QStringLiteral("sourceSpec"),source}};});
    } else if (tipo == QLatin1String("database.find")) {
        setWindowTitle(tr("Procurar registro no Banco de Dados")); resize(760,520);
        auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString());auto* field=new QComboBox(this);auto* operation=new QComboBox(this);auto* sourceStack=new QStackedWidget(this);const QVariantMap savedSource=p.value(QStringLiteral("sourceSpec")).toMap();
        auto numberSource=makeCommonSourceEditor(sourceStack,ed,core::CommonValueType::Number,savedSource,m_commonContext);auto boolSource=makeCommonSourceEditor(sourceStack,ed,core::CommonValueType::Boolean,savedSource,m_commonContext);auto textSource=makeCommonSourceEditor(sourceStack,ed,core::CommonValueType::Text,savedSource,m_commonContext);sourceStack->addWidget(numberSource->widget);sourceStack->addWidget(boolSource->widget);sourceStack->addWidget(textSource->widget);
        auto target=makeValueTargetEditor(this,ed,core::CommonValueType::Text,p.value(QStringLiteral("target")).toMap(),m_commonContext);
        form->addRow(tr("Banco"),database);form->addRow(tr("Campo"),field);form->addRow(tr("Comparação"),operation);form->addRow(tr("Valor procurado"),sourceStack);form->addRow(tr("Guardar ID encontrado em"),target->widget);
        const QString savedField=p.value(QStringLiteral("fieldId")).toString(),savedOp=p.value(QStringLiteral("operation"),QStringLiteral("equals")).toString();
        auto syncField=[=]{const QString dbId=database->currentData().toString();const QString keep=field->currentData().toString().isEmpty()?savedField:field->currentData().toString();{QSignalBlocker b(field);field->clear();if(const auto* db=ed.customDatabase(dbId))for(const auto& f:db->fields)field->addItem(f.name.isEmpty()?f.id:f.name,f.id);int i=field->findData(keep);if(i>=0)field->setCurrentIndex(i);}const auto* db=ed.customDatabase(dbId);const auto* f=db?core::customDatabaseFieldById(*db,field->currentData().toString()):nullptr;const auto t=customFieldUiType(f);sourceStack->setCurrentIndex(t==core::CommonValueType::Number?0:t==core::CommonValueType::Boolean?1:2);const QString keepOp=operation->currentData().toString().isEmpty()?savedOp:operation->currentData().toString();QSignalBlocker ob(operation);operation->clear();operation->addItem(tr("Igual a"),QStringLiteral("equals"));operation->addItem(tr("Diferente de"),QStringLiteral("notEquals"));if(f&&f->type==core::CustomDatabaseFieldType::Number){operation->addItem(tr("Menor que"),QStringLiteral("less"));operation->addItem(tr("Menor ou igual"),QStringLiteral("lessEqual"));operation->addItem(tr("Maior que"),QStringLiteral("greater"));operation->addItem(tr("Maior ou igual"),QStringLiteral("greaterEqual"));}else if(f&&f->type==core::CustomDatabaseFieldType::Text){operation->addItem(tr("Contém"),QStringLiteral("contains"));operation->addItem(tr("Começa com"),QStringLiteral("startsWith"));operation->addItem(tr("Termina com"),QStringLiteral("endsWith"));}int oi=operation->findData(keepOp);if(oi>=0)operation->setCurrentIndex(oi);};syncField();connect(database,&QComboBox::currentIndexChanged,this,[=](int){syncField();});connect(field,&QComboBox::currentIndexChanged,this,[=](int){syncField();});
        connect(this,&QDialog::accepted,this,[=]{const auto* db=ed.customDatabase(database->currentData().toString());const auto* f=db?core::customDatabaseFieldById(*db,field->currentData().toString()):nullptr;const auto t=customFieldUiType(f);const QVariantMap source=t==core::CommonValueType::Number?numberSource->value():t==core::CommonValueType::Boolean?boolSource->value():textSource->value();m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("fieldId"),field->currentData()},{QStringLiteral("operation"),operation->currentData()},{QStringLiteral("sourceSpec"),source},{QStringLiteral("target"),target->result()}};});
    } else if (tipo == QLatin1String("database.count")) {
        setWindowTitle(tr("Contar registros"));auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString());auto target=makeValueTargetEditor(this,ed,core::CommonValueType::Number,p.value(QStringLiteral("target")).toMap(),m_commonContext);form->addRow(tr("Banco"),database);form->addRow(tr("Guardar quantidade em"),target->widget);connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("target"),target->result()}};});
    } else if (tipo == QLatin1String("database.exists")) {
        setWindowTitle(tr("Verificar se registro existe"));auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString());auto record=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("recordSpec")).toMap(),p.value(QStringLiteral("recordId")).toString(),m_commonContext);auto target=makeValueTargetEditor(this,ed,core::CommonValueType::Boolean,p.value(QStringLiteral("target")).toMap(),m_commonContext);form->addRow(tr("Banco"),database);form->addRow(tr("Registro"),record->widget);form->addRow(tr("Guardar resultado em"),target->widget);connect(database,&QComboBox::currentIndexChanged,this,[=](int){record->refresh(database->currentData().toString());});connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("recordId"),record->recordId()},{QStringLiteral("recordSpec"),record->recordSpec()},{QStringLiteral("target"),target->result()}};});
    } else if (tipo == QLatin1String("database.copy")) {
        setWindowTitle(tr("Copiar dados entre registros"));resize(720,420);auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString(),true);auto source=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("sourceRecordSpec")).toMap(),p.value(QStringLiteral("sourceRecordId")).toString(),m_commonContext);auto target=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("targetRecordSpec")).toMap(),p.value(QStringLiteral("targetRecordId")).toString(),m_commonContext);form->addRow(tr("Banco de dados:"),database);form->addRow(tr("Copiar de:"),source->widget);form->addRow(tr("Copiar para:"),target->widget);connect(database,&QComboBox::currentIndexChanged,this,[=](int){const QString id=database->currentData().toString();source->refresh(id);target->refresh(id);});connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("sourceRecordId"),source->recordId()},{QStringLiteral("sourceRecordSpec"),source->recordSpec()},{QStringLiteral("targetRecordId"),target->recordId()},{QStringLiteral("targetRecordSpec"),target->recordSpec()}};});
    } else if (tipo == QLatin1String("database.reset")) {
        setWindowTitle(tr("Restaurar registro"));auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString(),true);auto record=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("recordSpec")).toMap(),p.value(QStringLiteral("recordId")).toString(),m_commonContext);form->addRow(tr("Banco de dados:"),database);form->addRow(tr("Registro:"),record->widget);connect(database,&QComboBox::currentIndexChanged,this,[=](int){record->refresh(database->currentData().toString());});connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("recordId"),record->recordId()},{QStringLiteral("recordSpec"),record->recordSpec()}};});
    } else if (tipo == QLatin1String("database.recordInfo")) {
        setWindowTitle(tr("Obter ID, nome ou número do registro"));auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString());auto record=makeDatabaseRecordSelector(this,ed,database->currentData().toString(),p.value(QStringLiteral("recordSpec")).toMap(),p.value(QStringLiteral("recordId")).toString(),m_commonContext);auto* info=new QComboBox(this);info->addItem(tr("ID estável"),QStringLiteral("id"));info->addItem(tr("Nome"),QStringLiteral("name"));info->addItem(tr("Número"),QStringLiteral("number"));info->setCurrentIndex(qMax(0,info->findData(p.value(QStringLiteral("info"),QStringLiteral("id")))));auto* targetStack=new QStackedWidget(this);auto textTarget=makeValueTargetEditor(targetStack,ed,core::CommonValueType::Text,p.value(QStringLiteral("target")).toMap(),m_commonContext);auto numberTarget=makeValueTargetEditor(targetStack,ed,core::CommonValueType::Number,p.value(QStringLiteral("target")).toMap(),m_commonContext);targetStack->addWidget(textTarget->widget);targetStack->addWidget(numberTarget->widget);auto sync=[=]{targetStack->setCurrentIndex(info->currentData().toString()==QLatin1String("number")?1:0);};sync();form->addRow(tr("Banco"),database);form->addRow(tr("Registro"),record->widget);form->addRow(tr("Informação"),info);form->addRow(tr("Guardar em"),targetStack);connect(database,&QComboBox::currentIndexChanged,this,[=](int){record->refresh(database->currentData().toString());});connect(info,&QComboBox::currentIndexChanged,this,[=](int){sync();});connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("recordId"),record->recordId()},{QStringLiteral("recordSpec"),record->recordSpec()},{QStringLiteral("info"),info->currentData()},{QStringLiteral("target"),info->currentData().toString()==QLatin1String("number")?numberTarget->result():textTarget->result()}};});
    } else if (tipo == QLatin1String("database.each")) {
        setWindowTitle(tr("Para cada registro"));auto* database=customDatabaseCombo(this,ed,p.value(QStringLiteral("databaseId")).toString());auto target=makeValueTargetEditor(this,ed,core::CommonValueType::Text,p.value(QStringLiteral("target")).toMap(),m_commonContext);form->addRow(tr("Banco"),database);form->addRow(tr("Guardar ID atual em"),target->widget);auto* note=new QLabel(tr("Os comandos inseridos dentro deste bloco serão executados uma vez para cada registro, na ordem do banco."),this);note->setWordWrap(true);v->addWidget(note);connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("databaseId"),database->currentData()},{QStringLiteral("target"),target->result()}};});
    } else if (tipo == QLatin1String("common.local.set")) {
        setWindowTitle(tr("Mudar parâmetro/local"));
        auto* target = new QComboBox(this);
        if (m_commonContext) {
            for (const core::CommonEventParameter& def : m_commonContext->parameters)
                target->addItem(tr("Parâmetro: %1 — %2").arg(def.name, core::commonValueTypeLabel(def.type)), def.id);
            for (const core::CommonEventLocal& def : m_commonContext->locals)
                target->addItem(tr("Local: %1 — %2").arg(def.name, core::commonValueTypeLabel(def.type)), def.id);
        }
        if (target->count() == 0) target->addItem(tr("(nenhum parâmetro/local definido)"), QString());
        target->setCurrentIndex(qMax(0, target->findData(p.value(QStringLiteral("id")).toString())));

        auto* op = new QComboBox(this);
        auto* sourceHost = new QWidget(this);
        auto* sourceLayout = new QVBoxLayout(sourceHost); sourceLayout->setContentsMargins(0,0,0,0);
        auto sourceState = std::make_shared<std::shared_ptr<CommonSourceEditorState>>();
        const QVariantMap oldSpec = p.value(QStringLiteral("sourceSpec")).toMap();
        const QString savedOp = p.value(QStringLiteral("op"), QStringLiteral("=")).toString();

        auto rebuildForTarget = [=, this] {
            core::CommonValueType type = core::CommonValueType::Number;
            commonUiValueType(m_commonContext, target->currentData().toString(), &type);
            QVariantMap previous = *sourceState ? (*sourceState)->value() : oldSpec;
            if (*sourceState && (*sourceState)->widget) {
                sourceLayout->removeWidget((*sourceState)->widget);
                (*sourceState)->widget->deleteLater();
            }
            *sourceState = makeCommonSourceEditor(sourceHost, ed, type, previous, m_commonContext);
            sourceLayout->addWidget((*sourceState)->widget);

            const QString previousOp = op->count() > 0 ? op->currentData().toString() : savedOp;
            QSignalBlocker blocker(op); op->clear();
            if (type == core::CommonValueType::Number)
                for (const QString& value : {QStringLiteral("="),QStringLiteral("+"),QStringLiteral("-"),QStringLiteral("*"),QStringLiteral("/"),QStringLiteral("%")}) op->addItem(value,value);
            else if (type == core::CommonValueType::Boolean) {
                op->addItem(tr("Definir"),QStringLiteral("="));
                op->addItem(tr("Inverter"),QStringLiteral("toggle"));
            } else {
                op->addItem(tr("Definir"),QStringLiteral("="));
                op->addItem(tr("Concatenar"),QStringLiteral("+"));
            }
            int oi = op->findData(previousOp); if (oi < 0) oi = op->findData(savedOp); if (oi < 0) oi = 0; op->setCurrentIndex(oi);
            sourceHost->setVisible(op->currentData().toString() != QLatin1String("toggle"));
        };
        rebuildForTarget();
        connect(target,&QComboBox::currentIndexChanged,this,[rebuildForTarget](int){rebuildForTarget();});
        connect(op,&QComboBox::currentIndexChanged,this,[=](int){sourceHost->setVisible(op->currentData().toString()!=QLatin1String("toggle"));});

        form->addRow(tr("Destino"), target);
        form->addRow(tr("Operação"), op);
        form->addRow(tr("Valor"), sourceHost);
        auto* note = new QLabel(tr("Parâmetros e locais existem somente durante esta execução do Evento Comum. Cada chamada possui seu próprio contexto e as origens exibidas respeitam o tipo do destino."), this);
        note->setWordWrap(true); note->setStyleSheet(QStringLiteral("color:#777;font-size:11px")); v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{
            QVariantMap spec; if (*sourceState) spec=(*sourceState)->value();
            m_cmd.params={{QStringLiteral("id"),target->currentData()},
                          {QStringLiteral("op"),op->currentData()},
                          {QStringLiteral("sourceSpec"),spec}};
        });
    } else if (tipo == QLatin1String("common.return")) {
        setWindowTitle(tr("Retornar do Evento Comum"));
        const core::CommonValueType returnType = m_commonContext
            ? m_commonContext->returnValue.type : core::CommonValueType::Number;
        const QVariantMap oldSpec=p.value(QStringLiteral("sourceSpec")).toMap();
        auto sourceState=makeCommonSourceEditor(this,ed,returnType,oldSpec,m_commonContext);
        form->addRow(tr("Valor de retorno"),sourceState->widget);
        if(!m_commonContext||!m_commonContext->returnValue.enabled){
            auto* warning=new QLabel(tr("Este Evento Comum não possui retorno habilitado na Assinatura. Habilite o retorno antes de usar este comando."),this);
            warning->setWordWrap(true); warning->setStyleSheet(QStringLiteral("color:#b87300")); v->addWidget(warning);
        } else {
            auto* hint=new QLabel(tr("Tipo esperado: %1").arg(core::commonValueTypeLabel(returnType)),this);
            hint->setStyleSheet(QStringLiteral("color:#777;font-size:11px")); v->addWidget(hint);
        }
        connect(this,&QDialog::accepted,this,[this,sourceState]{m_cmd.params={{QStringLiteral("sourceSpec"),sourceState->value()}};});
    } else if(tipo==QLatin1String("input.wait")){
        setWindowTitle(tr("Esperar ação do jogador"));
        auto* action=new QComboBox(this);for(core::GameAction a:core::allGameActions())action->addItem(core::gameActionLabel(a),core::gameActionId(a));
        int ai=action->findData(p.value(QStringLiteral("action"),QStringLiteral("confirm")));if(ai>=0)action->setCurrentIndex(ai);
        auto* state=new QComboBox(this);state->addItem(tr("Acabou de pressionar"),QStringLiteral("pressed"));state->addItem(tr("Está pressionada"),QStringLiteral("held"));state->addItem(tr("Acabou de soltar"),QStringLiteral("released"));
        int si=state->findData(p.value(QStringLiteral("state"),QStringLiteral("pressed")));if(si>=0)state->setCurrentIndex(si);
        form->addRow(tr("Ação"),action);form->addRow(tr("Quando continuar"),state);
        auto* note=new QLabel(tr("O evento continua quando a ação escolhida acontecer. Funciona da mesma forma com teclado ou controle."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("action"),action->currentData()},{QStringLiteral("state"),state->currentData()}};});
    } else if(tipo==QLatin1String("input.number")){
        setWindowTitle(tr("Digitar número"));
        auto* variable=comboVariaveis(this,ed,p.value(QStringLiteral("variableId"),1).toInt());
        auto* title=new QLineEdit(p.value(QStringLiteral("title"),tr("Digite um número")).toString(),this);
        auto* prompt=new QLineEdit(p.value(QStringLiteral("prompt"),tr("Valor:")).toString(),this);
        auto minimum=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(p.value(QStringLiteral("minimum")),0),m_commonContext);
        auto maximum=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(p.value(QStringLiteral("maximum")),9999),m_commonContext);
        form->addRow(tr("Título"),title);form->addRow(tr("Texto"),prompt);form->addRow(tr("Gravar em"),variable);form->addRow(tr("Mínimo"),minimum->widget);form->addRow(tr("Máximo"),maximum->widget);
        auto* note=new QLabel(tr("O jogador escolhe um número dentro dos limites definidos. Se cancelar, a variável continua com o valor que já tinha."),this);note->setWordWrap(true);note->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));v->addWidget(note);
        connect(this,&QDialog::accepted,this,[this,variable,title,prompt,minimum,maximum]{m_cmd.params={{QStringLiteral("variableId"),variable->currentData()},{QStringLiteral("title"),title->text().trimmed()},{QStringLiteral("prompt"),prompt->text().trimmed()},{QStringLiteral("minimum"),minimum->value()},{QStringLiteral("maximum"),maximum->value()}};});
    } else if (tipo == QLatin1String("input.text")) {
        setWindowTitle(tr("Digitar texto"));
        auto* stringBox=comboStrings(this,ed,p.value(QStringLiteral("stringId"),1).toInt());
        auto* title=new QLineEdit(p.value(QStringLiteral("title"),tr("Digite um texto")).toString(),this);
        auto* prompt=new QLineEdit(p.value(QStringLiteral("prompt"),tr("Texto:")).toString(),this);
        auto maximum=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(p.value(QStringLiteral("maximumLength")),32),m_commonContext);
        auto* replace=new QCheckBox(tr("Começar com o texto atual preenchido"),this);replace->setChecked(p.value(QStringLiteral("replace"),true).toBool());
        auto* allowCancel=new QCheckBox(tr("Permitir cancelar"),this);allowCancel->setChecked(p.value(QStringLiteral("allowCancel"),true).toBool());
        form->addRow(tr("Título"),title);form->addRow(tr("Texto"),prompt);form->addRow(tr("Gravar em"),stringBox);form->addRow(tr("Máximo de caracteres"),maximum->widget);form->addRow(QString(),replace);form->addRow(QString(),allowCancel);
        auto* note=new QLabel(tr("A entrada aparece dentro do próprio jogo. O jogador pode editar o texto, apagar caracteres e confirmar quando terminar."),this);note->setWordWrap(true);note->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("stringId"),stringBox->currentData()},{QStringLiteral("title"),title->text().trimmed()},{QStringLiteral("prompt"),prompt->text().trimmed()},{QStringLiteral("maximumLength"),maximum->value()},{QStringLiteral("replace"),replace->isChecked()},{QStringLiteral("allowCancel"),allowCancel->isChecked()}};});
    } else if (tipo == QLatin1String("input.confirm")) {
        setWindowTitle(tr("Pedir confirmação"));
        auto* result = comboVariaveis(this, ed, p.value(QStringLiteral("resultVariable"), 1).toInt());
        auto* title = new QLineEdit(p.value(QStringLiteral("title"), tr("Confirmar")).toString(), this);
        auto* prompt = new QLineEdit(p.value(QStringLiteral("prompt"), tr("Continuar?")).toString(), this);
        auto* initial = new QComboBox(this);
        initial->addItem(tr("Sim"), true); initial->addItem(tr("Não"), false);
        initial->setCurrentIndex(p.value(QStringLiteral("defaultYes"), true).toBool() ? 0 : 1);
        auto* cancel = new QSpinBox(this); cancel->setRange(-999999, 999999);
        cancel->setValue(p.value(QStringLiteral("cancelValue"), 0).toInt());
        form->addRow(tr("Título"), title); form->addRow(tr("Pergunta"), prompt);
        form->addRow(tr("Opção inicial"), initial); form->addRow(tr("Gravar em"), result);
        form->addRow(tr("Valor ao cancelar"), cancel);
        auto* note = new QLabel(tr("A resposta é guardada na variável escolhida: Sim grava 1 e Não grava 0."), this);
        note->setWordWrap(true); note->setStyleSheet(QStringLiteral("color:#999;font-size:11px")); v->addWidget(note);
        connect(this, &QDialog::accepted, this, [this,result,title,prompt,initial,cancel] {
            m_cmd.params = {{QStringLiteral("resultVariable"), result->currentData()},
                            {QStringLiteral("title"), title->text().trimmed()},
                            {QStringLiteral("prompt"), prompt->text().trimmed()},
                            {QStringLiteral("defaultYes"), initial->currentData().toBool()},
                            {QStringLiteral("cancelValue"), cancel->value()}};
        });
    } else if (tipo == QLatin1String("input.item")) {
        setWindowTitle(tr("Selecionar item"));
        auto* variable = comboVariaveis(this, ed, p.value(QStringLiteral("variableId"), 1).toInt());
        auto* title = new QLineEdit(p.value(QStringLiteral("title"), tr("Selecionar item")).toString(), this);
        auto* prompt = new QLineEdit(p.value(QStringLiteral("prompt"), tr("Escolha um item:")).toString(), this);
        auto* category = new QComboBox(this);
        category->addItem(tr("Itens"), QStringLiteral("items"));
        category->addItem(tr("Armas"), QStringLiteral("weapons"));
        category->addItem(tr("Armaduras"), QStringLiteral("armors"));
        category->setCurrentIndex(qMax(0, category->findData(p.value(QStringLiteral("category"), QStringLiteral("items")))));
        form->addRow(tr("Título"), title); form->addRow(tr("Texto"), prompt);
        form->addRow(tr("Categoria"), category); form->addRow(tr("Guardar item escolhido em"), variable);
        auto* note = new QLabel(tr("Mostra apenas o que o grupo possui. Se o jogador cancelar, será guardado 0; ao escolher, será guardado o número do item."), this);
        note->setWordWrap(true); note->setStyleSheet(QStringLiteral("color:#999;font-size:11px")); v->addWidget(note);
        connect(this, &QDialog::accepted, this, [this,variable,title,prompt,category] {
            m_cmd.params = {{QStringLiteral("variableId"), variable->currentData()},
                            {QStringLiteral("title"), title->text().trimmed()},
                            {QStringLiteral("prompt"), prompt->text().trimmed()},
                            {QStringLiteral("category"), category->currentData()}};
        });
    } else if (tipo == QLatin1String("choice.show")) {
        setWindowTitle(tr("Mostrar escolhas"));
        resize(720, 720);
        auto* choices = new QPlainTextEdit(this);
        choices->setMaximumHeight(125);
        choices->setPlaceholderText(tr("Sim\nNão"));
        QStringList currentChoices = p.value(QStringLiteral("choices")).toStringList();
        if (currentChoices.isEmpty()) for (const QVariant& value : p.value(QStringLiteral("choices")).toList()) currentChoices.push_back(value.toString());
        if (currentChoices.isEmpty()) currentChoices = {tr("Sim"), tr("Não")};
        choices->setPlainText(currentChoices.join(QLatin1Char('\n')));
        auto* choiceKeys = new QPlainTextEdit(this);
        choiceKeys->setMaximumHeight(95);
        choiceKeys->setPlaceholderText(tr("menu.choice.yes\nmenu.choice.no\n(opcional; uma chave por opção)"));
        QStringList currentChoiceKeys = p.value(QStringLiteral("choiceLocalizationKeys")).toStringList();
        while (currentChoiceKeys.size() < currentChoices.size()) currentChoiceKeys.push_back(QString());
        choiceKeys->setPlainText(currentChoiceKeys.join(QLatin1Char('\n')));

        auto* result = comboVariaveis(this, ed, p.value(QStringLiteral("resultVariable"), 1).toInt());
        auto* cancel = new QComboBox(this); cancel->addItem(tr("Cancelar grava 0"), 0); cancel->addItem(tr("Não permitir cancelar"), -1);
        cancel->setCurrentIndex(qMax(0, cancel->findData(p.value(QStringLiteral("cancelValue"), 0))));
        auto* initial = new QSpinBox(this); initial->setRange(1, 8); initial->setValue(p.value(QStringLiteral("default"), 0).toInt() + 1);
        auto* position = new QComboBox(this);
        for(const auto& item : QList<QPair<QString,QString>>{{tr("Superior esquerdo"),"top-left"},{tr("Superior centro"),"top-center"},{tr("Superior direito"),"top-right"},{tr("Centro esquerdo"),"center-left"},{tr("Centro"),"center"},{tr("Centro direito"),"center-right"},{tr("Inferior esquerdo"),"bottom-left"},{tr("Inferior centro"),"bottom-center"},{tr("Inferior direito"),"bottom-right"}}) position->addItem(item.first,item.second);
        position->setCurrentIndex(qMax(0,position->findData(p.value(QStringLiteral("position"),QStringLiteral("center-right")))));
        auto* offsetX=new QSpinBox(this);auto* offsetY=new QSpinBox(this);for(auto* spin:{offsetX,offsetY})spin->setRange(-10000,10000);
        offsetX->setValue(p.value(QStringLiteral("offsetX"),0).toInt());offsetY->setValue(p.value(QStringLiteral("offsetY"),0).toInt());
        auto* offsetRow=new QWidget(this);auto* offsetLayout=new QHBoxLayout(offsetRow);offsetLayout->setContentsMargins(0,0,0,0);offsetLayout->addWidget(new QLabel(tr("X:"),offsetRow));offsetLayout->addWidget(offsetX);offsetLayout->addWidget(new QLabel(tr("Y:"),offsetRow));offsetLayout->addWidget(offsetY);

        auto* layoutMode=new QComboBox(this);layoutMode->addItem(tr("Vertical"),QStringLiteral("vertical"));layoutMode->addItem(tr("Horizontal"),QStringLiteral("horizontal"));layoutMode->addItem(tr("Grid"),QStringLiteral("grid"));layoutMode->setCurrentIndex(qMax(0,layoutMode->findData(p.value(QStringLiteral("layout"),QStringLiteral("vertical")))));
        auto* columns=new QSpinBox(this);columns->setRange(1,8);columns->setValue(qBound(1,p.value(QStringLiteral("columns"),1).toInt(),8));
        auto* spacingRow=new QWidget(this);auto* spacingLayout=new QHBoxLayout(spacingRow);spacingLayout->setContentsMargins(0,0,0,0);auto* spacingX=new QSpinBox(spacingRow);auto* spacingY=new QSpinBox(spacingRow);for(auto* spin:{spacingX,spacingY}){spin->setRange(0,128);spin->setSuffix(tr(" px"));}spacingX->setValue(qBound(0,p.value(QStringLiteral("spacingX"),6).toInt(),128));spacingY->setValue(qBound(0,p.value(QStringLiteral("spacingY"),4).toInt(),128));spacingLayout->addWidget(new QLabel(tr("Horizontal:"),spacingRow));spacingLayout->addWidget(spacingX);spacingLayout->addWidget(new QLabel(tr("Vertical:"),spacingRow));spacingLayout->addWidget(spacingY);
        auto* alignment=new QComboBox(this);alignment->addItem(tr("Esquerda"),QStringLiteral("left"));alignment->addItem(tr("Centro"),QStringLiteral("center"));alignment->addItem(tr("Direita"),QStringLiteral("right"));alignment->setCurrentIndex(qMax(0,alignment->findData(p.value(QStringLiteral("alignment"),QStringLiteral("left")))));
        auto* boxMode=new QComboBox(this);boxMode->addItem(tr("Usar aparência do tema"),QStringLiteral("theme"));boxMode->addItem(tr("Transparente"),QStringLiteral("transparent"));boxMode->setCurrentIndex(qMax(0,boxMode->findData(p.value(QStringLiteral("boxMode"),QStringLiteral("theme")))));
        auto* choiceFont=new QComboBox(this);choiceFont->addItem(tr("Usar aparência do tema"),QString());for(const core::ProjectFont& pf:ed.projectFonts){if(!pf.sourcePath.isEmpty())QFontDatabase::addApplicationFont(QDir(ed.projectRoot()).filePath(pf.sourcePath));if(choiceFont->findData(pf.family)<0)choiceFont->addItem(tr("Projeto — %1").arg(pf.family),pf.family);}for(const QString& family:QFontDatabase::families())if(choiceFont->findData(family)<0)choiceFont->addItem(family,family);choiceFont->setCurrentIndex(qMax(0,choiceFont->findData(p.value(QStringLiteral("fontFamily")).toString())));
        auto* choiceFontSize=new QSpinBox(this);choiceFontSize->setRange(0,96);choiceFontSize->setSpecialValueText(tr("Herdar"));choiceFontSize->setValue(qBound(0,p.value(QStringLiteral("fontSize"),0).toInt(),96));choiceFontSize->setSuffix(tr(" px"));
        auto* disabledList=new QListWidget(this);disabledList->setMaximumHeight(115);disabledList->setToolTip(tr("Marque as opções que devem continuar visíveis, mas não podem ser selecionadas."));
        QVector<bool> initialDisabled;for(const QVariant& value:p.value(QStringLiteral("disabledChoices")).toList())initialDisabled.push_back(value.toBool());
        for(int i=0;i<currentChoices.size();++i){auto* item=new QListWidgetItem(currentChoices.at(i),disabledList);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(initialDisabled.value(i,false)?Qt::Checked:Qt::Unchecked);}
        const auto rebuildDisabled=[choices,disabledList]{QVector<bool> old;for(int i=0;i<disabledList->count();++i)old.push_back(disabledList->item(i)->checkState()==Qt::Checked);QStringList values;for(const QString&line:choices->toPlainText().split(QLatin1Char('\n'))){const QString value=line.trimmed();if(!value.isEmpty())values.push_back(value);if(values.size()==8)break;}QSignalBlocker blocker(disabledList);disabledList->clear();for(int i=0;i<values.size();++i){auto* item=new QListWidgetItem(values.at(i),disabledList);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(old.value(i,false)?Qt::Checked:Qt::Unchecked);}};
        connect(choices,&QPlainTextEdit::textChanged,this,[rebuildDisabled]{rebuildDisabled();});
        const auto syncColumns=[layoutMode,columns]{const QString mode=layoutMode->currentData().toString();columns->setEnabled(mode==QLatin1String("grid"));if(mode==QLatin1String("horizontal"))columns->setToolTip(QObject::tr("No layout horizontal, cada opção ocupa uma coluna."));else columns->setToolTip(QString());};syncColumns();connect(layoutMode,&QComboBox::currentIndexChanged,this,[syncColumns](int){syncColumns();});

        auto* timeLimit=new QDoubleSpinBox(this);timeLimit->setRange(0.0,86400.0);timeLimit->setDecimals(1);timeLimit->setSuffix(tr(" s"));timeLimit->setSpecialValueText(tr("Sem limite"));timeLimit->setValue(qBound(0.0,p.value(QStringLiteral("timeLimit"),0.0).toDouble(),86400.0));
        auto* timedDefault=new QComboBox(this);timedDefault->addItem(tr("Cancelar escolha"),-1);for(int i=0;i<currentChoices.size();++i)timedDefault->addItem(currentChoices.at(i),i);timedDefault->setCurrentIndex(qMax(0,timedDefault->findData(p.value(QStringLiteral("defaultChoice"),-1))));
        auto* showDisabledReason=new QCheckBox(tr("Mostrar motivo junto da opção bloqueada"),this);showDisabledReason->setChecked(p.value(QStringLiteral("showDisabledReason"),true).toBool());

        form->addRow(tr("Opções (uma por linha)"), choices); form->addRow(tr("Opções bloqueadas"),disabledList);form->addRow(QString(),showDisabledReason);form->addRow(tr("Chaves de localização (opcional)"), choiceKeys); form->addRow(tr("Gravar resposta em"), result); form->addRow(tr("Ao cancelar"), cancel); form->addRow(tr("Opção inicial"), initial);form->addRow(tr("Tempo para escolher"),timeLimit);form->addRow(tr("Ao esgotar o tempo"),timedDefault);form->addRow(tr("Layout"),layoutMode);form->addRow(tr("Colunas da grade"),columns);form->addRow(tr("Espaçamento"),spacingRow);form->addRow(tr("Alinhamento"),alignment);form->addRow(tr("Caixa"),boxMode);form->addRow(tr("Fonte"),choiceFont);form->addRow(tr("Tamanho da fonte"),choiceFontSize);form->addRow(tr("Posição"),position);form->addRow(tr("Ajuste de posição"),offsetRow);

        // Regra de UX da LUDO: quando um comando possui preview, a configuração
        // fica à esquerda e a pré-visualização permanece sempre à direita.
        v->removeItem(form);
        auto* body = new QHBoxLayout;
        auto* leftContainer = new QWidget(this);
        auto* left = new QVBoxLayout(leftContainer);
        left->setContentsMargins(0,0,6,0);
        left->addLayout(form);

        auto branches=std::make_shared<QVector<QVector<core::EventCommand>>>();
        const QVariantList branchValues=p.value(QStringLiteral("branches")).toList();
        for(int i=0;i<currentChoices.size();++i){QVector<core::EventCommand> branch;if(i<branchValues.size())branch=core::eventCommandsFromVariantList(branchValues.at(i).toList());branches->push_back(branch);}
        auto* branchList=new QListWidget(this);branchList->setMaximumHeight(150);
        auto* editBranch=new QPushButton(tr("Editar comandos da escolha…"),this);
        auto* branchGroup=new QGroupBox(tr("Comandos dentro de cada escolha"),this);auto* branchLayout=new QVBoxLayout(branchGroup);branchLayout->addWidget(branchList);branchLayout->addWidget(editBranch);left->addWidget(branchGroup);
        const auto parsedChoices=[choices]{QStringList values;for(const QString&line:choices->toPlainText().split(QLatin1Char('\n'))){const QString value=line.trimmed();if(!value.isEmpty())values.push_back(value);if(values.size()==8)break;}return values;};
        auto* textEffects=new TextEffectsEditorWidget(
            ed,core::TextEffectStack::fromVariantMap(p.value(QStringLiteral("textEffects")).toMap()),
            core::TextGradientSpec::fromVariantMap(p.value(QStringLiteral("textGradient")).toMap()),this);
        textEffects->setSampleText(parsedChoices().join(QStringLiteral("   ")));
        left->addWidget(textEffects);
        const auto rebuildBranches=[branchList,branches,parsedChoices]{const QStringList values=parsedChoices();while(branches->size()<values.size())branches->push_back({});while(branches->size()>values.size())branches->removeLast();const int old=branchList->currentRow();QSignalBlocker blocker(branchList);branchList->clear();for(int i=0;i<values.size();++i){const int count=branches->at(i).size();branchList->addItem(count==1?QObject::tr("%1  —  1 comando").arg(values.at(i)):QObject::tr("%1  —  %2 comandos").arg(values.at(i)).arg(count));}if(branchList->count()>0)branchList->setCurrentRow(qBound(0,old<0?0:old,branchList->count()-1));};
        rebuildBranches();connect(choices,&QPlainTextEdit::textChanged,this,[rebuildBranches]{rebuildBranches();});
        connect(editBranch,&QPushButton::clicked,this,[this,branchList,branches,rebuildBranches]{const int row=branchList->currentRow();if(row<0||row>=branches->size())return;QDialog d(this);d.setWindowTitle(tr("Comandos da escolha"));d.resize(720,560);auto* lay=new QVBoxLayout(&d);auto* editor=new CommandListWidget(ed,(*branches)[row],&d);lay->addWidget(editor,1);auto* box=new QDialogButtonBox(QDialogButtonBox::Close,&d);lay->addWidget(box);connect(box,&QDialogButtonBox::rejected,&d,&QDialog::accept);connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);d.exec();rebuildBranches();});

        auto conditions=std::make_shared<QVector<QVariantMap>>();const QVariantList savedConditions=p.value(QStringLiteral("conditions")).toList();for(int i=0;i<currentChoices.size();++i)conditions->push_back(i<savedConditions.size()?savedConditions.at(i).toMap():QVariantMap());
        auto* conditionList=new QListWidget(this);conditionList->setMaximumHeight(150);auto* editCondition=new QPushButton(tr("Editar condição e motivo…"),this);auto* conditionGroup=new QGroupBox(tr("Disponibilidade dinâmica por escolha"),this);auto* conditionLayout=new QVBoxLayout(conditionGroup);conditionLayout->addWidget(conditionList);conditionLayout->addWidget(editCondition);left->addWidget(conditionGroup);
        const auto rebuildConditions=[conditionList,conditions,parsedChoices]{const QStringList values=parsedChoices();while(conditions->size()<values.size())conditions->push_back({});while(conditions->size()>values.size())conditions->removeLast();const int old=conditionList->currentRow();QSignalBlocker blocker(conditionList);conditionList->clear();for(int i=0;i<values.size();++i){const QVariantMap spec=conditions->at(i);const bool active=!spec.value(QStringLiteral("conditionTree"),spec.value(QStringLiteral("condition"))).toMap().isEmpty();const QString reason=spec.value(QStringLiteral("disabledReason")).toString().trimmed();conditionList->addItem(QObject::tr("%1  —  %2%3").arg(values.at(i),active?QObject::tr("condição ativa"):QObject::tr("sempre disponível"),reason.isEmpty()?QString():QObject::tr(" · %1").arg(reason)));}if(conditionList->count()>0)conditionList->setCurrentRow(qBound(0,old<0?0:old,conditionList->count()-1));};
        rebuildConditions();connect(choices,&QPlainTextEdit::textChanged,this,[=]{const QStringList values=parsedChoices();const int previous=timedDefault->currentData().toInt();QSignalBlocker blocker(timedDefault);timedDefault->clear();timedDefault->addItem(QObject::tr("Cancelar escolha"),-1);for(int i=0;i<values.size();++i)timedDefault->addItem(values.at(i),i);timedDefault->setCurrentIndex(qMax(0,timedDefault->findData(previous)));rebuildConditions();});
connect(editCondition,&QPushButton::clicked,this,[=]{const int row=conditionList->currentRow();if(row<0||row>=conditions->size())return;QDialog d(this);d.setWindowTitle(tr("Condição da escolha"));d.resize(760,650);auto* lay=new QVBoxLayout(&d);const QVariantMap old=conditions->at(row);const QVariantMap oldTree=old.value(QStringLiteral("conditionTree"),old.value(QStringLiteral("condition"))).toMap();auto* enabled=new QCheckBox(tr("Disponibilidade depende desta condição"),&d);enabled->setChecked(true);lay->addWidget(enabled);QVariantMap source;source.insert(QStringLiteral("conditionTree"),oldTree);auto* tree=new ConditionTreeEditorWidget(ed,source,m_commonContext,&d);connect(enabled,&QCheckBox::toggled,tree,&QWidget::setEnabled);lay->addWidget(tree,1);auto* form2=new QFormLayout;auto* label=new QLineEdit(old.value(QStringLiteral("disabledLabel")).toString(),&d);auto* reason=new QLineEdit(old.value(QStringLiteral("disabledReason")).toString(),&d);form2->addRow(tr("Texto enquanto bloqueada"),label);form2->addRow(tr("Motivo visível"),reason);lay->addLayout(form2);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);lay->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return;if(enabled->isChecked())(*conditions)[row]={{QStringLiteral("conditionTree"),tree->value()},{QStringLiteral("disabledLabel"),label->text().trimmed()},{QStringLiteral("disabledReason"),reason->text().trimmed()}};else (*conditions)[row]=QVariantMap();rebuildConditions();});

        auto* note = new QLabel(tr("Cada opção pode ter seus próprios comandos. Se quiser, você também pode guardar a escolha em uma variável para usar depois."), this);note->setWordWrap(true);note->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));left->addWidget(note);left->addStretch(1);

        auto* preview=new ChoiceCommandPreview(ed,this);
        auto* previewDialog = new CommandPreviewDialog(tr("Prévia das escolhas"), preview, this);
        auto* previewButton = new QPushButton(tr("Ver prévia"), leftContainer);
        previewButton->setToolTip(tr("Abre uma prévia em uma janela separada."));
        left->addWidget(previewButton);
        connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);
        auto* leftScroll = new QScrollArea(this);
        leftScroll->setWidgetResizable(true);
        leftScroll->setFrameShape(QFrame::NoFrame);
        leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        leftScroll->setWidget(leftContainer);
        body->addWidget(leftScroll,1);
        v->insertLayout(0,body,1);
        resize(760,720);

        const auto refreshPreview=[preview,parsedChoices,position,offsetX,offsetY,textEffects,disabledList,layoutMode,columns,spacingX,spacingY,alignment,boxMode,choiceFont,choiceFontSize]{
            textEffects->setSampleText(parsedChoices().join(QStringLiteral("   ")));QVector<bool> disabled;for(int i=0;i<disabledList->count();++i)disabled.push_back(disabledList->item(i)->checkState()==Qt::Checked);
            preview->setData(parsedChoices(),position->currentData().toString(),offsetX->value(),offsetY->value(),textEffects->effects(),textEffects->gradient(),disabled,layoutMode->currentData().toString(),columns->value(),spacingX->value(),spacingY->value(),alignment->currentData().toString(),boxMode->currentData().toString(),choiceFont->currentData().toString(),choiceFontSize->value());
        };
        connect(choices,&QPlainTextEdit::textChanged,this,[refreshPreview]{refreshPreview();});connect(position,&QComboBox::currentIndexChanged,this,[refreshPreview](int){refreshPreview();});connect(offsetX,&QSpinBox::valueChanged,this,[refreshPreview](int){refreshPreview();});connect(offsetY,&QSpinBox::valueChanged,this,[refreshPreview](int){refreshPreview();});connect(textEffects,&TextEffectsEditorWidget::changed,this,[refreshPreview]{refreshPreview();});connect(disabledList,&QListWidget::itemChanged,this,[refreshPreview](QListWidgetItem*){refreshPreview();});for(QComboBox* combo:{layoutMode,alignment,boxMode,choiceFont})connect(combo,&QComboBox::currentIndexChanged,this,[refreshPreview](int){refreshPreview();});for(QSpinBox* spin:{columns,spacingX,spacingY,choiceFontSize})connect(spin,&QSpinBox::valueChanged,this,[refreshPreview](int){refreshPreview();});refreshPreview();
        connect(this, &QDialog::accepted, this, [=] {
            const QStringList values=parsedChoices();rebuildBranches();QVariantList branchOut;for(const auto&branch:*branches)branchOut.push_back(core::eventCommandsToVariantList(branch));
            QStringList localizedKeys; const QStringList rawKeys=choiceKeys->toPlainText().split(QLatin1Char('\n')); for(int i=0;i<values.size();++i)localizedKeys.push_back(core::normalizeLocalizationKey(rawKeys.value(i)));
            QVariantList disabledOut;for(int i=0;i<values.size();++i)disabledOut.push_back(i<disabledList->count()&&disabledList->item(i)->checkState()==Qt::Checked);
            rebuildConditions();QVariantList conditionOut;for(const QVariantMap& spec:*conditions)conditionOut.push_back(spec);
            QVariantMap params={{QStringLiteral("choices"), values},{QStringLiteral("disabledChoices"),disabledOut},{QStringLiteral("conditions"),conditionOut},{QStringLiteral("showDisabledReason"),showDisabledReason->isChecked()},{QStringLiteral("timeLimit"),timeLimit->value()},{QStringLiteral("defaultChoice"),timedDefault->currentData()},{QStringLiteral("choiceLocalizationKeys"), localizedKeys},{QStringLiteral("resultVariable"), result->currentData()},{QStringLiteral("cancelValue"), cancel->currentData()},{QStringLiteral("default"), qMax(0, initial->value() - 1)},{QStringLiteral("layout"),layoutMode->currentData()},{QStringLiteral("columns"),columns->value()},{QStringLiteral("spacingX"),spacingX->value()},{QStringLiteral("spacingY"),spacingY->value()},{QStringLiteral("alignment"),alignment->currentData()},{QStringLiteral("boxMode"),boxMode->currentData()},{QStringLiteral("fontFamily"),choiceFont->currentData()},{QStringLiteral("fontSize"),choiceFontSize->value()},{QStringLiteral("position"),position->currentData()},{QStringLiteral("offsetX"),offsetX->value()},{QStringLiteral("offsetY"),offsetY->value()},{QStringLiteral("branches"),branchOut}};
            const auto effects=textEffects->effects();const auto gradient=textEffects->gradient();
            if(effects.enabled())params[QStringLiteral("textEffects")]=effects.toVariantMap();
            if(gradient.enabled())params[QStringLiteral("textGradient")]=gradient.toVariantMap();
            m_cmd.params=params;
        });
    } else if (tipo == QLatin1String("if") || tipo == QLatin1String("wait.until")) {
        const bool waitUntil = tipo == QLatin1String("wait.until");
        setWindowTitle(waitUntil ? tr("Esperar até…") : tr("Se…"));resize(760,waitUntil?620:560);v->removeItem(form);delete form;
        QTabWidget* conditionMode=nullptr;ConditionTreeEditorWidget* treeEditor=nullptr;QTabWidget* tabs=nullptr;
        if(allowConditionTree){
            conditionMode=new QTabWidget(this);auto* simpleHost=new QWidget(conditionMode);auto* simpleLayout=new QVBoxLayout(simpleHost);simpleLayout->setContentsMargins(0,0,0,0);tabs=new QTabWidget(simpleHost);simpleLayout->addWidget(tabs);
            conditionMode->addTab(simpleHost,tr("Condição simples"));treeEditor=new ConditionTreeEditorWidget(ed,p,m_commonContext,conditionMode);conditionMode->addTab(treeEditor,tr("Combinar condições"));v->insertWidget(0,conditionMode,1);
            if(!p.value(QStringLiteral("conditionTree")).toMap().isEmpty())conditionMode->setCurrentIndex(1);
        }else{tabs=new QTabWidget(this);v->insertWidget(0,tabs,1);}
        QVariantMap simpleParams=p;if(!p.value(QStringLiteral("conditionTree")).toMap().isEmpty()){const auto leaves=core::conditionTreePredicates(p.value(QStringLiteral("conditionTree")).toMap());if(!leaves.isEmpty())simpleParams=leaves.first();}
        const QString oldKind=simpleParams.value("kind","switch").toString();
        auto makePage=[&](const QStringList&labels,const QStringList&ids,QComboBox**kindOut,QFormLayout**formOut){auto*w=new QWidget(tabs);auto*f=new QFormLayout(w);auto*k=new QComboBox(w);for(int i=0;i<labels.size();++i)k->addItem(labels[i],ids[i]);f->addRow(tr("Verificar:"),k);*kindOut=k;*formOut=f;return w;};
        QComboBox *logicKind,*actorKind,*mapKind,*dataKind;QFormLayout *lf,*af,*mf,*df;
        QStringList logicLabels={tr("Interruptor"),tr("Interruptor próprio"),tr("Variável"),tr("String"),tr("Probabilidade")};QStringList logicIds={"switch","selfSwitch","variable","string","random"};
        if(m_commonContext&&(!m_commonContext->parameters.isEmpty()||!m_commonContext->locals.isEmpty())){logicLabels<<tr("Parâmetro/local do Evento Comum");logicIds<<QStringLiteral("commonValue");}
        auto* logic=makePage(logicLabels,logicIds,&logicKind,&lf);
        auto* sw=comboInterruptores(logic,ed,simpleParams.value("id",1).toInt());auto* letter=new QComboBox(logic);letter->addItems({"A","B","C","D"});letter->setCurrentText(simpleParams.value("letter","A").toString());auto* state=new QComboBox(logic);state->addItem(tr("Ligado"),true);state->addItem(tr("Desligado"),false);state->setCurrentIndex(simpleParams.value("value",true).toBool()?0:1);auto* var=comboVariaveis(logic,ed,simpleParams.value("id",1).toInt());auto* op=new QComboBox(logic);for(const auto&cmp:QVector<QPair<QString,QString>>{{tr("maior ou igual a"),">="},{tr("menor ou igual a"),"<="},{tr("igual a"),"=="},{tr("diferente de"),"!="},{tr("maior que"),">"},{tr("menor que"),"<"}})op->addItem(cmp.first,cmp.second);op->setCurrentIndex(qMax(0,op->findData(simpleParams.value("op",">="))));QVariantMap variableRight=simpleParams.value(QStringLiteral("rightSpec")).toMap();if(variableRight.isEmpty())variableRight=core::valueSpecFromLegacy(simpleParams.value(QStringLiteral("value")),0);auto variableValue=makeCommonSourceEditor(logic,ed,core::CommonValueType::Number,variableRight,m_commonContext);auto* stringVar=comboStrings(logic,ed,simpleParams.value("id",1).toInt());auto* stringOp=new QComboBox(logic);stringOp->addItem(tr("Igual"),"==");stringOp->addItem(tr("Diferente"),"!=");stringOp->addItem(tr("Contém"),"contains");stringOp->addItem(tr("Começa com"),"startsWith");stringOp->addItem(tr("Termina com"),"endsWith");stringOp->setCurrentIndex(qMax(0,stringOp->findData(simpleParams.value("op","=="))));QVariantMap stringRight=simpleParams.value(QStringLiteral("rightSpec")).toMap();if(stringRight.isEmpty())stringRight={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),simpleParams.value(QStringLiteral("value"))}};auto stringValue=makeCommonSourceEditor(logic,ed,core::CommonValueType::Text,stringRight,m_commonContext);auto* chance=new QSpinBox(logic);chance->setRange(0,100);chance->setSuffix("%");chance->setValue(simpleParams.value("chance",50).toInt());auto* commonCond=new QComboBox(logic);
        if(m_commonContext){
            for(const auto& def:m_commonContext->parameters) commonCond->addItem(tr("Parâmetro: %1 — %2").arg(def.name,core::commonValueTypeLabel(def.type)),def.id);
            for(const auto& def:m_commonContext->locals) commonCond->addItem(tr("Local: %1 — %2").arg(def.name,core::commonValueTypeLabel(def.type)),def.id);
        }
        if(commonCond->count()==0)commonCond->addItem(tr("(nenhum)"),QString());
        commonCond->setCurrentIndex(qMax(0,commonCond->findData(simpleParams.value("id").toString())));
        auto* commonOp=new QComboBox(logic);
        auto* commonCompareHost=new QWidget(logic);auto* commonCompareLayout=new QVBoxLayout(commonCompareHost);commonCompareLayout->setContentsMargins(0,0,0,0);
        auto commonCompareEditor=std::make_shared<std::shared_ptr<CommonSourceEditorState>>();
        QVariantMap commonSavedRight=simpleParams.value(QStringLiteral("rightSpec")).toMap();
        if(commonSavedRight.isEmpty())commonSavedRight=core::valueSpecFromLegacy(simpleParams.value(QStringLiteral("value")),simpleParams.value(QStringLiteral("value")));
        auto rebuildCommonCondition=[=]{
            core::CommonValueType type=core::CommonValueType::Number;commonUiValueType(m_commonContext,commonCond->currentData().toString(),&type);
            const QString old=commonOp->count()?commonOp->currentData().toString():simpleParams.value("op","==").toString();commonOp->clear();
            if(type==core::CommonValueType::Number)for(const auto&cmp:QVector<QPair<QString,QString>>{{tr("igual a"),QStringLiteral("==")},{tr("diferente de"),QStringLiteral("!=")},{tr("maior ou igual a"),QStringLiteral(">=")},{tr("menor ou igual a"),QStringLiteral("<=")},{tr("maior que"),QStringLiteral(">")},{tr("menor que"),QStringLiteral("<")}})commonOp->addItem(cmp.first,cmp.second);
            else if(type==core::CommonValueType::Boolean){commonOp->addItem(QStringLiteral("=="),QStringLiteral("=="));commonOp->addItem(QStringLiteral("!="),QStringLiteral("!="));}
            else{commonOp->addItem(tr("Igual"),QStringLiteral("=="));commonOp->addItem(tr("Diferente"),QStringLiteral("!="));commonOp->addItem(tr("Contém"),QStringLiteral("contains"));commonOp->addItem(tr("Começa com"),QStringLiteral("startsWith"));commonOp->addItem(tr("Termina com"),QStringLiteral("endsWith"));}
            int oi=commonOp->findData(old);if(oi<0)oi=0;commonOp->setCurrentIndex(oi);
            QVariantMap previous=*commonCompareEditor?(*commonCompareEditor)->value():commonSavedRight;
            if(*commonCompareEditor){commonCompareLayout->removeWidget((*commonCompareEditor)->widget);(*commonCompareEditor)->widget->deleteLater();}
            *commonCompareEditor=makeCommonSourceEditor(commonCompareHost,ed,type,previous,m_commonContext);commonCompareLayout->addWidget((*commonCompareEditor)->widget);
        };
        rebuildCommonCondition();connect(commonCond,&QComboBox::currentIndexChanged,this,[rebuildCommonCondition](int){rebuildCommonCondition();});
        lf->addRow(tr("Interruptor:"),sw);lf->addRow(tr("Qual interruptor:"),letter);lf->addRow(tr("Deve estar:"),state);lf->addRow(tr("Variável:"),var);lf->addRow(tr("Comparação:"),op);lf->addRow(tr("Comparar com:"),variableValue->widget);lf->addRow(tr("Texto global:"),stringVar);lf->addRow(tr("Comparação:"),stringOp);lf->addRow(tr("Comparar com:"),stringValue->widget);lf->addRow(tr("Chance de acontecer:"),chance);lf->addRow(tr("Parâmetro ou valor local:"),commonCond);lf->addRow(tr("Comparação:"),commonOp);lf->addRow(tr("Comparar com:"),commonCompareHost);tabs->addTab(logic,tr("1 · Lógica"));
        auto* actors=makePage({tr("Posição do jogador"),tr("Direção do jogador"),tr("Distância do jogador ao evento"),tr("Rota em execução")},{"playerPosition","direction","distance","routeRunning"},&actorKind,&af);auto*x=new QSpinBox(actors);auto*y=new QSpinBox(actors);x->setRange(-9999,9999);y->setRange(-9999,9999);x->setValue(simpleParams.value("x").toInt());y->setValue(simpleParams.value("y").toInt());auto*dir=new QComboBox(actors);dir->addItem(tr("Baixo"),0);dir->addItem(tr("Esquerda"),1);dir->addItem(tr("Direita"),2);dir->addItem(tr("Cima"),3);dir->setCurrentIndex(qMax(0,dir->findData(simpleParams.value("direction",0))));auto*ev=new QComboBox(actors);ev->addItem(tr("Este evento"),QString());for(const MapEvent&e:ed.events())ev->addItem(e.name,e.id);ev->setCurrentIndex(qMax(0,ev->findData(simpleParams.value("eventId"))));auto*dist=new QDoubleSpinBox(actors);dist->setRange(0,999);dist->setSuffix(tr(" tiles"));dist->setValue(simpleParams.value("distance",3).toDouble());auto*routeTarget=new QComboBox(actors);routeTarget->addItem(tr("Jogador"),"player");routeTarget->addItem(tr("Este evento"),"self");for(const MapEvent&e:ed.events())routeTarget->addItem(e.name,"event:"+e.id);routeTarget->setCurrentIndex(qMax(0,routeTarget->findData(simpleParams.value("target","player"))));af->addRow("X:",x);af->addRow("Y:",y);af->addRow(tr("Direção:"),dir);af->addRow(tr("Evento:"),ev);af->addRow(tr("Distância máxima:"),dist);af->addRow(tr("Alvo da rota:"),routeTarget);tabs->addTab(actors,tr("2 · Jogador/Eventos"));
        auto* map=makePage({tr("Mapa atual"),tr("Botão/ação pressionado")},{"map","button"},&mapKind,&mf);auto*mapCombo=new QComboBox(map);for(const MapDoc&d:ed.docs)mapCombo->addItem(d.name,d.id);mapCombo->setCurrentIndex(qMax(0,mapCombo->findData(simpleParams.value("mapId"))));auto*action=new QComboBox(map);for(GameAction a:allGameActions())action->addItem(gameActionLabel(a),gameActionId(a));action->setCurrentIndex(qMax(0,action->findData(simpleParams.value("action","confirm"))));auto*actionState=new QComboBox(map);actionState->addItem(tr("Está pressionada"),QStringLiteral("held"));actionState->addItem(tr("Acabou de pressionar"),QStringLiteral("pressed"));actionState->addItem(tr("Acabou de soltar"),QStringLiteral("released"));actionState->setCurrentIndex(qMax(0,actionState->findData(simpleParams.value("state",QStringLiteral("held")))));mf->addRow(tr("Mapa:"),mapCombo);mf->addRow(tr("Ação:"),action);mf->addRow(tr("Estado da ação:"),actionState);tabs->addTab(map,tr("3 · Mapa/Entrada"));
        auto* data=makePage({tr("Ouro"),tr("Item no inventário")},{"gold","item"},&dataKind,&df);auto*goldOp=new QComboBox(data);for(const QString&o:{">=","<=","==","!=",">","<"})goldOp->addItem(o,o);goldOp->setCurrentIndex(qMax(0,goldOp->findData(simpleParams.value("op",">="))));QVariantMap goldRight=simpleParams.value(QStringLiteral("rightSpec")).toMap();if(goldRight.isEmpty())goldRight=core::valueSpecFromLegacy(simpleParams.value(QStringLiteral("value")),0);auto gold=makeCommonSourceEditor(data,ed,core::CommonValueType::Number,goldRight,m_commonContext);auto*itemId=new QLineEdit(simpleParams.value("itemId").toString(),data);QVariantMap itemAmount=simpleParams.value(QStringLiteral("amountSpec")).toMap();if(itemAmount.isEmpty())itemAmount=core::valueSpecFromLegacy(simpleParams.value(QStringLiteral("amount")),1);auto amount=makeCommonSourceEditor(data,ed,core::CommonValueType::Number,itemAmount,m_commonContext);df->addRow(tr("Operador:"),goldOp);df->addRow(tr("Valor:"),gold->widget);df->addRow(tr("ID do item:"),itemId);df->addRow(tr("Quantidade:"),amount->widget);tabs->addTab(data,tr("4 · Dados"));
        QVector<QComboBox*>kinds={logicKind,actorKind,mapKind,dataKind};for(int ti=0;ti<kinds.size();++ti){int q=kinds[ti]->findData(oldKind);if(q>=0){tabs->setCurrentIndex(ti);kinds[ti]->setCurrentIndex(q);break;}}
        auto updateVisibility=[=]{QString k=kinds[tabs->currentIndex()]->currentData().toString();sw->setVisible(k=="switch");letter->setVisible(k=="selfSwitch");state->setVisible(k=="switch"||k=="selfSwitch");var->setVisible(k=="variable");op->setVisible(k=="variable");variableValue->widget->setVisible(k=="variable");stringVar->setVisible(k=="string");stringOp->setVisible(k=="string");stringValue->widget->setVisible(k=="string");chance->setVisible(k=="random");commonCond->setVisible(k=="commonValue");commonOp->setVisible(k=="commonValue");commonCompareHost->setVisible(k=="commonValue");x->setVisible(k=="playerPosition");y->setVisible(k=="playerPosition");dir->setVisible(k=="direction");ev->setVisible(k=="distance");dist->setVisible(k=="distance");routeTarget->setVisible(k=="routeRunning");mapCombo->setVisible(k=="map");action->setVisible(k=="button");actionState->setVisible(k=="button");goldOp->setVisible(k=="gold");gold->widget->setVisible(k=="gold");itemId->setVisible(k=="item");amount->widget->setVisible(k=="item");};for(QComboBox*k:kinds)connect(k,&QComboBox::currentIndexChanged,this,[=](int){updateVisibility();});connect(tabs,&QTabWidget::currentChanged,this,[=](int){updateVisibility();});updateVisibility();
        std::shared_ptr<CommonSourceEditorState> timeoutFrames;
        if(waitUntil){timeoutFrames=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(p.value(QStringLiteral("timeoutFrames")),0),m_commonContext);auto* timeoutRow=new QFormLayout;timeoutRow->addRow(tr("Parar de esperar após (0 = sem limite)"),timeoutFrames->widget);v->addLayout(timeoutRow);}
        auto* elseHint=new QLabel(waitUntil
            ? tr("A execução continua quando a condição for verdadeira. Use 0 no limite para esperar pelo tempo que for necessário.")
            : tr("Se a condição não for atendida, os comandos de <b>Senão</b> serão executados."),this);elseHint->setWordWrap(true);elseHint->setStyleSheet(QStringLiteral("color:#777;font-size:11px"));v->addWidget(elseHint);
        connect(this,&QDialog::accepted,this,[=]{
            if(conditionMode&&conditionMode->currentIndex()==1&&treeEditor){QVariantMap n{{QStringLiteral("conditionTree"),treeEditor->value()}};if(waitUntil)n[QStringLiteral("timeoutFrames")]=timeoutFrames?QVariant(timeoutFrames->value()):QVariant(0);else n[QStringLiteral("createElse")]=true;m_cmd.params=n;return;}
            QVariantMap n;const QString k=kinds[tabs->currentIndex()]->currentData().toString();n["kind"]=k;if(k=="switch"){n["id"]=sw->currentData();n["value"]=state->currentData();}else if(k=="selfSwitch"){n["letter"]=letter->currentText();n["value"]=state->currentData();}else if(k=="variable"){n["id"]=var->currentData();n["op"]=op->currentData();n["rightSpec"]=variableValue->value();}else if(k=="string"){n["id"]=stringVar->currentData();n["op"]=stringOp->currentData();n["rightSpec"]=stringValue->value();}else if(k=="commonValue"){n["id"]=commonCond->currentData();n["op"]=commonOp->currentData();if(*commonCompareEditor)n[QStringLiteral("rightSpec")]=(*commonCompareEditor)->value();}else if(k=="random")n["chance"]=chance->value();else if(k=="playerPosition"){n["x"]=x->value();n["y"]=y->value();n["opX"]="==";n["opY"]="==";}else if(k=="direction")n["direction"]=dir->currentData();else if(k=="distance"){n["eventId"]=ev->currentData();n["distance"]=dist->value();}else if(k=="routeRunning")n["target"]=routeTarget->currentData();else if(k=="map")n["mapId"]=mapCombo->currentData();else if(k=="button"){n["action"]=action->currentData();n["state"]=actionState->currentData();}else if(k=="gold"){n["op"]=goldOp->currentData();n["rightSpec"]=gold->value();}else if(k=="item"){n["itemId"]=itemId->text();n["amountSpec"]=amount->value();}if(waitUntil)n[QStringLiteral("timeoutFrames")]=timeoutFrames?QVariant(timeoutFrames->value()):QVariant(0);else n["createElse"]=true;m_cmd.params=n;});
    } else if (tipo == QLatin1String("label") || tipo == QLatin1String("jump")) {
        const bool ehRotulo = (tipo == QLatin1String("label"));
        setWindowTitle(ehRotulo ? tr("Rótulo") : tr("Pular para rótulo"));
        auto* nome = new QLineEdit(p.value(ehRotulo ? QStringLiteral("name")
                                                    : QStringLiteral("label")).toString(), this);
        form->addRow(tr("Nome do rótulo"), nome);
        connect(this, &QDialog::accepted, this, [this, nome, ehRotulo] {
            m_cmd.params = { { ehRotulo ? QStringLiteral("name") : QStringLiteral("label"),
                               nome->text().trimmed() } };
        });
    } else if (tipo == QLatin1String("wait")) {
        setWindowTitle(tr("Esperar"));
        auto duration=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(p.value(QStringLiteral("frames")),30),m_commonContext);
        form->addRow(tr("Esperar por (quadros)"), duration->widget);
        auto* note=new QLabel(tr("Você pode informar um número fixo ou usar um valor do jogo para decidir quanto tempo esperar."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this, &QDialog::accepted, this, [this, duration] {
            m_cmd.params = { { QStringLiteral("frames"), duration->value() } };
        });
    } else if (tipo == QLatin1String("repeat.begin")) {
        setWindowTitle(tr("Repetir várias vezes"));
        QVariantMap savedCount=p.value(QStringLiteral("countSpec")).toMap();
        if(savedCount.isEmpty()) savedCount={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),p.value(QStringLiteral("count"),1)}};
        auto count=makeCommonSourceEditor(this,ed,core::CommonValueType::Number,savedCount,m_commonContext);
        auto indexTarget=makeValueTargetEditor(this,ed,core::CommonValueType::Number,p.value(QStringLiteral("indexTarget")).toMap(),m_commonContext,true);
        form->addRow(tr("Quantas vezes"),count->widget);
        form->addRow(tr("Guardar número da repetição em"),indexTarget->widget);
        auto* note=new QLabel(tr("Os comandos dentro deste bloco serão executados na ordem, pela quantidade de vezes escolhida."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("countSpec"),count->value()},{QStringLiteral("indexTarget"),indexTarget->result()}};});
    } else if (tipo == QLatin1String("map.event.call")) {
        setWindowTitle(tr("Chamar evento do mapa"));
        auto* map=new QComboBox(this);
        for(const core::MapDoc& doc:ed.docs) map->addItem(doc.name.isEmpty()?doc.id:doc.name,doc.id);
        QString savedMap=p.value(QStringLiteral("mapId")).toString();if(savedMap.isEmpty()&&ed.doc())savedMap=ed.doc()->id;
        int mi=map->findData(savedMap);if(mi>=0)map->setCurrentIndex(mi);
        auto* event=new QComboBox(this);auto* page=new QComboBox(this);
        const QString savedEvent=p.value(QStringLiteral("eventId")).toString();const int savedPage=p.value(QStringLiteral("pageIndex"),-1).toInt();
        auto rebuildPages=[=]{QSignalBlocker b(page);page->clear();page->addItem(tr("Página ativa pelas condições"),-1);const core::MapDoc* doc=ed.mapById(map->currentData().toString());if(!doc)return;const QString eid=event->currentData().toString();for(const core::MapEvent& ev:doc->events)if(ev.id==eid){for(int i=0;i<ev.pages.size();++i)page->addItem(tr("Página %1").arg(i+1),i);break;}int pi=page->findData(savedPage);if(pi>=0)page->setCurrentIndex(pi);};
        auto rebuildEvents=[=]{QSignalBlocker b(event);event->clear();const core::MapDoc* doc=ed.mapById(map->currentData().toString());if(doc)for(const core::MapEvent& ev:doc->events)event->addItem(ev.name.isEmpty()?ev.id:ev.name,ev.id);int ei=event->findData(savedEvent);if(ei>=0)event->setCurrentIndex(ei);rebuildPages();};
        rebuildEvents();connect(map,&QComboBox::currentIndexChanged,this,[=](int){rebuildEvents();});connect(event,&QComboBox::currentIndexChanged,this,[=](int){rebuildPages();});
        form->addRow(tr("Mapa"),map);form->addRow(tr("Evento"),event);form->addRow(tr("Página"),page);
        auto* note=new QLabel(tr("O evento precisa estar no mapa que está sendo jogado. Para chamar um evento de outro mapa, mova o jogador para esse mapa primeiro."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("mapId"),map->currentData()},{QStringLiteral("eventId"),event->currentData()},{QStringLiteral("pageIndex"),page->currentData()}};});
    } else if (tipo == QLatin1String("common.reserve")) {
        setWindowTitle(tr("Agendar evento comum"));resize(700,520);
        auto* c=new QComboBox(this);for(const core::CommonEvent& ce:ed.commonEvents)c->addItem(QStringLiteral("%1 — %2").arg(ce.number).arg(ce.name),ce.id);if(ed.commonEvents.isEmpty())c->addItem(tr("(nenhum criado)"),QString());
        QString selected=p.value(QStringLiteral("commonId")).toString();if(selected.isEmpty())if(const core::CommonEvent* legacy=ed.commonEventByNumber(p.value(QStringLiteral("number")).toInt()))selected=legacy->id;int ci=c->findData(selected);if(ci>=0)c->setCurrentIndex(ci);
        auto* priority=new QSpinBox(this);priority->setRange(-1000,1000);priority->setValue(p.value(QStringLiteral("priority"),0).toInt());priority->setToolTip(tr("Eventos com prioridade maior são executados primeiro. Em caso de empate, vale a ordem em que foram agendados."));
        form->addRow(tr("Evento comum"),c);form->addRow(tr("Prioridade"),priority);
        auto* signatureBox=new QGroupBox(tr("Parâmetros"),this);auto* signatureForm=new QFormLayout(signatureBox);v->addWidget(signatureBox);
        auto editors=std::make_shared<QVector<QPair<QString,std::shared_ptr<CommonSourceEditorState>>>>();const QVariantMap oldArgs=p.value(QStringLiteral("arguments")).toMap();const QString oldId=p.value(QStringLiteral("commonId")).toString();const int oldNumber=p.value(QStringLiteral("number"),0).toInt();
        auto rebuild=[=]{while(QLayoutItem* item=signatureForm->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}editors->clear();const core::CommonEvent* ce=ed.commonEventById(c->currentData().toString());if(!ce){signatureForm->addRow(new QLabel(tr("Nenhum Evento Comum selecionado."),signatureBox));return;}const bool same=!oldId.isEmpty()?ce->id==oldId:ce->number==oldNumber;if(ce->parameters.isEmpty())signatureForm->addRow(new QLabel(tr("Este Evento Comum não possui parâmetros."),signatureBox));for(const core::CommonEventParameter& def:ce->parameters){QVariantMap current=same&&oldArgs.contains(def.id)?oldArgs.value(def.id).toMap():QVariantMap();if(def.required&&current.isEmpty())current={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),def.defaultValue}};auto editor=makeCommonSourceEditor(signatureBox,ed,def.type,current,m_commonContext,!def.required,def.defaultValue);signatureForm->addRow(def.name+(def.required?tr(" *"):QString())+QStringLiteral(":"),editor->widget);editors->push_back({def.id,editor});}};
        rebuild();connect(c,&QComboBox::currentIndexChanged,this,[=](int){rebuild();});
        auto* note=new QLabel(tr("O evento será colocado na fila e executado assim que chegar a vez dele, sem interromper o evento que já está em andamento. A fila é mantida ao salvar e carregar a partida."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{QVariantMap args;for(const auto& pair:*editors){const QVariantMap spec=pair.second->value();if(spec.value(QStringLiteral("source")).toString()!=QLatin1String("default"))args[pair.first]=spec;}const core::CommonEvent* ce=ed.commonEventById(c->currentData().toString());m_cmd.params={{QStringLiteral("commonId"),c->currentData()},{QStringLiteral("number"),ce?ce->number:0},{QStringLiteral("arguments"),args},{QStringLiteral("priority"),priority->value()}};});
    } else if (tipo == QLatin1String("flow.exit")) {
        setWindowTitle(tr("Encerrar ou retornar"));
        auto* scope=new QComboBox(this);scope->addItem(tr("Encerrar somente este bloco"),QStringLiteral("frame"));scope->addItem(tr("Encerrar este evento do mapa"),QStringLiteral("map"));scope->addItem(tr("Retornar deste Evento Comum"),QStringLiteral("common"));scope->addItem(tr("Encerrar toda a execução atual"),QStringLiteral("all"));int si=scope->findData(p.value(QStringLiteral("scope"),QStringLiteral("frame")));if(si>=0)scope->setCurrentIndex(si);form->addRow(tr("O que encerrar"),scope);
        std::shared_ptr<CommonSourceEditorState> source;
        if(m_commonContext&&m_commonContext->returnValue.enabled){source=makeCommonSourceEditor(this,ed,m_commonContext->returnValue.type,p.value(QStringLiteral("sourceSpec")).toMap(),m_commonContext,true,m_commonContext->returnValue.defaultValue);form->addRow(tr("Valor de retorno"),source->widget);auto sync=[=]{source->widget->setVisible(scope->currentData().toString()==QLatin1String("common"));};sync();connect(scope,&QComboBox::currentIndexChanged,this,[=](int){sync();});}
        auto* note=new QLabel(tr("Escolha até onde a execução deve parar. Ao retornar de um Evento Comum, você também pode devolver um valor para quem fez a chamada."),this);note->setWordWrap(true);v->addWidget(note);
        connect(this,&QDialog::accepted,this,[=]{m_cmd.params={{QStringLiteral("scope"),scope->currentData()}};if(source&&scope->currentData().toString()==QLatin1String("common")&&source->value().value(QStringLiteral("source")).toString()!=QLatin1String("default"))m_cmd.params[QStringLiteral("sourceSpec")]=source->value();});
    } else if (tipo == QLatin1String("common.call")) {
        setWindowTitle(tr("Chamar evento comum")); resize(700,540);
        auto* c = new QComboBox(this);
        for (const core::CommonEvent& ce : ed.commonEvents)
            c->addItem(QStringLiteral("%1 — %2").arg(ce.number).arg(ce.name), ce.id);
        if (ed.commonEvents.isEmpty()) c->addItem(tr("(nenhum criado)"), QString());
        const QString oldCommonId=p.value(QStringLiteral("commonId")).toString();
        const int oldNumber=p.value(QStringLiteral("number"),0).toInt();
        QString selectedCommonId=oldCommonId;
        if(selectedCommonId.isEmpty()) if(const core::CommonEvent* legacy=ed.commonEventByNumber(oldNumber)) selectedCommonId=legacy->id;
        c->setCurrentIndex(qMax(0,c->findData(selectedCommonId)));
        form->addRow(tr("Evento comum"),c);

        auto* signatureBox=new QGroupBox(tr("Parâmetros"),this); auto* signatureForm=new QFormLayout(signatureBox); v->addWidget(signatureBox);
        auto* returnBox=new QGroupBox(tr("Retorno"),this); auto* returnForm=new QFormLayout(returnBox); v->addWidget(returnBox);
        auto argumentEditors=std::make_shared<QVector<QPair<QString,std::shared_ptr<CommonSourceEditorState>>>>();
        auto returnKind=std::make_shared<QComboBox*>(nullptr);
        auto returnTarget=std::make_shared<QComboBox*>(nullptr);
        const QVariantMap oldArguments=p.value(QStringLiteral("arguments")).toMap();
        const QVariantMap oldReturn=p.value(QStringLiteral("returnTarget")).toMap();
        const auto clearForm=[](QFormLayout* layout){
            while(QLayoutItem* item=layout->takeAt(0)){ if(item->widget()) item->widget()->deleteLater(); delete item; }
        };
        const auto rebuild=[=,this]{
            *returnKind=nullptr; *returnTarget=nullptr;
            clearForm(signatureForm); clearForm(returnForm); argumentEditors->clear();
            const core::CommonEvent* ce=ed.commonEventById(c->currentData().toString());
            if(!ce){ signatureForm->addRow(new QLabel(tr("Nenhum Evento Comum selecionado."),signatureBox)); returnBox->setVisible(false); return; }
            if(ce->parameters.isEmpty()) signatureForm->addRow(new QLabel(tr("Este Evento Comum não possui parâmetros."),signatureBox));
            const bool sameCall = (!oldCommonId.isEmpty() ? ce->id == oldCommonId : ce->number == oldNumber);
            for(const core::CommonEventParameter& def:ce->parameters){
                QVariantMap current = sameCall && oldArguments.contains(def.id)
                    ? oldArguments.value(def.id).toMap() : QVariantMap();
                if(def.required && current.isEmpty())
                    current=QVariantMap{{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),def.defaultValue}};
                auto editor=makeCommonSourceEditor(signatureBox,ed,def.type,current,m_commonContext,!def.required,def.defaultValue);
                QString label=def.name; if(def.required) label+=tr(" *");
                signatureForm->addRow(label+QStringLiteral(":"),editor->widget);
                if(!def.description.isEmpty()) editor->widget->setToolTip(def.description);
                argumentEditors->push_back({def.id,editor});
            }

            returnBox->setVisible(ce->returnValue.enabled);
            if(!ce->returnValue.enabled) return;
            *returnKind=new QComboBox(returnBox);
            (*returnKind)->addItem(tr("Ignorar retorno"),QStringLiteral("none"));
            if(ce->returnValue.type==core::CommonValueType::Number)
                (*returnKind)->addItem(tr("Variável global"),QStringLiteral("variable"));
            else if(ce->returnValue.type==core::CommonValueType::Boolean)
                (*returnKind)->addItem(tr("Interruptor global"),QStringLiteral("switch"));
            else if(ce->returnValue.type==core::CommonValueType::Text)
                (*returnKind)->addItem(tr("Texto global"),QStringLiteral("string"));
            bool hasCompatibleLocal=false;
            if(m_commonContext) for(const core::CommonEventLocal& def:m_commonContext->locals)
                if(def.type==ce->returnValue.type){hasCompatibleLocal=true;break;}
            if(hasCompatibleLocal) (*returnKind)->addItem(tr("Variável local do chamador"),QStringLiteral("commonValue"));
            const QString oldKind=sameCall?oldReturn.value(QStringLiteral("target"),QStringLiteral("none")).toString():QStringLiteral("none");
            int ki=(*returnKind)->findData(oldKind); if(ki<0)ki=0; (*returnKind)->setCurrentIndex(ki);
            *returnTarget=new QComboBox(returnBox);
            returnForm->addRow(tr("Guardar em"),*returnKind); returnForm->addRow(tr("Destino"),*returnTarget);
            auto fillTarget=[=](int){
                (*returnTarget)->clear(); const QString kind=(*returnKind)->currentData().toString();
                if(kind==QLatin1String("variable")){
                    for(const core::VariableDef& var:ed.variables)(*returnTarget)->addItem(QStringLiteral("%1 — %2").arg(var.id).arg(var.name),var.id);
                }else if(kind==QLatin1String("switch")){
                    for(const core::SwitchDef& sw:ed.switches)(*returnTarget)->addItem(QStringLiteral("%1 — %2").arg(sw.id).arg(sw.name),sw.id);
                }else if(kind==QLatin1String("string")){
                    for(const core::StringDef& value:ed.strings)(*returnTarget)->addItem(QStringLiteral("%1 — %2").arg(value.id).arg(value.name),value.id);
                }else if(kind==QLatin1String("commonValue")&&m_commonContext){
                    for(const core::CommonEventLocal& def:m_commonContext->locals) if(def.type==ce->returnValue.type) (*returnTarget)->addItem(def.name,def.id);
                }
                (*returnTarget)->setEnabled(kind!=QLatin1String("none"));
                if(sameCall){
                    const QVariant oldId = kind==QLatin1String("commonValue") ? oldReturn.value(QStringLiteral("commonValueId")) : oldReturn.value(QStringLiteral("id"));
                    int ti=(*returnTarget)->findData(oldId); if(ti>=0)(*returnTarget)->setCurrentIndex(ti);
                }
            };
            fillTarget(0); connect(*returnKind,&QComboBox::currentIndexChanged,this,fillTarget);
            auto* hint=new QLabel(tr("Tipo retornado: %1").arg(core::commonValueTypeLabel(ce->returnValue.type)),returnBox);
            hint->setStyleSheet(QStringLiteral("color:#888;font-size:11px")); returnForm->addRow(hint);
        };
        rebuild(); connect(c,&QComboBox::currentIndexChanged,this,[rebuild](int){rebuild();});
        connect(this,&QDialog::accepted,this,[=]{
            QVariantMap arguments;
            for(const auto& pair:*argumentEditors){
                const QVariantMap spec=pair.second->value();
                if(spec.value(QStringLiteral("source")).toString()!=QLatin1String("default")) arguments[pair.first]=spec;
            }
            QVariantMap target; target[QStringLiteral("target")]=QStringLiteral("none");
            if(*returnKind){
                const QString kind=(*returnKind)->currentData().toString(); target[QStringLiteral("target")]=kind;
                if(kind==QLatin1String("commonValue")&&*returnTarget) target[QStringLiteral("commonValueId")]=(*returnTarget)->currentData();
                else if(kind!=QLatin1String("none")&&*returnTarget) target[QStringLiteral("id")]=(*returnTarget)->currentData();
            }
            const core::CommonEvent* selected=ed.commonEventById(c->currentData().toString());
            m_cmd.params={{QStringLiteral("commonId"),c->currentData()},
                          {QStringLiteral("number"),selected?selected->number:0},
                          {QStringLiteral("arguments"),arguments},
                          {QStringLiteral("returnTarget"),target}};
        });
    } else {
        setWindowTitle(tr("Comando"));
        form->addRow(new QLabel(tr("Este comando não tem opções."), this));
        connect(this, &QDialog::accepted, this, [this] { m_cmd.params.clear(); });
    }

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ============================================================================
//  Lista de comandos (usada pela página do evento E pelos eventos comuns)
// ============================================================================
QString CommandListWidget::descricao(const core::Editor& ed, const core::EventCommand& c)
{
    const QVariantMap& p = c.params;
    auto nomeSw = [&](int id) {
        const QString n = ed.switchName(id);
        return n.isEmpty() ? QString::number(id) : QStringLiteral("%1 (%2)").arg(id).arg(n);
    };
    auto nomeVar = [&](int id) {
        const QString n = ed.variableName(id);
        return n.isEmpty() ? QString::number(id) : QStringLiteral("%1 (%2)").arg(id).arg(n);
    };
    auto nomeString = [&](int id) {
        const QString n = ed.stringName(id);
        return n.isEmpty() ? QString::number(id) : QStringLiteral("%1 (%2)").arg(id).arg(n);
    };
    auto valorResumo = [&](const QVariant& raw, const QVariant& fallback=QVariant()) {
        if(!core::isValueSpec(raw))return raw.isValid()?raw.toString():fallback.toString();
        const QVariantMap spec=raw.toMap();const QString source=spec.value(QStringLiteral("source"),QStringLiteral("constant")).toString();
        if(source==QLatin1String("constant"))return spec.value(QStringLiteral("value"),fallback).toString();
        if(source==QLatin1String("variable"))return QObject::tr("Variável %1").arg(nomeVar(spec.value(QStringLiteral("variableId"),1).toInt()));
        if(source==QLatin1String("switch"))return QObject::tr("Switch %1").arg(nomeSw(spec.value(QStringLiteral("switchId"),1).toInt()));
        if(source==QLatin1String("string"))return QObject::tr("String %1").arg(nomeString(spec.value(QStringLiteral("stringId"),1).toInt()));
        if(source==QLatin1String("commonValue"))return QObject::tr("parâmetro/local");
        if(source==QLatin1String("gameValue"))return QObject::tr("Game Value: %1").arg(spec.value(QStringLiteral("query")).toMap().value(QStringLiteral("key")).toString());
        if(source==QLatin1String("databaseField"))return QObject::tr("campo do banco");
        if(source==QLatin1String("random"))return QObject::tr("sorteio %1..%2").arg(spec.value(QStringLiteral("minimum")).toInt()).arg(spec.value(QStringLiteral("maximum")).toInt());
        return QObject::tr("valor dinâmico");
    };
    auto nomeDatabase = [&](const QString& id) {
        if (const auto* db = ed.customDatabase(id)) return db->name.isEmpty() ? db->id : db->name;
        return id.isEmpty() ? QObject::tr("(banco não selecionado)") : id;
    };
    auto nomeDbField = [&](const QString& dbId, const QString& fieldId) {
        if (const auto* db = ed.customDatabase(dbId)) if (const auto* field = core::customDatabaseFieldById(*db, fieldId)) return field->name.isEmpty() ? field->id : field->name;
        return fieldId.isEmpty() ? QObject::tr("(campo não selecionado)") : fieldId;
    };
    auto nomeDbRecord = [&](const QString& dbId, const QString& recordId, const QVariantMap& recordSpec) {
        if (!recordSpec.isEmpty()) return QObject::tr("registro vindo de valor");
        if (const auto* db = ed.customDatabase(dbId)) if (const auto* record = core::customDatabaseRecordById(*db, recordId)) return record->name.isEmpty() ? QObject::tr("Registro %1").arg(record->number) : record->name;
        return recordId.isEmpty() ? QObject::tr("(registro não selecionado)") : recordId;
    };
    auto nomeRuntimeMap = [&](const QString& id) {
        if (id.isEmpty()) return QObject::tr("mapa atual");
        if (const core::MapDoc* map = ed.mapById(id)) return map->name.isEmpty() ? map->id : map->name;
        return id;
    };
    auto nomeRuntimeLayer = [&](const QString& mapId, const QString& layerId) {
        const core::MapDoc* map = mapId.isEmpty() ? ed.doc() : ed.mapById(mapId);
        if (!map) return layerId;
        std::function<QString(const QVector<core::LayerPtr>&)> find = [&](const QVector<core::LayerPtr>& layers) -> QString {
            for (const core::LayerPtr& layer : layers) { if (!layer) continue; if (layer->id == layerId) return layer->name.isEmpty()?layer->id:layer->name; const QString nested=find(layer->children); if(!nested.isEmpty())return nested; } return {};
        };
        const QString name=find(map->layers); return name.isEmpty()?layerId:name;
    };
    auto nomeRuntimeTileset = [&](const QString& id) {
        for (const core::Tileset& tileset : ed.tilesets) if (tileset.id == id) return tileset.name.isEmpty()?tileset.id:tileset.name;
        return id;
    };
    auto nomeRegistro = [&](const QString& category, const QString& id, const QString& fallback = QString()) {
        if (id.isEmpty()) return fallback;
        for (const core::DatabaseRecord& record : ed.database.value(category))
            if (record.id == id) return record.name.isEmpty() ? id : record.name;
        return fallback.isEmpty() ? id : fallback;
    };
    auto nomeItem = [&](const QString& id, const QString& fallback = QString()) {
        for (const QString& category : {QStringLiteral("items"), QStringLiteral("weapons"), QStringLiteral("armors")}) {
            const QString name = nomeRegistro(category, id);
            if (!name.isEmpty() && name != id) return name;
        }
        return fallback.isEmpty() ? id : fallback;
    };
    auto acao = [&](const QString& v) {
        if (v == QLatin1String("off")) return QObject::tr("desligar");
        if (v == QLatin1String("toggle")) return QObject::tr("inverter");
        return QObject::tr("ligar");
    };

    if (c.type == QLatin1String("message")) {
        QString t = p.value(QStringLiteral("text")).toString().replace(QLatin1Char('\n'),
                                                                       QStringLiteral(" / "));
        if (t.size() > 48) t = t.left(45) + QStringLiteral("…");
        const int ox = p.value(QStringLiteral("offsetX")).toInt();
        const int oy = p.value(QStringLiteral("offsetY")).toInt();
        return (ox || oy) ? QObject::tr("Mensagem: “%1” · offset (%2, %3)").arg(t).arg(ox).arg(oy)
                          : QObject::tr("Mensagem: “%1”").arg(t);
    }
    if (c.type == QLatin1String("dialogue.fastForward")) return QObject::tr("Avanço rápido: %1×").arg(p.value(QStringLiteral("speed"), 1.0).toDouble());
    if (c.type == QLatin1String("dialogue.skipMode")) return p.value(QStringLiteral("enabled"), true).toBool() ? QObject::tr("Texto instantâneo: ligado") : QObject::tr("Texto instantâneo: desligado");
    if (c.type == QLatin1String("subtitle.show") || c.type == QLatin1String("subtitle.enqueue")) {
        QString t = p.value(QStringLiteral("text")).toString().replace(QLatin1Char('\n'),
                                                                       QStringLiteral(" / "));
        if (t.size() > 40) t = t.left(37) + QStringLiteral("…");
        const QString quem = p.value(QStringLiteral("speaker")).toString();
        QString result = quem.isEmpty() ? QObject::tr("%1: “%2”").arg(c.type==QLatin1String("subtitle.enqueue")?QObject::tr("Enfileirar legenda"):QObject::tr("Legenda"),t)
                                        : QObject::tr("%1 [%2]: “%3”").arg(c.type==QLatin1String("subtitle.enqueue")?QObject::tr("Enfileirar legenda"):QObject::tr("Legenda"),quem,t);
        const int ox = p.value(QStringLiteral("offsetX")).toInt();
        const int oy = p.value(QStringLiteral("offsetY")).toInt();
        if (ox || oy) result += QObject::tr(" · offset (%1, %2)").arg(ox).arg(oy);
        return result;
    }
    if (c.type == QLatin1String("subtitle.configure")) return QObject::tr("Configurar legendas");
    if (c.type == QLatin1String("subtitle.clear"))     return QObject::tr("Limpar legendas");
    if (c.type == QLatin1String("subtitle.clearFade")) return QObject::tr("Ocultar legendas suavemente");
    if (c.type == QLatin1String("subtitle.wait"))      return QObject::tr("Esperar as legendas");
    if (c.type == QLatin1String("subtitle.clearQueue"))return QObject::tr("Limpar fila de legendas · %1").arg(p.value(QStringLiteral("track"),QStringLiteral("dialogue")).toString());
    if(c.type==QLatin1String("voice.play"))return QObject::tr("Voz [%1]: %2").arg(p.value(QStringLiteral("speakerId")).toString(),p.value(QStringLiteral("lineId"),p.value(QStringLiteral("source"))).toString());
    if(c.type==QLatin1String("voice.stop"))return QObject::tr("Parar fila de voz");
    if(c.type==QLatin1String("voice.waitForEnd"))return QObject::tr("Esperar fila de voz");
    if(c.type==QLatin1String("portrait.show"))return QObject::tr("Retrato [%1]: %2").arg(p.value(QStringLiteral("speakerId")).toString(),p.value(QStringLiteral("expression")).toString());
    if(c.type==QLatin1String("portrait.hide"))return QObject::tr("Ocultar retrato [%1]").arg(p.value(QStringLiteral("speakerId")).toString());
    if(c.type==QLatin1String("portrait.setExpression"))return QObject::tr("Expressão [%1]: %2").arg(p.value(QStringLiteral("speakerId")).toString(),p.value(QStringLiteral("expression")).toString());
    if(c.type==QLatin1String("bubble.show"))return QObject::tr("Balão [%1]: %2").arg(p.value(QStringLiteral("target"),QStringLiteral("self")).toString(),p.value(QStringLiteral("text")).toString().left(48));
    if(c.type==QLatin1String("bubble.hide"))return QObject::tr("Ocultar balão [%1]").arg(p.value(QStringLiteral("target"),QStringLiteral("self")).toString());
    if(c.type==QLatin1String("bubble.hideAll"))return QObject::tr("Ocultar todos os balões");
    if(c.type==QLatin1String("notification.show"))return QObject::tr("Notificação [%1]: %2").arg(p.value(QStringLiteral("position"),QStringLiteral("top-right")).toString(),p.value(QStringLiteral("text")).toString().left(48));
    if(c.type==QLatin1String("notification.hide"))return QObject::tr("Ocultar notificação");
    if (c.type == QLatin1String("choice.show")) {
        QStringList choices = p.value(QStringLiteral("choices")).toStringList();
        if (choices.isEmpty())
            for (const QVariant& value : p.value(QStringLiteral("choices")).toList())
                choices.push_back(value.toString());
        // O conteúdo das escolhas não ocupa a lista principal. O preview estrutural
        // fica sob demanda no botão "Ver prévia", sem reservar uma coluna.
        return QObject::tr("◆ Escolhas (%1)").arg(choices.size());
    }
    if(c.type==QLatin1String("input.number"))return QObject::tr("Digitar número em %1 · %2 até %3").arg(nomeVar(p.value(QStringLiteral("variableId"),1).toInt()),valorResumo(p.value(QStringLiteral("minimum")),0),valorResumo(p.value(QStringLiteral("maximum")),9999));
    if(c.type==QLatin1String("input.text"))return QObject::tr("Digitar texto → String %1").arg(nomeString(p.value(QStringLiteral("stringId"),1).toInt()));
    if(c.type==QLatin1String("input.confirm"))return QObject::tr("Confirmar · %1 → %2").arg(p.value(QStringLiteral("prompt"),QObject::tr("Continuar?")).toString(),nomeVar(p.value(QStringLiteral("resultVariable"),1).toInt()));
    if(c.type==QLatin1String("input.item"))return QObject::tr("Selecionar item → %1").arg(nomeVar(p.value(QStringLiteral("variableId"),1).toInt()));
    if(c.type==QLatin1String("input.wait")){bool ok=false;const auto a=core::gameActionFromId(p.value(QStringLiteral("action")).toString(),&ok);const QString st=p.value(QStringLiteral("state"),QStringLiteral("pressed")).toString();return QObject::tr("Esperar Input · %1 · %2").arg(ok?core::gameActionLabel(a):p.value(QStringLiteral("action")).toString(),st);}
    if (c.type == QLatin1String("switch.set"))
        return QObject::tr("Interruptor %1: %2")
            .arg(nomeSw(p.value(QStringLiteral("id"), 1).toInt()),
                 acao(p.value(QStringLiteral("value")).toString()));
    if (c.type == QLatin1String("selfSwitch.set"))
        return QObject::tr("Interruptor próprio %1: %2")
            .arg(p.value(QStringLiteral("letter"), QStringLiteral("A")).toString(),
                 acao(p.value(QStringLiteral("value")).toString()));
    if (c.type == QLatin1String("variable.set")) {
        const QVariantMap spec=p.value(QStringLiteral("sourceSpec")).toMap();
        const QString source=spec.value(QStringLiteral("source"),p.value(QStringLiteral("source"),QStringLiteral("constant"))).toString();
        QString value=spec.value(QStringLiteral("value"),p.value(QStringLiteral("value"),0)).toString();
        if(source==QLatin1String("variable"))value=QObject::tr("variável %1").arg(spec.value(QStringLiteral("variableId"),p.value(QStringLiteral("valueVariable"),1)).toInt());
        else if(source==QLatin1String("commonValue"))value=QObject::tr("parâmetro/local");
        else if(source==QLatin1String("random"))value=QObject::tr("sorteio");
        const int first=p.value(QStringLiteral("id"),1).toInt(),last=p.value(QStringLiteral("rangeEndId"),first).toInt();
        const QString target=last!=first?QObject::tr("Variáveis %1..%2").arg(first).arg(last):QObject::tr("Variável %1").arg(nomeVar(first));
        return QObject::tr("%1 %2 %3").arg(target,p.value(QStringLiteral("op"),QStringLiteral("=")).toString(),value);
    }
    if (c.type == QLatin1String("string.set")) return QObject::tr("String %1 · %2").arg(nomeString(p.value(QStringLiteral("id"),1).toInt()),p.value(QStringLiteral("op"),QStringLiteral("=")).toString());
    if (c.type == QLatin1String("variable.math")) return QObject::tr("Calcular %1 · %2").arg(nomeVar(p.value(QStringLiteral("id"),1).toInt()),p.value(QStringLiteral("operation"),QStringLiteral("add")).toString());
    if (c.type == QLatin1String("value.get")) return QObject::tr("Obter valor do jogo · %1").arg(p.value(QStringLiteral("query")).toMap().value(QStringLiteral("key")).toString());
    if (c.type == QLatin1String("map.runtime.tile")) return QObject::tr("Mapa Runtime · Alterar Tile · %1 / %2").arg(nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()),nomeRuntimeLayer(p.value(QStringLiteral("mapId")).toString(),p.value(QStringLiteral("layerId")).toString()));
    if (c.type == QLatin1String("map.runtime.fill")) return QObject::tr("Mapa Runtime · Preencher Área · %1 / %2").arg(nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()),nomeRuntimeLayer(p.value(QStringLiteral("mapId")).toString(),p.value(QStringLiteral("layerId")).toString()));
    if (c.type == QLatin1String("map.runtime.copy")) return QObject::tr("Mapa Runtime · Copiar Área · %1").arg(nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()));
    if (c.type == QLatin1String("map.runtime.passage")) return QObject::tr("Mapa Runtime · Alterar Passagem · %1").arg(nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()));
    if (c.type == QLatin1String("map.runtime.terrain")) return QObject::tr("Mapa Runtime · Alterar Terrain/Tag · %1").arg(nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()));
    if (c.type == QLatin1String("map.runtime.tileset")) return QObject::tr("Mapa Runtime · Tileset %1 → %2 · %3").arg(nomeRuntimeTileset(p.value(QStringLiteral("sourceTilesetId")).toString()),p.value(QStringLiteral("targetTilesetId")).toString().isEmpty()?QObject::tr("original"):nomeRuntimeTileset(p.value(QStringLiteral("targetTilesetId")).toString()),nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()));
    if (c.type == QLatin1String("map.runtime.reset")) return QObject::tr("Mapa Runtime · Resetar %1 · %2").arg(p.value(QStringLiteral("scope"),QStringLiteral("cell")).toString(),nomeRuntimeMap(p.value(QStringLiteral("mapId")).toString()));
    if (c.type == QLatin1String("database.get")) {
        const QString db=p.value(QStringLiteral("databaseId")).toString();
        return QObject::tr("DB · Ler %1 → %2 · %3").arg(nomeDbField(db,p.value(QStringLiteral("fieldId")).toString()),nomeDbRecord(db,p.value(QStringLiteral("recordId")).toString(),p.value(QStringLiteral("recordSpec")).toMap()),nomeDatabase(db));
    }
    if (c.type == QLatin1String("database.set")) {
        const QString db=p.value(QStringLiteral("databaseId")).toString();
        return QObject::tr("Banco · Alterar %1 · %2 · %3").arg(nomeDbField(db,p.value(QStringLiteral("fieldId")).toString()),nomeDbRecord(db,p.value(QStringLiteral("recordId")).toString(),p.value(QStringLiteral("recordSpec")).toMap()),nomeDatabase(db));
    }
    if (c.type == QLatin1String("database.find")) return QObject::tr("Banco · Procurar em %1 · %2").arg(nomeDatabase(p.value(QStringLiteral("databaseId")).toString()),nomeDbField(p.value(QStringLiteral("databaseId")).toString(),p.value(QStringLiteral("fieldId")).toString()));
    if (c.type == QLatin1String("database.count")) return QObject::tr("DB · Contar registros · %1").arg(nomeDatabase(p.value(QStringLiteral("databaseId")).toString()));
    if (c.type == QLatin1String("database.exists")) return QObject::tr("DB · Registro existe? · %1").arg(nomeDatabase(p.value(QStringLiteral("databaseId")).toString()));
    if (c.type == QLatin1String("database.copy")) return QObject::tr("Banco · Copiar dados · %1").arg(nomeDatabase(p.value(QStringLiteral("databaseId")).toString()));
    if (c.type == QLatin1String("database.reset")) return QObject::tr("Banco · Restaurar registro · %1").arg(nomeDatabase(p.value(QStringLiteral("databaseId")).toString()));
    if (c.type == QLatin1String("database.recordInfo")) return QObject::tr("DB · Obter %1 do registro · %2").arg(p.value(QStringLiteral("info"),QStringLiteral("id")).toString(),nomeDatabase(p.value(QStringLiteral("databaseId")).toString()));
    if (c.type == QLatin1String("database.each")) return QObject::tr("Para cada registro de %1").arg(nomeDatabase(p.value(QStringLiteral("databaseId")).toString()));
    if (c.type == QLatin1String("database.each.end")) return QObject::tr("Fim de Para cada registro");
    if (c.type == QLatin1String("wait.until")) {
        core::EventCommand condition=c;condition.type=QStringLiteral("if");condition.params.remove(QStringLiteral("timeoutFrames"));
        QString conditionText=descricao(ed,condition);if(conditionText.startsWith(QObject::tr("Se ")))conditionText.remove(0,QObject::tr("Se ").size());
        double timeoutNumber=0.0;const QVariant timeoutRaw=p.value(QStringLiteral("timeoutFrames"),0);const bool timeoutIsStatic=!core::isValueSpec(timeoutRaw)||timeoutRaw.toMap().value(QStringLiteral("source"),QStringLiteral("constant")).toString()==QLatin1String("constant");if(timeoutIsStatic)timeoutNumber=core::isValueSpec(timeoutRaw)?timeoutRaw.toMap().value(QStringLiteral("value")).toDouble():timeoutRaw.toDouble();
        return timeoutIsStatic&&timeoutNumber<=0?QObject::tr("Esperar até %1").arg(conditionText)
            :QObject::tr("Esperar até %1 · limite %2f").arg(conditionText,valorResumo(timeoutRaw,0));
    }
    if (c.type == QLatin1String("parallel.begin")) return QObject::tr("Executar em paralelo · cada comando/estrutura inicia junto");
    if (c.type == QLatin1String("parallel.end")) return QObject::tr("Fim do paralelo · aguardar todas as tarefas");
    if (c.type == QLatin1String("if")) {
        const QVariantMap tree=p.value(QStringLiteral("conditionTree")).toMap();
        if(!tree.isEmpty()){
            const core::ConditionTreeStats stats=core::conditionTreeStats(tree);const QString mode=core::normalizedConditionGroupMode(tree.value(QStringLiteral("mode")).toString())==QLatin1String("any")?QObject::tr("QUALQUER (OR)"):QObject::tr("TODAS (AND)");
            return QObject::tr("Se · %1 · condições: %2%3").arg(mode).arg(stats.conditions).arg(stats.groups>1?QObject::tr(" · %1 grupos").arg(stats.groups):QString());
        }
        const QString k = p.value(QStringLiteral("kind"), QStringLiteral("switch")).toString();
        if (k == QLatin1String("variable"))
            return QObject::tr("Se variável %1 %2 %3")
                .arg(nomeVar(p.value(QStringLiteral("id"), 1).toInt()),
                     p.value(QStringLiteral("op"), QStringLiteral(">=")).toString(),
                     valorResumo(p.contains(QStringLiteral("rightSpec"))?p.value(QStringLiteral("rightSpec")):p.value(QStringLiteral("value")),0));
        if(k=="random")return QObject::tr("Se sorteio de %1% acontecer").arg(p.value("chance").toInt());
        if(k=="playerPosition")return QObject::tr("Se jogador estiver em (%1,%2)").arg(p.value("x").toInt()).arg(p.value("y").toInt());
        if(k=="direction")return QObject::tr("Se jogador estiver voltado para direção %1").arg(p.value("direction").toInt()+1);
        if(k=="distance")return QObject::tr("Se jogador estiver a até %1 tiles do evento").arg(p.value("distance").toDouble());
        if(k=="routeRunning")return QObject::tr("Se a rota de %1 estiver em execução").arg(p.value("target").toString());
        if(k=="map")return QObject::tr("Se estiver no mapa selecionado");
        if(k=="button")return QObject::tr("Se a ação %1 estiver pressionada").arg(p.value("action").toString());
        if(k=="gold")return QObject::tr("Se ouro %1 %2").arg(p.value("op").toString(),valorResumo(p.contains(QStringLiteral("rightSpec"))?p.value(QStringLiteral("rightSpec")):p.value(QStringLiteral("value")),0));
        if(k=="item")return QObject::tr("Se possui %1× item %2").arg(valorResumo(p.contains(QStringLiteral("amountSpec"))?p.value(QStringLiteral("amountSpec")):p.value(QStringLiteral("amount")),1),p.value("itemId").toString());
        if (k == QLatin1String("selfSwitch"))
            return QObject::tr("Se interruptor próprio %1 %2")
                .arg(p.value(QStringLiteral("letter"), QStringLiteral("A")).toString(),
                     p.value(QStringLiteral("value"), true).toBool() ? QObject::tr("ligado")
                                                                     : QObject::tr("desligado"));
        return QObject::tr("Se interruptor %1 %2")
            .arg(nomeSw(p.value(QStringLiteral("id"), 1).toInt()),
                 p.value(QStringLiteral("value"), true).toBool() ? QObject::tr("ligado")
                                                                 : QObject::tr("desligado"));
    }
    if (c.type == QLatin1String("else"))  return QObject::tr("Senão");
    if (c.type == QLatin1String("endIf")) return QObject::tr("Fim da condição");
    if (c.type == QLatin1String("loop.begin")) return QObject::tr("Repetir continuamente");
    if (c.type == QLatin1String("loop.break")) return QObject::tr("Sair da repetição");
    if (c.type == QLatin1String("loop.end")) return QObject::tr("Fim da repetição contínua");
    if (c.type == QLatin1String("repeat.begin")) {
        const QVariantMap spec=p.value(QStringLiteral("countSpec")).toMap();
        const QString source=spec.value(QStringLiteral("source"),QStringLiteral("constant")).toString();
        const QString count=source==QLatin1String("constant")?QString::number(spec.value(QStringLiteral("value"),p.value(QStringLiteral("count"),1)).toInt()):QObject::tr("valor dinâmico");
        return QObject::tr("Repetir %1 vezes").arg(count);
    }
    if (c.type == QLatin1String("repeat.break")) return QObject::tr("Sair de Repetir N vezes");
    if (c.type == QLatin1String("repeat.end")) return QObject::tr("Fim de Repetir N vezes");
    if (c.type == QLatin1String("map.event.call")) {
        const QString mapId=p.value(QStringLiteral("mapId")).toString();const QString eventId=p.value(QStringLiteral("eventId")).toString();
        QString eventName=eventId;if(const core::MapDoc* map=ed.mapById(mapId))for(const core::MapEvent& ev:map->events)if(ev.id==eventId){eventName=ev.name.isEmpty()?ev.id:ev.name;break;}
        return QObject::tr("Chamar evento de mapa · %1 / %2").arg(nomeRuntimeMap(mapId),eventName);
    }
    if (c.type == QLatin1String("flow.exit")) {
        const QString scope=p.value(QStringLiteral("scope"),QStringLiteral("frame")).toString();
        if(scope==QLatin1String("all"))return QObject::tr("Exit · encerrar toda a cadeia");
        if(scope==QLatin1String("common"))return QObject::tr("Return · Evento Comum atual");
        if(scope==QLatin1String("map"))return QObject::tr("Exit · evento de mapa atual");
        return QObject::tr("Exit · frame atual");
    }
    if (c.type == QLatin1String("label"))
        return QObject::tr("Rótulo “%1”").arg(p.value(QStringLiteral("name")).toString());
    if (c.type == QLatin1String("jump"))
        return QObject::tr("Pular para “%1”").arg(p.value(QStringLiteral("label")).toString());
    if (c.type == QLatin1String("wait"))
        return QObject::tr("Esperar %1 quadros").arg(p.value(QStringLiteral("frames"), 30).toInt());
    if (c.type == QLatin1String("move.route")) {
        const core::MoveRoute r=core::moveRouteFromMap(p.value(QStringLiteral("route")).toMap());
        QString target=r.target==QLatin1String("player")?QObject::tr("Jogador"):QObject::tr("Este evento");
        if(r.target.startsWith(QLatin1String("event:"))){const QString id=r.target.mid(6);if(const core::MapDoc* d=ed.doc())for(const core::MapEvent& e:d->events)if(e.id==id){target=e.name;break;}}
        const QString mode=r.startMode==core::MoveRouteStartMode::Queue?QObject::tr(" · na fila"):QString();
        const QString steps=r.commands.size()==1?QObject::tr("1 etapa"):QObject::tr("%1 etapas").arg(r.commands.size());
        return QObject::tr("Rota de %1: %2%3%4").arg(target,steps).arg(r.waitForCompletion?QObject::tr(" · esperar terminar"):QString()).arg(mode);
    }
    if(c.type==QLatin1String("move.route.control")){
        const QString target=p.value(QStringLiteral("target"),QStringLiteral("self")).toString();
        const QString action=p.value(QStringLiteral("action"),QStringLiteral("pause")).toString();
        const QString actionLabel=action==QLatin1String("resume")?QObject::tr("Retomar"):action==QLatin1String("cancel")?QObject::tr("Cancelar"):action==QLatin1String("clearQueue")?QObject::tr("Limpar fila"):QObject::tr("Pausar");
        return QObject::tr("%1 rota de %2").arg(actionLabel,target==QLatin1String("player")?QObject::tr("Jogador"):target==QLatin1String("self")?QObject::tr("Este evento"):target);
    }
    if (c.type == QLatin1String("map.transfer")) {
        QString mapName = QObject::tr("mapa inexistente");
        if (const core::MapDoc* map = ed.mapById(p.value(QStringLiteral("mapId")).toString()))
            mapName = map->name;
        return QObject::tr("Mover jogador → %1 · posição (%2, %3)")
            .arg(mapName).arg(p.value(QStringLiteral("x")).toInt())
            .arg(p.value(QStringLiteral("y")).toInt());
    }
    if (c.type == QLatin1String("battle.start")) {
        QString troopName = QObject::tr("tropa inexistente");
        const QString troopId = p.value(QStringLiteral("troopId")).toString();
        for (const core::DatabaseRecord& record : ed.database.value(QStringLiteral("troops")))
            if (record.id == troopId) { troopName = record.name; break; }
        return QObject::tr("Iniciar batalha → %1%2")
            .arg(troopName,
                 p.value(QStringLiteral("allowEscape"), true).toBool()
                    ? QObject::tr(" · pode fugir") : QString());
    }
    if (c.type == QLatin1String("shop.open")) {
        QStringList items = p.value(QStringLiteral("itemIds")).toStringList();
        if (items.isEmpty())
            for (const QVariant& value : p.value(QStringLiteral("itemIds")).toList())
                items.push_back(value.toString());
        return QObject::tr("Abrir loja · produtos: %1%2").arg(items.size())
            .arg(p.value(QStringLiteral("purchaseOnly")).toBool()
                     ? QObject::tr(" · somente compra") : QString());
    }
    if (c.type == QLatin1String("shop.inn"))
        return QObject::tr("Abrir pousada: %1 G%2")
            .arg(valorResumo(p.value(QStringLiteral("cost")),50))
            .arg(p.value(QStringLiteral("removeStates"), true).toBool()
                     ? QObject::tr(" · remover estados") : QString());
    if (c.type == QLatin1String("weather.set")) {
        WeatherState weather;
        weather.setConfig(p.value(QStringLiteral("type"), QStringLiteral("none")).toString(),
                          p.value(QStringLiteral("intensity"), 50).toInt(),
                          p.value(QStringLiteral("thunderSe")).toString(),
                          p.value(QStringLiteral("thunderVolume"), 90).toInt(), true);
        const QString label = weather.kind == WeatherKind::Rain ? QObject::tr("Chuva")
            : weather.kind == WeatherKind::Snow ? QObject::tr("Neve")
            : weather.kind == WeatherKind::Storm ? QObject::tr("Tempestade")
            : QObject::tr("Sem clima");
        QString result = QObject::tr("Alterar clima: %1 · intensidade %2%")
            .arg(label).arg(weather.intensity);
        if (weather.kind == WeatherKind::Storm && !weather.thunderSePath.isEmpty())
            result += QObject::tr(" · trovão %1 (%2%)")
                .arg(QFileInfo(weather.thunderSePath).fileName())
                .arg(weather.thunderVolume);
        return result;
    }
    if (c.type.startsWith(QLatin1String("quest."))) {
        QString questName = QObject::tr("missão inexistente");
        const QString questId = p.value(QStringLiteral("questId")).toString();
        for (const core::DatabaseRecord& record : ed.database.value(QStringLiteral("quests")))
            if (record.id == questId) { questName = record.name; break; }
        if (c.type == QLatin1String("quest.start"))
            return QObject::tr("Iniciar missão: %1 · meta %2").arg(questName)
                .arg(valorResumo(p.value(QStringLiteral("target")),1));
        if (c.type == QLatin1String("quest.progress"))
            return QObject::tr("Atualizar missão: %1 · %2 %3").arg(
                questName, p.value(QStringLiteral("operation"), QStringLiteral("add")).toString())
                .arg(valorResumo(p.value(QStringLiteral("amount")),1));
        return c.type == QLatin1String("quest.complete")
            ? QObject::tr("Concluir missão: %1").arg(questName)
            : QObject::tr("Falhar missão: %1").arg(questName);
    }
    if (c.type == QLatin1String("plugin.call")) {
        const NoCodePlugin* plugin = noCodePluginById(ed.plugins, p.value(QStringLiteral("pluginId")).toString());
        const PluginCommand* command = plugin
            ? pluginCommandById(*plugin, p.value(QStringLiteral("commandId")).toString()) : nullptr;
        return plugin && command ? QObject::tr("%1 ▸ %2").arg(plugin->name, command->name)
                                 : QObject::tr("Extensão/comando ausente");
    }
    if (c.type == QLatin1String("party.change")) {
        const QString actorId = p.value(QStringLiteral("actorId")).toString();
        const QString actorName = nomeRegistro(QStringLiteral("actors"), actorId,
                                               p.value(QStringLiteral("actorName"), actorId).toString());
        return QObject::tr("%1 personagem: %2")
            .arg(p.value(QStringLiteral("operation"), QStringLiteral("add")).toString() == QLatin1String("remove")
                     ? QObject::tr("Remover") : QObject::tr("Adicionar"), actorName);
    }
    if (c.type == QLatin1String("party.gold"))
        return QObject::tr("Alterar ouro: %1 %2")
            .arg(p.value(QStringLiteral("operation"), QStringLiteral("add")).toString())
            .arg(valorResumo(p.value(QStringLiteral("amount")),1));
    if (c.type == QLatin1String("inventory.change")) {
        const QString itemId = p.value(QStringLiteral("itemId")).toString();
        const QString itemName = nomeItem(itemId, p.value(QStringLiteral("itemName"), itemId).toString());
        return QObject::tr("Alterar inventário: %1 ×%2")
            .arg(itemName)
            .arg(valorResumo(p.value(QStringLiteral("amount")),1));
    }
    if (c.type.startsWith(QLatin1String("actor."))) {
        const QString actorId = p.value(QStringLiteral("actorId")).toString();
        const QString actorName = actorId.isEmpty() ? QObject::tr("grupo")
            : nomeRegistro(QStringLiteral("actors"), actorId, p.value(QStringLiteral("actorName"), actorId).toString());
        return QObject::tr("Alterar %1 de %2").arg(c.type.mid(6).toUpper(), actorName);
    }
    if (c.type.startsWith(QLatin1String("audio."))) {
        const QString channel=c.type.mid(6);const auto channelLabel=[](const QString& id){return id==QLatin1String("bgm")?QObject::tr("música de fundo"):id==QLatin1String("bgs")?QObject::tr("som ambiente"):id==QLatin1String("me")?QObject::tr("música curta"):id==QLatin1String("se")?QObject::tr("efeito sonoro"):id==QLatin1String("voice")?QObject::tr("voz"):id;};
        if (c.type == QLatin1String("audio.stop")) return QObject::tr("Parar %1").arg(channelLabel(p.value(QStringLiteral("channel")).toString()));
        if (c.type == QLatin1String("audio.footstep")) {
            const QString id=p.value(QStringLiteral("surfaceId")).toString();
            const core::FootstepSurface* surface=ed.footstepSurfaceById(id);
            return QObject::tr("Tocar som de passo: %1 · %2%").arg(surface?surface->name:QObject::tr("automático")).arg(p.value(QStringLiteral("volume"),100).toInt());
        }
        return QObject::tr("Tocar %1: %2 · %3%").arg(
            channelLabel(channel),
            QFileInfo(p.value(QStringLiteral("source")).toString()).fileName())
            .arg(p.value(QStringLiteral("volume"), 90).toInt());
    }
    if (c.type == QLatin1String("localization.set")) {
        const QString code = p.value(QStringLiteral("locale")).toString();
        QString label = code;
        for (const core::LocalizationLocale& locale : ed.localization.locales)
            if (locale.code.compare(code, Qt::CaseInsensitive) == 0) { label = locale.name; break; }
        if (label.isEmpty()) label = QObject::tr("Idioma padrão");
        return QObject::tr("Alterar idioma → %1 (%2)").arg(label, code.isEmpty() ? ed.localization.defaultLocale : code);
    }
    if (c.type == QLatin1String("game.save"))
        return QObject::tr("Salvar partida no slot %1").arg(p.value(QStringLiteral("slot"), 1).toInt());
    if (c.type == QLatin1String("game.load"))
        return QObject::tr("Carregar partida do slot %1").arg(p.value(QStringLiteral("slot"), 1).toInt());
    if(c.type==QLatin1String("game.restart"))return QObject::tr("Reiniciar partida");
    if(c.type==QLatin1String("game.gameOver"))return QObject::tr("Mostrar tela de Game Over");
    if(c.type==QLatin1String("game.returnTitle"))return QObject::tr("Voltar à tela de título");
    if(c.type==QLatin1String("ludo.filter.chromaticAberration"))return QObject::tr("Aberração Cromática #%1 (%2): %3 px · %4 quadros").arg(p.value("slot",1).toInt()).arg(p.value("mode",QStringLiteral("lens")).toString()).arg(p.value("intensity",4.0).toDouble(),0,'f',1).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.filter.noise")){const auto cfg=core::NoiseFilterConfig::fromVariantMap(p);return QObject::tr("Ruído #%1: %2 · %3% · grão %4 px · %5 quadros").arg(p.value("slot",1).toInt()).arg(core::noiseStyleLabel(cfg.style)).arg(int(cfg.intensity*100.0)).arg(cfg.grainSizePixels,0,'f',1).arg(p.value("duration",30).toInt());}
    if(c.type==QLatin1String("ludo.filter.scanlines")){const auto cfg=core::ScanlineFilterConfig::fromVariantMap(p);return QObject::tr("Linhas de tela #%1: %2 · %3% · %4 px%5").arg(p.value("slot",1).toInt()).arg(core::scanlineStyleLabel(cfg.style)).arg(int(cfg.intensity*100.0)).arg(cfg.spacingPixels,0,'f',1).arg(cfg.whiteSweep?QObject::tr(" · varredura"):QString());}
    if(c.type==QLatin1String("ludo.filter.vignette"))return QObject::tr("Vinheta #%1: %2% · raio %3%").arg(p.value("slot",1).toInt()).arg(int(p.value("intensity",.45).toDouble()*100.0)).arg(int(p.value("radius",.68).toDouble()*100.0));
    if(c.type==QLatin1String("ludo.filter.blur"))return QObject::tr("Desfoque #%1 (%2): %3 px · %4%").arg(p.value("slot",1).toInt()).arg(p.value("style",QStringLiteral("legacy")).toString()).arg(p.value("radius",4.0).toDouble(),0,'f',1).arg(int(p.value("strength",1.0).toDouble()*100.0));
    if(c.type==QLatin1String("ludo.filter.tiltShift")){const auto cfg=core::TiltShiftFilterConfig::fromVariantMap(p);return QObject::tr("Foco seletivo #%1: %2 · desfoque %3 px · foco %4%").arg(p.value("slot",1).toInt()).arg(core::tiltShiftStyleLabel(cfg.style)).arg(cfg.blurPixels,0,'f',1).arg(int(cfg.focusWidth*100.0));}
    if(c.type==QLatin1String("ludo.filter.clear"))return QObject::tr("Remover filtro: %1 · %2").arg(p.value("filter",QStringLiteral("all")).toString()).arg(p.value("allSlots",true).toBool()?QObject::tr("todos os slots"):QObject::tr("slot %1").arg(p.value("slot",1).toInt()));
    if(c.type==QLatin1String("ludo.camera.move"))return QObject::tr("Ludo Camera: %1 · zoom %2× · %3 quadros").arg(p.value("target","player").toString()).arg(p.value("zoom",2.0).toDouble()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.camera.zoomOnly"))return QObject::tr("Ludo Camera: apenas zoom %1× · %2 quadros").arg(p.value("zoom",2.0).toDouble()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.camera.moveOnly"))return QObject::tr("Ludo Camera: apenas movimento para %1 · %2 quadros").arg(p.value("target","player").toString()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.camera.reset"))return QObject::tr("Ludo Camera: voltar suavemente ao jogador");
    if(c.type==QLatin1String("ludo.camera.save"))return QObject::tr("Ludo Camera: salvar posição");
    if(c.type==QLatin1String("ludo.camera.restore"))return QObject::tr("Ludo Camera: restaurar posição");
    if(c.type==QLatin1String("ludo.camera.release"))return QObject::tr("Ludo Camera: liberar câmera");
    if(c.type==QLatin1String("ludo.sprite.fade"))return QObject::tr("Ludo Sprite: fade de %1 para opacidade %2 em %3 quadros").arg(p.value("target","self").toString()).arg(p.value("opacity",255).toInt()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.sprite.offset"))return QObject::tr("Ludo Sprite: offset de %1 para (%2, %3) em %4 quadros").arg(p.value("target","self").toString()).arg(p.value("x",0).toDouble()).arg(p.value("y",0).toDouble()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.sprite.clearOffset"))return QObject::tr("Ludo Sprite: restaurar offset de %1").arg(p.value("target","self").toString());
    if(c.type==QLatin1String("ludo.sprite.zoom"))return QObject::tr("Ludo Sprite: zoom de %1 para %2×%3").arg(p.value("target","self").toString()).arg(p.value("scaleX",1.0).toDouble()).arg(p.value("scaleY",1.0).toDouble());
    if(c.type==QLatin1String("ludo.sprite.resetZoom"))return QObject::tr("Ludo Sprite: restaurar zoom de %1").arg(p.value("target","self").toString());
    if(c.type==QLatin1String("ludo.sprite.shake"))return QObject::tr("Ludo Sprite: tremer %1 em %2×%3 px").arg(p.value("target","self").toString()).arg(p.value("x",4.0).toDouble()).arg(p.value("y",0.0).toDouble());
    if(c.type==QLatin1String("ludo.sprite.phantom"))return QObject::tr("Ludo Sprite: fantasma de %1 · distância %2").arg(p.value("target","self").toString()).arg(p.value("distance",6).toDouble());
    if(c.type==QLatin1String("ludo.sprite.clear"))return QObject::tr("Ludo Sprite: limpar efeitos");
    if(c.type==QLatin1String("ludo.screen.tone"))return QObject::tr("Tonalidade da Tela: R %1 · G %2 · B %3 · Cinza %4 · %5 quadros").arg(p.value("red",0).toInt()).arg(p.value("green",0).toInt()).arg(p.value("blue",0).toInt()).arg(p.value("gray",0).toInt()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.screen.clearTone"))return QObject::tr("Remover Tonalidade da Tela");
    if(c.type==QLatin1String("ludo.screen.flash"))return QObject::tr("Piscar a tela: cor %1,%2,%3 · intensidade %4 · %5 quadros").arg(p.value("red",255).toInt()).arg(p.value("green",255).toInt()).arg(p.value("blue",255).toInt()).arg(p.value("alpha",180).toInt()).arg(p.value("duration",20).toInt());
    if(c.type==QLatin1String("ludo.screen.fade"))return QObject::tr("Transição da tela: %1 · intensidade %2 · %3 quadros").arg(p.value("direction",QStringLiteral("out")).toString()==QLatin1String("in")?QObject::tr("revelar"):QObject::tr("escurecer")).arg(p.value("alpha",255).toInt()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.screen.shake"))return QObject::tr("Tremer Tela: %1×%2 px · %3 quadros").arg(p.value("x",6.0).toDouble()).arg(p.value("y",3.0).toDouble()).arg(p.value("duration",30).toInt());
    if(c.type==QLatin1String("ludo.screen.clearEffects"))return QObject::tr("Limpar Flash/Fade/Shake da Tela");
    if(c.type==QLatin1String("ludo.screen.set")||c.type==QLatin1String("ludo.screen.clear"))return QObject::tr("Comando antigo removido: Ludo Screen Filters");
    if(c.type==QLatin1String("ludo.cutscene.begin"))return QObject::tr("Ludo Cutscene: início da cena pulável");
    if(c.type==QLatin1String("ludo.cutscene.end"))return QObject::tr("Ludo Cutscene: fim da região pulável");
    if(c.type==QLatin1String("ludo.cutscene.settings"))return QObject::tr("Ludo Cutscene: configurar próxima região · %1 · ação %2").arg(p.value("skipAllowed",true).toBool()?QObject::tr("skip permitido"):QObject::tr("skip bloqueado"),p.value("skipAction",QStringLiteral("skipCutscene")).toString());
    if(c.type==QLatin1String("ludo.cutscene.enable"))return QObject::tr("Ludo Cutscene Skip: ativar");
    if(c.type==QLatin1String("ludo.cutscene.disable"))return QObject::tr("Ludo Cutscene Skip: desativar");
    if(c.type==QLatin1String("comment"))return QObject::tr("Comentário: %1").arg(p.value("text").toString());
    if (c.type.startsWith(QLatin1String("picture."))) {
        const int n = p.value(QStringLiteral("number"), 1).toInt();
        const QString alvo=p.contains(QStringLiteral("logicalName"))?QObject::tr("“%1”").arg(p.value(QStringLiteral("logicalName")).toString()):QString::number(n);
        if(c.type==QLatin1String("picture.showByName"))return QObject::tr("Mostrar imagem chamada %1").arg(alvo);
        if(c.type==QLatin1String("picture.setGroup"))return QObject::tr("Colocar a imagem %2 no grupo “%1”").arg(p.value(QStringLiteral("group")).toString(),alvo);
        if(c.type==QLatin1String("picture.moveGroup"))return QObject::tr("Mover o grupo de imagens “%1” em %2 quadros").arg(p.value(QStringLiteral("group")).toString()).arg(p.value(QStringLiteral("duration")).toInt());
        if(c.type==QLatin1String("picture.eraseGroup"))return QObject::tr("Apagar o grupo de imagens “%1”").arg(p.value(QStringLiteral("group")).toString());
        if(c.type==QLatin1String("picture.attach"))return QObject::tr("Fazer a imagem %1 seguir %2").arg(alvo,p.value(QStringLiteral("target")).toString());
        if(c.type==QLatin1String("picture.detach"))return QObject::tr("Fazer a imagem %1 parar de seguir o alvo").arg(alvo);
        if(c.type==QLatin1String("picture.timeline.define"))return QObject::tr("Definir timeline “%1” · %2 quadros").arg(p.value(QStringLiteral("name")).toString()).arg(p.value(QStringLiteral("duration")).toInt());
        if(c.type==QLatin1String("picture.timeline.play"))return QObject::tr("Reproduzir a animação “%1” na imagem %2").arg(p.value(QStringLiteral("name")).toString(),alvo);
        if(c.type==QLatin1String("picture.timeline.stop"))return QObject::tr("Parar a animação da imagem %1").arg(alvo);
        if(c.type==QLatin1String("picture.onClick")||c.type==QLatin1String("picture.onTouch"))return QObject::tr("Imagem %1 · %2 → Evento Comum %3").arg(alvo,c.type.endsWith(QLatin1String("onClick"))?QObject::tr("clique"):QObject::tr("toque"),p.value(QStringLiteral("commonEventId")).toString());
        if (c.type == QLatin1String("picture.show")) {
            QString nome = p.value(QStringLiteral("assetName")).toString();
            if (const core::PictureAsset* a =
                    ed.pictureById(p.value(QStringLiteral("assetId")).toString()))
                nome = a->name;
            QString extra;
            if (!p.value(QStringLiteral("nineSlice")).toMap().isEmpty())
                extra = QObject::tr(" · 9-slice");
            return QObject::tr("Mostrar imagem %1: “%2” em (%3, %4)%5")
                .arg(n).arg(nome)
                .arg(p.value(QStringLiteral("x"), 0).toInt())
                .arg(p.value(QStringLiteral("y"), 0).toInt()).arg(extra);
        }
        if (c.type == QLatin1String("picture.text")) {
            QString t = core::PictureRichText::fromParams(
                            p.value(QStringLiteral("rich")).toMap()).text
                            .replace(QLatin1Char('\n'), QStringLiteral(" / "));
            if (t.size() > 40) t = t.left(37) + QStringLiteral("…");
            return QObject::tr("Texto como imagem %1: “%2”").arg(n).arg(t);
        }
        if (c.type == QLatin1String("picture.move")) {
            QStringList alvos;
            auto talvez = [&](const char* k, const QString& rot) {
                const QString key = QString::fromLatin1(k);
                if (p.contains(key))
                    alvos << QStringLiteral("%1 %2").arg(rot).arg(p.value(key).toDouble());
            };
            talvez("x", QObject::tr("X"));
            talvez("y", QObject::tr("Y"));
            talvez("scaleX", QObject::tr("escala X"));
            talvez("scaleY", QObject::tr("escala Y"));
            talvez("opacity", QObject::tr("opacidade"));
            talvez("angle", QObject::tr("ângulo"));
            return QObject::tr("Mover imagem %1 → %2 em %3 quadros")
                .arg(n).arg(alvos.isEmpty() ? QObject::tr("(nada)") : alvos.join(QStringLiteral(", ")))
                .arg(p.value(QStringLiteral("duration"), 0).toInt());
        }
        if (c.type == QLatin1String("picture.eraseAll"))
            return QObject::tr("Apagar todas as imagens");
        if (c.type == QLatin1String("picture.erase"))
            return n <= 0 ? QObject::tr("Apagar todas as imagens")
                          : QObject::tr("Apagar imagem %1").arg(n);
        if (c.type == QLatin1String("picture.zoomIn") || c.type == QLatin1String("picture.zoomOut")) {
            const bool in = c.type == QLatin1String("picture.zoomIn");
            const double fallback = in ? 150.0 : 50.0;
            return QObject::tr("%1 da imagem %2 → %3% × %4% em %5 quadros")
                .arg(in ? QObject::tr("Zoom In") : QObject::tr("Zoom Out"))
                .arg(p.value(QStringLiteral("number"), 1).toInt())
                .arg(p.value(QStringLiteral("scaleX"), fallback).toDouble())
                .arg(p.value(QStringLiteral("scaleY"), fallback).toDouble())
                .arg(p.value(QStringLiteral("duration"), 30).toInt());
        }
        if (c.type == QLatin1String("picture.physics"))
            return QObject::tr("Movimento automático da imagem %1").arg(n);
        if (c.type == QLatin1String("picture.anchor"))
            return QObject::tr("Âncora da imagem %1: %2").arg(n).arg(
                core::pictureAnchorLabel(core::pictureAnchorFromId(
                    p.value(QStringLiteral("anchor")).toString())));
        if (c.type == QLatin1String("picture.effects")) {
            const core::PictureEffects fx = core::PictureEffects::fromParams(
                p.value(QStringLiteral("fx")).toMap());
            QStringList ligados;
            if (fx.border.enabled)  ligados << QObject::tr("borda");
            if (fx.glow.enabled)    ligados << QObject::tr("brilho");
            if (fx.blink.enabled)   ligados << QObject::tr("piscar");
            if (fx.tone.enabled)    ligados << QObject::tr("tonalidade");
            if (fx.tint.enabled)    ligados << QObject::tr("tint legado");
            if (fx.negative.enabled) ligados << QObject::tr("negative");
            if (fx.distort.enabled) ligados << QObject::tr("onda");
            if (fx.shine.enabled)   ligados << QObject::tr("brilho deslizante");
            if (fx.mask.enabled)    ligados << QObject::tr("máscara");
            return QObject::tr("Efeitos da imagem %1: %2")
                .arg(n).arg(ligados.isEmpty() ? QObject::tr("(nenhum)")
                                              : ligados.join(QStringLiteral(", ")));
        }
        if (c.type == QLatin1String("picture.negative"))
            return QObject::tr("Negativo da imagem %1: %2% · %3 quadros").arg(n).arg(qRound(p.value(QStringLiteral("strength"),1.0).toDouble()*100.0)).arg(p.value(QStringLiteral("duration"),0).toInt());
        if (c.type == QLatin1String("picture.flip"))
            return QObject::tr("Virar imagem %1: H %2 · V %3")
                .arg(n).arg(p.value(QStringLiteral("flipH")).toBool() ? QObject::tr("sim") : QObject::tr("não"))
                .arg(p.value(QStringLiteral("flipV")).toBool() ? QObject::tr("sim") : QObject::tr("não"));
        if (c.type == QLatin1String("picture.display"))
            return QObject::tr("Configurações de exibição da imagem %1: %2 / camada %3")
                .arg(n)
                .arg(core::pictureSpaceLabel(core::pictureSpaceFromId(p.value(QStringLiteral("space")).toString())))
                .arg(core::pictureLayerLabel(core::pictureLayerFromId(p.value(QStringLiteral("layer")).toString())));
        if (c.type == QLatin1String("picture.clearEffects"))
            return QObject::tr("Limpar efeitos da imagem %1").arg(n);
        if (c.type == QLatin1String("picture.transitionOut"))
            return QObject::tr("Sumir com a imagem %1: %2 em %3 quadros")
                .arg(n).arg(core::pictureTransitionLabel(core::pictureTransitionFromId(
                              p.value(QStringLiteral("transition")).toString())))
                .arg(p.value(QStringLiteral("duration"), 30).toInt());
        if (c.type == QLatin1String("picture.wait"))
            return n <= 0 ? QObject::tr("Esperar as animações de imagem")
                          : QObject::tr("Esperar a imagem %1").arg(n);
    }
    if (c.type.startsWith(QLatin1String("fog."))) {
        const int slot=p.value(QStringLiteral("slot"),1).toInt();
        if(c.type==QLatin1String("fog.show"))return QObject::tr("Mostrar névoa %1 no slot %2").arg(QFileInfo(p.value("source").toString()).fileName()).arg(slot);
        if(c.type==QLatin1String("fog.enable"))return QObject::tr("Ativar névoa padrão do slot %1").arg(slot);
        if(c.type==QLatin1String("fog.disable"))return QObject::tr("Desativar névoa do slot %1").arg(slot);
        if(c.type==QLatin1String("fog.remove"))return QObject::tr("Remover névoa do slot %1").arg(slot);
        if(c.type==QLatin1String("fog.opacity"))return QObject::tr("Névoa %1: opacidade %2 em %3 frames").arg(slot).arg(p.value("opacity").toInt()).arg(p.value("duration").toInt());
        if(c.type==QLatin1String("fog.blend"))return QObject::tr("Névoa %1: mistura %2").arg(slot).arg(core::fogBlendLabel(core::fogBlendFromId(p.value("blend").toString())));
        if(c.type==QLatin1String("fog.scroll"))return QObject::tr("Névoa %1: scroll (%2, %3)").arg(slot).arg(p.value("scrollX").toDouble()).arg(p.value("scrollY").toDouble());
        if(c.type==QLatin1String("fog.fadeIn"))return QObject::tr("Fade in da névoa %1").arg(slot);
        if(c.type==QLatin1String("fog.fadeOut"))return QObject::tr("Fade out da névoa %1").arg(slot);
        if(c.type==QLatin1String("fog.clear"))return QObject::tr("Limpar todas as névoas");
    }
    if (c.type == QLatin1String("common.local.set")) {
        const QString id = p.value(QStringLiteral("id")).toString();
        QString name = id;
        for (const core::CommonEvent& ce : ed.commonEvents) {
            for (const core::CommonEventParameter& def : ce.parameters) if (def.id == id) name = def.name;
            for (const core::CommonEventLocal& def : ce.locals) if (def.id == id) name = def.name;
        }
        const QString op=p.value(QStringLiteral("op"),QStringLiteral("=")).toString();
        return QObject::tr("Mudar parâmetro/local %1 · %2").arg(name,op==QLatin1String("toggle")?QObject::tr("inverter"):op);
    }
    if (c.type == QLatin1String("common.return"))
        return QObject::tr("Retornar do Evento Comum");
    if (c.type == QLatin1String("common.reserve")) {
        const QString commonId=p.value(QStringLiteral("commonId")).toString();const int n=p.value(QStringLiteral("number"),0).toInt();
        const core::CommonEvent* ce=!commonId.isEmpty()?ed.commonEventById(commonId):ed.commonEventByNumber(n);
        QString text=QObject::tr("Reservar evento comum %1").arg(ce?QStringLiteral("%1 — %2").arg(ce->number).arg(ce->name):QString::number(n));
        const int priority=p.value(QStringLiteral("priority"),0).toInt();if(priority!=0)text+=QObject::tr(" · prioridade %1").arg(priority);
        return text;
    }
    if (c.type == QLatin1String("common.call")) {
        const QString commonId=p.value(QStringLiteral("commonId")).toString();
        const int n = p.value(QStringLiteral("number"), 0).toInt();
        const core::CommonEvent* ce = !commonId.isEmpty()?ed.commonEventById(commonId):ed.commonEventByNumber(n);
        QString text=QObject::tr("Chamar evento comum %1").arg(
            ce ? QStringLiteral("%1 — %2").arg(ce->number).arg(ce->name) : QString::number(n));
        const int argumentCount=p.value(QStringLiteral("arguments")).toMap().size();
        if(argumentCount>0)text+=QObject::tr(" · argumentos: %1").arg(argumentCount);
        const QVariantMap ret=p.value(QStringLiteral("returnTarget")).toMap();
        const QString kind=ret.value(QStringLiteral("target"),QStringLiteral("none")).toString();
        if(kind==QLatin1String("variable"))text+=QObject::tr(" · retorno → variável %1").arg(nomeVar(ret.value(QStringLiteral("id")).toInt()));
        else if(kind==QLatin1String("switch"))text+=QObject::tr(" · retorno → switch %1").arg(nomeSw(ret.value(QStringLiteral("id")).toInt()));
        else if(kind==QLatin1String("commonValue"))text+=QObject::tr(" · retorno → local");
        return text;
    }
    return QObject::tr("(comando %1)").arg(c.type);
}

static bool editMoveRouteControl(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    QDialog d(parent); d.setWindowTitle(QObject::tr("Controlar rota em andamento"));
    auto* v=new QVBoxLayout(&d);auto* form=new QFormLayout;v->addLayout(form);
    auto* target=new QComboBox(&d);target->addItem(QObject::tr("Este evento"),QStringLiteral("self"));target->addItem(QObject::tr("Jogador"),QStringLiteral("player"));
    if(const core::MapDoc* doc=ed.doc())for(const core::MapEvent& ev:doc->events)target->addItem(QObject::tr("Evento: %1").arg(ev.name),QStringLiteral("event:")+ev.id);
    target->setCurrentIndex(qMax(0,target->findData(cmd.params.value(QStringLiteral("target"),QStringLiteral("self")))));
    auto* action=new QComboBox(&d);action->addItem(QObject::tr("Pausar rota atual"),QStringLiteral("pause"));action->addItem(QObject::tr("Retomar rota pausada"),QStringLiteral("resume"));action->addItem(QObject::tr("Cancelar rota atual"),QStringLiteral("cancel"));action->addItem(QObject::tr("Limpar rotas na fila"),QStringLiteral("clearQueue"));
    action->setCurrentIndex(qMax(0,action->findData(cmd.params.value(QStringLiteral("action"),QStringLiteral("pause")))));
    form->addRow(QObject::tr("Alvo:"),target);form->addRow(QObject::tr("Ação:"),action);
    auto* note=new QLabel(QObject::tr("Cancelar interrompe a rota atual. Se houver outra esperando, ela começa em seguida. “Limpar fila” remove somente as rotas que ainda não começaram."),&d);note->setWordWrap(true);v->addWidget(note);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    if(d.exec()!=QDialog::Accepted)return false;
    cmd.params={{QStringLiteral("target"),target->currentData()},{QStringLiteral("action"),action->currentData()}};
    return true;
}

static bool editFogCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    if(cmd.type==QLatin1String("fog.clear")){cmd.params.clear();return true;}
    QDialog d(parent);d.setWindowTitle(QObject::tr("Comando de névoa"));auto* v=new QVBoxLayout(&d);auto* form=new QFormLayout;
    auto* slot=new QSpinBox(&d);slot->setRange(1,5);slot->setValue(cmd.params.value("slot",1).toInt());form->addRow(QObject::tr("Slot:"),slot);
    QLineEdit* source=nullptr;QComboBox* blend=nullptr;QSpinBox* opacity=nullptr;QSpinBox* duration=nullptr;QDoubleSpinBox* sx=nullptr;QDoubleSpinBox* sy=nullptr;QDoubleSpinBox* zoom=nullptr;QCheckBox* repeat=nullptr;QImage chosen;
    auto addDuration=[&](int def){duration=new QSpinBox(&d);duration->setRange(0,6000);duration->setValue(cmd.params.value("duration",def).toInt());duration->setSuffix(QObject::tr(" quadros"));form->addRow(QObject::tr("Duração:"),duration);};
    if(cmd.type==QLatin1String("fog.show")){
        auto* row=new QWidget(&d);auto* h=new QHBoxLayout(row);h->setContentsMargins(0,0,0,0);source=new QLineEdit(cmd.params.value("source").toString(),row);source->setReadOnly(true);auto* b=new QPushButton(QObject::tr("Escolher…"),row);h->addWidget(source,1);h->addWidget(b);form->addRow(QObject::tr("Imagem:"),row);
        QObject::connect(b,&QPushButton::clicked,&d,[&]{QString p=AssetBrowserDialog::chooseImage(ed,&d,QStringLiteral("Fogs"));if(!p.isEmpty()){chosen=QImage(p);source->setText(ed.projectRelativePath(p));}});
        blend=new QComboBox(&d);for(core::FogBlend x:{core::FogBlend::Normal,core::FogBlend::Add,core::FogBlend::Multiply,core::FogBlend::Screen,core::FogBlend::Overlay})blend->addItem(core::fogBlendLabel(x),core::fogBlendId(x));blend->setCurrentIndex(qMax(0,blend->findData(cmd.params.value("blend","normal"))));form->addRow(QObject::tr("Mistura:"),blend);
        opacity=new QSpinBox(&d);opacity->setRange(0,255);opacity->setValue(cmd.params.value("opacity",180).toInt());form->addRow(QObject::tr("Opacidade:"),opacity);
        auto dec=[&](double val,double lo,double hi){auto* s=new QDoubleSpinBox(&d);s->setRange(lo,hi);s->setDecimals(2);s->setValue(val);return s;};sx=dec(cmd.params.value("scrollX").toDouble(),-100,100);sy=dec(cmd.params.value("scrollY").toDouble(),-100,100);zoom=dec(cmd.params.value("zoom",1.0).toDouble(),.1,5);form->addRow(QObject::tr("Scroll X:"),sx);form->addRow(QObject::tr("Scroll Y:"),sy);form->addRow(QObject::tr("Aproximação:"),zoom);repeat=new QCheckBox(QObject::tr("Repetir imagem"),&d);repeat->setChecked(cmd.params.value("tileRepeat",true).toBool());form->addRow(repeat);addDuration(0);
    }else if(cmd.type==QLatin1String("fog.opacity")||cmd.type==QLatin1String("fog.fadeIn")){
        opacity=new QSpinBox(&d);opacity->setRange(0,255);opacity->setValue(cmd.params.value("opacity",255).toInt());form->addRow(QObject::tr("Opacidade final:"),opacity);addDuration(60);
    }else if(cmd.type==QLatin1String("fog.remove")||cmd.type==QLatin1String("fog.fadeOut"))addDuration(cmd.type==QLatin1String("fog.fadeOut")?60:0);
    else if(cmd.type==QLatin1String("fog.blend")){blend=new QComboBox(&d);for(core::FogBlend x:{core::FogBlend::Normal,core::FogBlend::Add,core::FogBlend::Multiply,core::FogBlend::Screen,core::FogBlend::Overlay})blend->addItem(core::fogBlendLabel(x),core::fogBlendId(x));blend->setCurrentIndex(qMax(0,blend->findData(cmd.params.value("blend","normal"))));form->addRow(QObject::tr("Mistura:"),blend);}
    else if(cmd.type==QLatin1String("fog.scroll")){sx=new QDoubleSpinBox(&d);sy=new QDoubleSpinBox(&d);for(auto* s:{sx,sy}){s->setRange(-100,100);s->setDecimals(2);}sx->setValue(cmd.params.value("scrollX").toDouble());sy->setValue(cmd.params.value("scrollY").toDouble());form->addRow(QObject::tr("Scroll X:"),sx);form->addRow(QObject::tr("Scroll Y:"),sy);}
    v->addLayout(form);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;
    cmd.params["slot"]=slot->value();if(source){if(source->text().isEmpty())return false;cmd.params["source"]=source->text();if(!chosen.isNull())cmd.params["image"]=core::io::imageToDataUri(chosen);}
    if(blend)cmd.params["blend"]=blend->currentData();if(opacity)cmd.params["opacity"]=opacity->value();if(duration&&cmd.type!=QLatin1String("fog.show"))cmd.params["duration"]=duration->value();if(sx)cmd.params["scrollX"]=sx->value();if(sy)cmd.params["scrollY"]=sy->value();if(zoom)cmd.params["zoom"]=zoom->value();if(repeat)cmd.params["tileRepeat"]=repeat->isChecked();if(cmd.type==QLatin1String("fog.show"))cmd.params["fadeIn"]=duration?duration->value():0;return true;
}

static bool editLocalizationCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    if (!ed.localization.enabled) {
        const auto answer = QMessageBox::question(
            parent, QObject::tr("Localização"),
            QObject::tr("A localização está desativada neste projeto. Deseja ativá-la para usar este comando?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return false;
        ed.localization.enabled = true;
        ed.localization.ensureDefaults();
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Alterar idioma"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* localeCombo = new QComboBox(&dialog);
    for (const core::LocalizationLocale& locale : ed.localization.locales) {
        if (!locale.enabled || locale.code.trimmed().isEmpty()) continue;
        localeCombo->addItem(QStringLiteral("%1 (%2)").arg(locale.name, locale.code), locale.code);
    }
    if (localeCombo->count() == 0) {
        QMessageBox::information(parent, QObject::tr("Localização"),
                                 QObject::tr("Adicione ao menos um idioma ativo em Localização / Idiomas antes de usar este comando."));
        return false;
    }
    const QString current = cmd.params.value(QStringLiteral("locale"), ed.localization.defaultLocale).toString();
    int index = localeCombo->findData(current);
    if (index < 0) index = localeCombo->findData(ed.localization.defaultLocale);
    localeCombo->setCurrentIndex(qMax(0, index));
    form->addRow(QObject::tr("Idioma:"), localeCombo);
    layout->addLayout(form);
    auto* hint = new QLabel(QObject::tr(
        "A mudança é persistida nas preferências do jogador. Textos localizados passam a usar o novo idioma imediatamente; "
        "se uma tradução faltar, a LUDO usa o fallback/texto original."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    cmd.params = {{QStringLiteral("locale"), localeCombo->currentData().toString()}};
    return true;
}

static bool editGameCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent,
                            const core::CommonEvent* commonContext = nullptr)
{
    QDialog dialog(parent);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    QComboBox* map = nullptr;
    std::shared_ptr<CommonSourceEditorState> transferX;
    std::shared_ptr<CommonSourceEditorState> transferY;
    QComboBox* direction = nullptr;
    std::shared_ptr<CommonSourceEditorState> transferFade;
    QSpinBox* slot = nullptr;
    QComboBox* troop = nullptr;
    QCheckBox* allowEscape = nullptr;
    QComboBox* resultVariable = nullptr;
    QListWidget* shopItems = nullptr;
    QCheckBox* purchaseOnly = nullptr;
    std::shared_ptr<CommonSourceEditorState> innCost;
    QCheckBox* innRemoveStates = nullptr;
    QComboBox* gameScreen = nullptr;

    if (cmd.type == QLatin1String("game.ui.open")) {
        dialog.setWindowTitle(QObject::tr("Abrir tela da interface"));
        gameScreen = new QComboBox(&dialog);
        gameScreen->addItem(QObject::tr("Menu principal"), QStringLiteral("main"));
        gameScreen->addItem(QObject::tr("Itens / Inventário"), QStringLiteral("inventory"));
        gameScreen->addItem(QObject::tr("Status"), QStringLiteral("status"));
        gameScreen->addItem(QObject::tr("Equipamentos"), QStringLiteral("equipment"));
        gameScreen->addItem(QObject::tr("Habilidades"), QStringLiteral("skills"));
        gameScreen->addItem(QObject::tr("Missões"), QStringLiteral("quests"));
        gameScreen->addItem(QObject::tr("Salvar"), QStringLiteral("save"));
        gameScreen->addItem(QObject::tr("Carregar"), QStringLiteral("load"));
        gameScreen->addItem(QObject::tr("Configurações"), QStringLiteral("settings"));
        gameScreen->setCurrentIndex(qMax(0, gameScreen->findData(cmd.params.value(QStringLiteral("screenId"), QStringLiteral("main")))));
        form->addRow(QObject::tr("Tela:"), gameScreen);
        auto* hint=new QLabel(QObject::tr("Abre uma das telas prontas da interface do jogo. Ela usa a mesma aparência e organização definidas nas Configurações do Jogo."),&dialog);
        hint->setWordWrap(true); layout->addWidget(hint);
    } else if (cmd.type == QLatin1String("game.ui.close") || cmd.type == QLatin1String("game.checkpoint") || cmd.type == QLatin1String("game.restoreCheckpoint") || cmd.type == QLatin1String("game.autosave") || cmd.type == QLatin1String("game.restart") || cmd.type == QLatin1String("game.gameOver") || cmd.type == QLatin1String("game.returnTitle")) {
        const QString title = cmd.type==QLatin1String("game.ui.close")?QObject::tr("Comando legado removido"):
                              cmd.type==QLatin1String("game.checkpoint")?QObject::tr("Criar Checkpoint"):
                              cmd.type==QLatin1String("game.restoreCheckpoint")?QObject::tr("Restaurar Checkpoint"):
                              cmd.type==QLatin1String("game.autosave")?QObject::tr("Autosave"):
                              cmd.type==QLatin1String("game.restart")?QObject::tr("Reiniciar partida"):
                              cmd.type==QLatin1String("game.gameOver")?QObject::tr("Tela de Game Over"):QObject::tr("Voltar à tela de título");
        dialog.setWindowTitle(title);
        auto* hint=new QLabel(QObject::tr("Este comando não precisa de parâmetros adicionais."),&dialog);hint->setWordWrap(true);layout->addWidget(hint);
    } else if (cmd.type == QLatin1String("map.transfer")) {
        dialog.setWindowTitle(QObject::tr("Mover jogador para outro mapa"));
        dialog.resize(760, 680);
        map = new QComboBox(&dialog);
        for (const core::MapDoc& doc : ed.docs) map->addItem(doc.name, doc.id);
        map->setCurrentIndex(qMax(0, map->findData(cmd.params.value(QStringLiteral("mapId")).toString())));
        transferX=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,
            core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("x")),0),commonContext);
        transferY=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,
            core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("y")),0),commonContext);
        direction = new QComboBox(&dialog);
        const QStringList labels = {QObject::tr("Baixo"), QObject::tr("Esquerda"),
                                    QObject::tr("Direita"), QObject::tr("Cima"),
                                    QObject::tr("Baixo-esquerda"), QObject::tr("Baixo-direita"),
                                    QObject::tr("Cima-esquerda"), QObject::tr("Cima-direita")};
        for (int i = 0; i < labels.size(); ++i) direction->addItem(labels[i], i);
        direction->setCurrentIndex(qMax(0, direction->findData(
            cmd.params.value(QStringLiteral("direction"), 0).toInt())));
        transferFade=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,
            core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("fadeFrames")),18),commonContext);
        form->addRow(QObject::tr("Mapa"), map);
        form->addRow(QObject::tr("Posição X"), transferX->widget);
        form->addRow(QObject::tr("Posição Y"), transferY->widget);
        form->addRow(QObject::tr("Direção ao chegar"), direction);
        form->addRow(QObject::tr("Duração da transição (quadros)"),transferFade->widget);

        auto* locationLabel = new QLabel(&dialog);
        locationLabel->setStyleSheet(QStringLiteral("font-weight:600;color:#dfe8f5"));
        form->addRow(QObject::tr("Chegada"), locationLabel);

        layout->removeItem(form);
        layout->insertLayout(0, form);
        auto* hint = new QLabel(QObject::tr(
            "Você pode informar a posição diretamente ou usar valores dinâmicos. Quando X e Y forem fixos, a prévia permite escolher o destino clicando no mapa."),
            &dialog);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
        layout->insertWidget(1, hint);

        auto* preview = new MapDestinationPreview(ed, &dialog);
        auto* previewDialog = new CommandPreviewDialog(QObject::tr("Prévia do destino"), preview, &dialog);
        auto* previewButton = new QPushButton(QObject::tr("Ver prévia"), &dialog);
        previewButton->setToolTip(QObject::tr("Abre o mapa de destino em uma janela separada."));
        layout->insertWidget(2, previewButton);
        QObject::connect(previewButton, &QPushButton::clicked, previewDialog, &CommandPreviewDialog::present);
        dialog.resize(760, 680);

        const auto isConstant=[](const std::shared_ptr<CommonSourceEditorState>& value){
            return value&&value->source&&value->source->currentData().toString()==QLatin1String("constant")&&value->number;
        };
        const auto updateCoordinates = [=, &ed] {
            const core::MapDoc* destination = ed.mapById(map->currentData().toString());
            if (destination) {
                if(transferX&&transferX->number)transferX->number->setRange(0, qMax(0, destination->map.width - 1));
                if(transferY&&transferY->number)transferY->number->setRange(0, qMax(0, destination->map.height - 1));
            }
            preview->setMapId(map->currentData().toString());
            if(isConstant(transferX)&&isConstant(transferY)){
                const QPoint requested(transferX->number->value(),transferY->number->value());
                preview->setSelection(requested);
                const QPoint selected=preview->selection();
                transferX->number->setValue(selected.x());transferY->number->setValue(selected.y());
                locationLabel->setText(QObject::tr("Posição (%1, %2)").arg(selected.x()).arg(selected.y()));
            }else{
                locationLabel->setText(QObject::tr("Posição dinâmica — o valor será definido durante o jogo"));
            }
        };
        preview->onCellPicked = [=](const QPoint& cell) {
            const int constantX=transferX->source->findData(QStringLiteral("constant"));if(constantX>=0)transferX->source->setCurrentIndex(constantX);
            const int constantY=transferY->source->findData(QStringLiteral("constant"));if(constantY>=0)transferY->source->setCurrentIndex(constantY);
            if(transferX->number)transferX->number->setValue(cell.x());if(transferY->number)transferY->number->setValue(cell.y());
            locationLabel->setText(QObject::tr("Posição (%1, %2)").arg(cell.x()).arg(cell.y()));
        };
        QObject::connect(map, &QComboBox::currentIndexChanged, &dialog,[=](int) { updateCoordinates(); });
        QObject::connect(transferX->source,&QComboBox::currentIndexChanged,&dialog,[=](int){updateCoordinates();});
        QObject::connect(transferY->source,&QComboBox::currentIndexChanged,&dialog,[=](int){updateCoordinates();});
        if(transferX->number)QObject::connect(transferX->number,&QSpinBox::valueChanged,&dialog,[=](int){updateCoordinates();});
        if(transferY->number)QObject::connect(transferY->number,&QSpinBox::valueChanged,&dialog,[=](int){updateCoordinates();});
        updateCoordinates();
    } else if (cmd.type == QLatin1String("shop.inn")) {
        dialog.setWindowTitle(QObject::tr("Abrir pousada"));
        innCost=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("cost")),50),commonContext);
        innRemoveStates = new QCheckBox(QObject::tr("Remover estados negativos ao descansar"), &dialog);
        innRemoveStates->setChecked(cmd.params.value(QStringLiteral("removeStates"), true).toBool());
        form->addRow(QObject::tr("Preço:"), innCost->widget);
        form->addRow(innRemoveStates);
    } else if (cmd.type == QLatin1String("shop.open")) {
        dialog.setWindowTitle(QObject::tr("Abrir loja"));
        dialog.resize(620, 620);
        shopItems = new QListWidget(&dialog);
        shopItems->setSelectionMode(QAbstractItemView::NoSelection);
        QStringList selected = cmd.params.value(QStringLiteral("itemIds")).toStringList();
        if (selected.isEmpty())
            for (const QVariant& value : cmd.params.value(QStringLiteral("itemIds")).toList())
                selected.push_back(value.toString());
        for (const QString& category : {QStringLiteral("items"), QStringLiteral("weapons"),
                                        QStringLiteral("armors")}) {
            for (const core::DatabaseRecord& record : ed.database.value(category)) {
                auto* item = new QListWidgetItem(
                    QStringLiteral("%1 — %2 (%3 G)")
                        .arg(core::databaseCategoryLabel(category), record.name)
                        .arg(record.data.value(QStringLiteral("price"), 0).toInt()), shopItems);
                item->setData(Qt::UserRole, record.id);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(record.id) ? Qt::Checked : Qt::Unchecked);
            }
        }
        purchaseOnly = new QCheckBox(QObject::tr("Somente comprar (não permitir venda)"), &dialog);
        purchaseOnly->setChecked(cmd.params.value(QStringLiteral("purchaseOnly"), false).toBool());
        form->addRow(QObject::tr("Produtos"), shopItems);
        form->addRow(purchaseOnly);
        auto* hint = new QLabel(QObject::tr("Marque os produtos. Nome e preço vêm do Banco de Dados; nenhum ID precisa ser digitado."), &dialog);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
        layout->addWidget(hint);
    } else if (cmd.type == QLatin1String("battle.start")) {
        dialog.setWindowTitle(QObject::tr("Iniciar batalha"));
        troop = new QComboBox(&dialog);
        for (const core::DatabaseRecord& record : ed.database.value(QStringLiteral("troops")))
            troop->addItem(record.name.isEmpty() ? QObject::tr("Tropa %1").arg(record.number) : record.name,
                           record.id);
        if (troop->count() == 0) {
            QMessageBox::information(parent, QObject::tr("Batalha"),
                                     QObject::tr("Crie uma tropa no Banco de Dados antes de adicionar este comando."));
            return false;
        }
        troop->setCurrentIndex(qMax(0, troop->findData(cmd.params.value(QStringLiteral("troopId")))));
        allowEscape = new QCheckBox(QObject::tr("Permitir fuga"), &dialog);
        allowEscape->setChecked(cmd.params.value(QStringLiteral("allowEscape"), true).toBool());
        resultVariable = new QComboBox(&dialog);
        resultVariable->addItem(QObject::tr("(não gravar resultado)"), 0);
        for (const core::VariableDef& variable : ed.variables)
            resultVariable->addItem(QStringLiteral("%1: %2").arg(variable.id).arg(variable.name), variable.id);
        resultVariable->setCurrentIndex(qMax(0, resultVariable->findData(
            cmd.params.value(QStringLiteral("resultVariable"), 0).toInt())));
        form->addRow(QObject::tr("Tropa:"), troop);
        form->addRow(allowEscape);
        form->addRow(QObject::tr("Resultado em variável:"), resultVariable);
        auto* hint = new QLabel(QObject::tr("Resultado: 0 = vitória, 1 = fuga, 2 = derrota."), &dialog);
        hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
        layout->addWidget(hint);
    } else {
        const bool saving = cmd.type == QLatin1String("game.save");
        dialog.setWindowTitle(saving ? QObject::tr("Salvar partida")
                                     : QObject::tr("Carregar partida"));
        slot = new QSpinBox(&dialog);
        slot->setRange(1, 99);
        slot->setValue(cmd.params.value(QStringLiteral("slot"), 1).toInt());
        form->addRow(QObject::tr("Slot"), slot);
        auto* hint = new QLabel(saving
            ? QObject::tr("O arquivo é gravado de forma atômica; um erro de energia não deixa um save pela metade.")
            : QObject::tr("Mapa, posição, direção, interruptores, variáveis, ouro e inventário serão restaurados."),
            &dialog);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
        layout->addWidget(hint);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    if (cmd.type == QLatin1String("game.ui.open")) {
        cmd.params = {{QStringLiteral("screenId"), gameScreen->currentData().toString()},
                      {QStringLiteral("wait"), true}};
    } else if (cmd.type == QLatin1String("game.ui.close") || cmd.type == QLatin1String("game.checkpoint") || cmd.type == QLatin1String("game.restoreCheckpoint") || cmd.type == QLatin1String("game.autosave") || cmd.type == QLatin1String("game.restart") || cmd.type == QLatin1String("game.gameOver") || cmd.type == QLatin1String("game.returnTitle")) {
        cmd.params.clear();
    } else if (cmd.type == QLatin1String("map.transfer")) {
        cmd.params = {{QStringLiteral("mapId"), map->currentData()},
                      {QStringLiteral("useSpawn"), false},
                      {QStringLiteral("x"), transferX->value()},
                      {QStringLiteral("y"), transferY->value()},
                      {QStringLiteral("direction"), direction->currentData()},
                      {QStringLiteral("fadeFrames"),transferFade->value()}};
    } else if (cmd.type == QLatin1String("shop.inn")) {
        cmd.params = {{QStringLiteral("cost"), innCost->value()},
                      {QStringLiteral("removeStates"), innRemoveStates->isChecked()}};
    } else if (cmd.type == QLatin1String("shop.open")) {
        QStringList itemIds;
        for (int row = 0; row < shopItems->count(); ++row)
            if (shopItems->item(row)->checkState() == Qt::Checked)
                itemIds.push_back(shopItems->item(row)->data(Qt::UserRole).toString());
        cmd.params = {{QStringLiteral("itemIds"), itemIds},
                      {QStringLiteral("purchaseOnly"), purchaseOnly->isChecked()}};
    } else if (cmd.type == QLatin1String("battle.start")) {
        cmd.params = {{QStringLiteral("troopId"), troop->currentData()},
                      {QStringLiteral("allowEscape"), allowEscape->isChecked()},
                      {QStringLiteral("resultVariable"), resultVariable->currentData()}};
    } else {
        cmd.params = {{QStringLiteral("slot"), slot->value()}};
    }
    return true;
}

static bool editWeatherCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    // O editor do comando usa o MESMO WeatherState das Propriedades do Mapa
    // e do runtime. Assim ids legados/defaults/limites não divergem por UI.
    WeatherState initial;
    initial.setConfig(cmd.params.value(QStringLiteral("type"), QStringLiteral("rain")).toString(),
                      cmd.params.value(QStringLiteral("intensity"), 50).toInt(),
                      cmd.params.value(QStringLiteral("thunderSe")).toString(),
                      cmd.params.value(QStringLiteral("thunderVolume"), 90).toInt(), true);

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Alterar clima"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* type = new QComboBox(&dialog);
    type->addItem(QObject::tr("Sem clima"), QStringLiteral("none"));
    type->addItem(QObject::tr("Chuva"), QStringLiteral("rain"));
    type->addItem(QObject::tr("Neve"), QStringLiteral("snow"));
    type->addItem(QObject::tr("Tempestade"), QStringLiteral("storm"));
    type->setCurrentIndex(qMax(0, type->findData(initial.typeId())));
    auto* intensity = new QSpinBox(&dialog);
    intensity->setRange(1, 100);
    intensity->setSuffix(QStringLiteral("%"));
    intensity->setValue(initial.active() ? initial.intensity : 50);
    form->addRow(QObject::tr("Tipo:"), type);
    form->addRow(QObject::tr("Intensidade:"), intensity);
    auto* thunderRow = new QWidget(&dialog);
    auto* thunderLayout = new QHBoxLayout(thunderRow);
    thunderLayout->setContentsMargins(0, 0, 0, 0);
    auto* thunderSource = new QLineEdit(initial.thunderSePath, thunderRow);
    thunderSource->setReadOnly(true);
    auto* thunderVolume = new QSpinBox(thunderRow);
    thunderVolume->setRange(0, 100);
    thunderVolume->setSuffix(QStringLiteral("%"));
    thunderVolume->setValue(initial.thunderVolume);
    auto* chooseThunder = new QPushButton(QObject::tr("Escolher e ouvir…"), thunderRow);
    auto* clearThunder = new QPushButton(QObject::tr("×"), thunderRow);
    clearThunder->setFixedWidth(30);
    thunderLayout->addWidget(thunderSource, 1);
    thunderLayout->addWidget(thunderVolume);
    thunderLayout->addWidget(chooseThunder);
    thunderLayout->addWidget(clearThunder);
    form->addRow(QObject::tr("SE do trovão:"), thunderRow);
    auto updateThunderVisibility = [form, thunderRow](const QString& value) {
        form->setRowVisible(thunderRow, value == QLatin1String("storm"));
    };
    updateThunderVisibility(type->currentData().toString());
    QObject::connect(type, &QComboBox::currentIndexChanged, &dialog,
                     [type, updateThunderVisibility](int) {
        updateThunderVisibility(type->currentData().toString());
    });
    QObject::connect(chooseThunder, &QPushButton::clicked, &dialog,
                     [&ed, thunderSource, thunderVolume, &dialog] {
        QString source = thunderSource->text();
        int volume = thunderVolume->value();
        if (chooseGameAudio(ed, &dialog, QObject::tr("Escolher SE do trovão"),
                            source, volume, QStringLiteral("SE"))) {
            thunderSource->setText(source);
            thunderVolume->setValue(volume);
        }
    });
    QObject::connect(clearThunder, &QPushButton::clicked, thunderSource, &QLineEdit::clear);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    WeatherState chosen;
    chosen.setConfig(type->currentData().toString(), intensity->value(),
                     thunderSource->text(), thunderVolume->value(), true);
    cmd.params = {{QStringLiteral("type"), chosen.typeId()},
                  {QStringLiteral("intensity"), chosen.intensity},
                  {QStringLiteral("thunderSe"), chosen.thunderSePath},
                  {QStringLiteral("thunderVolume"), chosen.thunderVolume}};
    return true;
}

static bool editQuestCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent,
                             const core::CommonEvent* commonContext = nullptr)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Comando de missão"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* quest = new QComboBox(&dialog);
    for (const core::DatabaseRecord& record : ed.database.value(QStringLiteral("quests")))
        quest->addItem(record.name.isEmpty() ? QObject::tr("Missão %1").arg(record.number) : record.name,
                       record.id);
    if (quest->count() == 0) {
        QMessageBox::information(parent, QObject::tr("Missões"),
                                 QObject::tr("Crie uma missão no Banco de Dados primeiro."));
        return false;
    }
    quest->setCurrentIndex(qMax(0, quest->findData(cmd.params.value(QStringLiteral("questId")))));
    form->addRow(QObject::tr("Missão:"), quest);
    std::shared_ptr<CommonSourceEditorState> target;
    std::shared_ptr<CommonSourceEditorState> amount;
    QComboBox* operation = nullptr;
    if (cmd.type == QLatin1String("quest.start")) {
        target=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("target")),1),commonContext);
        form->addRow(QObject::tr("Meta de progresso:"),target->widget);
    } else if (cmd.type == QLatin1String("quest.progress")) {
        operation = new QComboBox(&dialog);
        operation->addItem(QObject::tr("Somar ao progresso"),QStringLiteral("add"));
        operation->addItem(QObject::tr("Definir progresso"),QStringLiteral("set"));
        operation->setCurrentIndex(qMax(0,operation->findData(
            cmd.params.value(QStringLiteral("operation"),QStringLiteral("add")))));
        amount=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("amount")),1),commonContext);
        form->addRow(QObject::tr("Operação:"),operation);form->addRow(QObject::tr("Valor:"),amount->widget);
    }
    layout->addLayout(form);
    auto* hint = new QLabel(QObject::tr("O diário de missões é salvo automaticamente junto da partida."), &dialog);
    hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("color:#999"));layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return false;
    cmd.params={{QStringLiteral("questId"),quest->currentData()}};
    if(target)cmd.params[QStringLiteral("target")]=target->value();
    if(operation){cmd.params[QStringLiteral("operation")]=operation->currentData();cmd.params[QStringLiteral("amount")]=amount->value();}
    return true;
}

static bool editPluginCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    const NoCodePlugin* plugin = noCodePluginById(ed.plugins, cmd.params.value(QStringLiteral("pluginId")).toString());
    const PluginCommand* definition = plugin
        ? pluginCommandById(*plugin, cmd.params.value(QStringLiteral("commandId")).toString()) : nullptr;
    if (!plugin || !definition) {
        QMessageBox::warning(parent, QObject::tr("Extensão"),
                             QObject::tr("O pacote ou comando desta extensão não existe mais."));
        return false;
    }
    QDialog dialog(parent);dialog.setWindowTitle(QStringLiteral("%1 — %2").arg(plugin->name,definition->name));
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    const QVariantMap current=cmd.params.value(QStringLiteral("arguments")).toMap();
    QVector<QPair<PluginField,QWidget*>> widgets;
    for(const PluginField& field:definition->fields){
        const QVariant value=current.contains(field.id)?current.value(field.id):field.defaultValue;QWidget* widget=nullptr;
        if(field.type==QLatin1String("integer")){auto* spin=new QSpinBox(&dialog);spin->setRange(field.minimum,field.maximum);spin->setValue(value.toInt());widget=spin;}
        else if(field.type==QLatin1String("boolean")){auto* check=new QCheckBox(&dialog);check->setChecked(value.toBool());widget=check;}
        else if(field.type==QLatin1String("switch")){auto* combo=comboInterruptores(&dialog,ed,value.toInt());widget=combo;}
        else if(field.type==QLatin1String("variable")){auto* combo=comboVariaveis(&dialog,ed,value.toInt());widget=combo;}
        else if(field.type==QLatin1String("map")){auto* combo=new QComboBox(&dialog);for(const MapDoc& map:ed.docs)combo->addItem(map.name,map.id);combo->setCurrentIndex(qMax(0,combo->findData(value)));widget=combo;}
        else if(field.type==QLatin1String("actor")||field.type==QLatin1String("troop")||field.type==QLatin1String("quest")||field.type==QLatin1String("item")||field.type==QLatin1String("state")||field.type==QLatin1String("skill")||field.type==QLatin1String("animation")){
            auto* combo=new QComboBox(&dialog);QStringList categories;
            if(field.type==QLatin1String("actor"))categories={QStringLiteral("actors")};else if(field.type==QLatin1String("troop"))categories={QStringLiteral("troops")};else if(field.type==QLatin1String("quest"))categories={QStringLiteral("quests")};else if(field.type==QLatin1String("state"))categories={QStringLiteral("states")};else if(field.type==QLatin1String("skill"))categories={QStringLiteral("skills")};else if(field.type==QLatin1String("animation"))categories={QStringLiteral("animations")};else categories={QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")};
            for(const QString& category:categories)for(const DatabaseRecord& record:ed.database.value(category))combo->addItem(categories.size()>1?QStringLiteral("%1 — %2").arg(databaseCategoryLabel(category),record.name):record.name,record.id);
            combo->setCurrentIndex(qMax(0,combo->findData(value)));widget=combo;
        }else if(field.type==QLatin1String("select")&&!field.options.isEmpty()){auto* combo=new QComboBox(&dialog);for(const QString& option:field.options)combo->addItem(option,option);combo->setCurrentIndex(qMax(0,combo->findData(value)));widget=combo;}
        else{auto* line=new QLineEdit(value.toString(),&dialog);widget=line;}
        form->addRow(field.label+QLatin1Char(':'),widget);widgets.push_back({field,widget});
    }
    if(!definition->description.isEmpty()){auto* hint=new QLabel(definition->description,&dialog);hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("color:#999"));layout->addWidget(hint);}
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return false;
    QVariantMap arguments;
    for(const auto& pair:widgets){const PluginField& field=pair.first;QWidget* widget=pair.second;QVariant value;
        if(auto* spin=qobject_cast<QSpinBox*>(widget))value=spin->value();else if(auto* check=qobject_cast<QCheckBox*>(widget))value=check->isChecked();else if(auto* combo=qobject_cast<QComboBox*>(widget))value=combo->currentData();else if(auto* line=qobject_cast<QLineEdit*>(widget))value=line->text();arguments[field.id]=value;}
    QVariantMap normalized;QStringList contractErrors;
    if(!normalizePluginArguments(*definition,arguments,&normalized,&contractErrors)){
        QMessageBox::warning(parent,QObject::tr("Contrato da extensão"),contractErrors.join(QLatin1Char('\n')));
        return false;
    }
    cmd.params[QStringLiteral("arguments")]=normalized;return true;
}

static bool editRpgCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent,
                           const core::CommonEvent* commonContext = nullptr)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Comando de RPG"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    QComboBox* actor = nullptr;
    QComboBox* operation = nullptr;
    QComboBox* item = nullptr;
    QComboBox* equipSlot = nullptr;
    QComboBox* state = nullptr;
    std::shared_ptr<CommonSourceEditorState> amount;
    auto makeActors = [&](bool allowAll) {
        auto* combo = new QComboBox(&dialog);
        if (allowAll) combo->addItem(QObject::tr("Todo o grupo"), QString());
        for (const DatabaseRecord& record : ed.database.value(QStringLiteral("actors")))
            combo->addItem(record.name, record.id);
        return combo;
    };
    auto makeOperation = [&] {
        auto* combo = new QComboBox(&dialog);
        combo->addItem(QObject::tr("Adicionar"), QStringLiteral("add"));
        combo->addItem(QObject::tr("Remover / subtrair"), QStringLiteral("subtract"));
        combo->addItem(QObject::tr("Definir valor"), QStringLiteral("set"));
        return combo;
    };
    if (cmd.type == QLatin1String("party.change")) {
        actor = makeActors(false);
        operation = new QComboBox(&dialog);
        operation->addItem(QObject::tr("Adicionar ao grupo"), QStringLiteral("add"));
        operation->addItem(QObject::tr("Remover do grupo"), QStringLiteral("remove"));
        amount=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("level")),1),commonContext);
        form->addRow(QObject::tr("Personagem:"),actor);form->addRow(QObject::tr("Operação:"),operation);form->addRow(QObject::tr("Nível ao adicionar:"),amount->widget);
    } else if (cmd.type == QLatin1String("party.gold")) {
        operation=makeOperation();amount=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("amount")),1),commonContext);
        form->addRow(QObject::tr("Operação:"),operation);form->addRow(QObject::tr("Quantidade:"),amount->widget);
    } else if (cmd.type == QLatin1String("inventory.change")) {
        item=new QComboBox(&dialog);
        for(const QString& category:{QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")})
            for(const DatabaseRecord& record:ed.database.value(category))item->addItem(QStringLiteral("%1 — %2").arg(databaseCategoryLabel(category),record.name),record.id);
        operation=makeOperation();amount=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("amount")),1),commonContext);
        form->addRow(QObject::tr("Item:"),item);form->addRow(QObject::tr("Operação:"),operation);form->addRow(QObject::tr("Quantidade:"),amount->widget);
    } else if (cmd.type == QLatin1String("actor.equip")) {
        actor=makeActors(false);equipSlot=new QComboBox(&dialog);equipSlot->addItem(QObject::tr("Arma"),QStringLiteral("weapon"));equipSlot->addItem(QObject::tr("Armadura"),QStringLiteral("armor"));equipSlot->addItem(QObject::tr("Acessório"),QStringLiteral("accessory"));
        item=new QComboBox(&dialog);item->addItem(QObject::tr("(remover equipamento)"),QString());for(const DatabaseRecord& record:ed.database.value(QStringLiteral("weapons")))item->addItem(QObject::tr("Arma — %1").arg(record.name),record.id);for(const DatabaseRecord& record:ed.database.value(QStringLiteral("armors")))item->addItem(QObject::tr("Proteção — %1").arg(record.name),record.id);
        form->addRow(QObject::tr("Personagem:"),actor);form->addRow(QObject::tr("Espaço:"),equipSlot);form->addRow(QObject::tr("Equipamento:"),item);
    } else if(cmd.type==QLatin1String("actor.state")){
        actor=makeActors(true);operation=new QComboBox(&dialog);operation->addItem(QObject::tr("Aplicar"),QStringLiteral("add"));operation->addItem(QObject::tr("Remover"),QStringLiteral("remove"));state=new QComboBox(&dialog);for(const DatabaseRecord& record:ed.database.value(QStringLiteral("states")))state->addItem(record.name,record.id);form->addRow(QObject::tr("Personagem:"),actor);form->addRow(QObject::tr("Operação:"),operation);form->addRow(QObject::tr("Estado:"),state);
    } else {
        actor=makeActors(true);operation=makeOperation();amount=makeCommonSourceEditor(&dialog,ed,core::CommonValueType::Number,core::valueSpecFromLegacy(cmd.params.value(QStringLiteral("amount")),1),commonContext);
        form->addRow(QObject::tr("Personagem:"),actor);form->addRow(QObject::tr("Operação:"),operation);form->addRow(QObject::tr("Quantidade:"),amount->widget);
    }
    if(actor)actor->setCurrentIndex(qMax(0,actor->findData(cmd.params.value(QStringLiteral("actorId")))));
    if(operation)operation->setCurrentIndex(qMax(0,operation->findData(cmd.params.value(QStringLiteral("operation"),QStringLiteral("add")))));
    if(item)item->setCurrentIndex(qMax(0,item->findData(cmd.params.value(QStringLiteral("itemId")))));
    if(equipSlot)equipSlot->setCurrentIndex(qMax(0,equipSlot->findData(cmd.params.value(QStringLiteral("slot"),QStringLiteral("weapon")))));
    if(state)state->setCurrentIndex(qMax(0,state->findData(cmd.params.value(QStringLiteral("stateId")))));
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return false;
    cmd.params.clear();
    if(actor)cmd.params[QStringLiteral("actorId")]=actor->currentData();
    if(operation)cmd.params[QStringLiteral("operation")]=operation->currentData();
    if(item)cmd.params[QStringLiteral("itemId")]=item->currentData();
    if(equipSlot)cmd.params[QStringLiteral("slot")]=equipSlot->currentData();
    if(state)cmd.params[QStringLiteral("stateId")]=state->currentData();
    if(amount){if(cmd.type==QLatin1String("party.change"))cmd.params[QStringLiteral("level")]=amount->value();else cmd.params[QStringLiteral("amount")]=amount->value();}
    return true;
}

static bool editAudioCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    if (cmd.type == QLatin1String("audio.footstep")) {
        QDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Tocar som de passo"));
        auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
        auto* surface=new QComboBox(&dialog);surface->addItem(QObject::tr("Automático pelo chão"),QString());
        for(const auto& s:ed.footstepSurfaces)surface->addItem(s.name,s.id);
        surface->setCurrentIndex(qMax(0,surface->findData(cmd.params.value(QStringLiteral("surfaceId")).toString())));
        auto* volume=new QSpinBox(&dialog);volume->setRange(0,100);volume->setSuffix(QStringLiteral("%"));volume->setValue(qBound(0,cmd.params.value(QStringLiteral("volume"),100).toInt(),100));
        form->addRow(QObject::tr("Superfície:"),surface);form->addRow(QObject::tr("Volume:"),volume);
        auto* note=new QLabel(QObject::tr("Em Automático, o som é escolhido pela superfície onde o personagem está. Você também pode escolher uma superfície específica para cutscenes e eventos."),&dialog);note->setWordWrap(true);layout->addWidget(note);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return false;cmd.params.clear();cmd.params[QStringLiteral("surfaceId")]=surface->currentData();cmd.params[QStringLiteral("volume")]=volume->value();return true;
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Áudio"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    QComboBox* channel = nullptr;
    QLineEdit* source = nullptr;
    QSpinBox* volume = nullptr;
    QCheckBox* loop = nullptr;
    QSpinBox* fadeIn = nullptr;
    QSpinBox* fadeOut = nullptr;
    QCheckBox* smoothTransition = nullptr;
    QSpinBox* transitionTime = nullptr;
    QSpinBox* pitch = nullptr;
    QSpinBox* pan = nullptr;

    if (cmd.type == QLatin1String("audio.stop")) {
        channel = new QComboBox(&dialog);
        channel->addItem(QObject::tr("Música de fundo (BGM)"), QStringLiteral("bgm"));
        channel->addItem(QObject::tr("Som ambiente (BGS)"), QStringLiteral("bgs"));
        channel->addItem(QObject::tr("Música curta (ME)"), QStringLiteral("me"));
        channel->addItem(QObject::tr("Efeito sonoro (SE)"), QStringLiteral("se"));
        channel->addItem(QObject::tr("Voz"), QStringLiteral("voice"));
        channel->setCurrentIndex(qMax(0, channel->findData(
            cmd.params.value(QStringLiteral("channel"), QStringLiteral("bgm")))));
        fadeOut = new QSpinBox(&dialog);
        fadeOut->setRange(0, 60000); fadeOut->setSuffix(QStringLiteral(" ms"));
        fadeOut->setSpecialValueText(QObject::tr("Sem fade"));
        fadeOut->setValue(qBound(0, cmd.params.value(QStringLiteral("fadeOutMs"), 0).toInt(), 60000));
        form->addRow(QObject::tr("Canal:"), channel);
        form->addRow(QObject::tr("Desaparecer em:"), fadeOut);
    } else {
        const QString channelType = cmd.type.mid(6);
        const QString type = channelType.toUpper();
        auto* row = new QWidget(&dialog);
        auto* horizontal = new QHBoxLayout(row);
        horizontal->setContentsMargins(0, 0, 0, 0);
        source = new QLineEdit(cmd.params.value(QStringLiteral("source")).toString(), row);
        source->setReadOnly(true);
        volume = new QSpinBox(&dialog);
        volume->setRange(0, 100);
        volume->setSuffix(QStringLiteral("%"));
        volume->setValue(qBound(0, cmd.params.value(QStringLiteral("volume"), 90).toInt(), 100));
        auto* choose = new QPushButton(QObject::tr("Escolher e ouvir…"), row);
        horizontal->addWidget(source, 1);
        horizontal->addWidget(choose);
        const QString pickerContext = channelType == QLatin1String("bgm") ? QStringLiteral("BGM")
                                    : channelType == QLatin1String("bgs") ? QStringLiteral("BGS")
                                    : channelType == QLatin1String("me") ? QStringLiteral("ME")
                                    : channelType == QLatin1String("voice") ? QStringLiteral("Voice")
                                    : QStringLiteral("SE");
        QObject::connect(choose, &QPushButton::clicked, &dialog,
                         [&ed, &dialog, source, volume, type, pickerContext] {
            QString selected = source->text();
            int selectedVolume = volume->value();
            if (chooseGameAudio(ed, &dialog,
                                QObject::tr("Escolher %1").arg(type),
                                selected, selectedVolume, pickerContext)) {
                source->setText(selected);
                volume->setValue(selectedVolume);
            }
        });
        const bool supportsLoop = channelType == QLatin1String("bgm") ||
                                  channelType == QLatin1String("bgs") ||
                                  channelType == QLatin1String("me");
        if (supportsLoop) {
            loop = new QCheckBox(QObject::tr("Repetir continuamente"), &dialog);
            loop->setChecked(cmd.params.value(
                QStringLiteral("loop"),
                channelType == QLatin1String("bgm") || channelType == QLatin1String("bgs")).toBool());
        }
        form->addRow(QObject::tr("Arquivo:"), row);
        form->addRow(QObject::tr("Volume:"), volume);
        pitch=new QSpinBox(&dialog);pitch->setRange(50,200);pitch->setSuffix(QStringLiteral("%"));pitch->setValue(qBound(50,cmd.params.value(QStringLiteral("pitch"),100).toInt(),200));
        pitch->setToolTip(QObject::tr("100% mantém o áudio original. Valores menores deixam o som mais grave e lento; valores maiores, mais agudo e rápido."));
        pan=new QSpinBox(&dialog);pan->setRange(-100,100);pan->setValue(qBound(-100,cmd.params.value(QStringLiteral("pan"),0).toInt(),100));pan->setPrefix(QObject::tr("E "));pan->setSuffix(QObject::tr(" D"));pan->setSpecialValueText(QObject::tr("Centro"));
        form->addRow(QObject::tr("Tom e velocidade:"),pitch);form->addRow(QObject::tr("Posição estéreo:"),pan);
        if (loop) form->addRow(loop);

        if (channelType != QLatin1String("se") && channelType != QLatin1String("voice")) {
            fadeIn = new QSpinBox(&dialog);
            fadeIn->setRange(0, 60000); fadeIn->setSuffix(QStringLiteral(" ms"));
            fadeIn->setSpecialValueText(QObject::tr("Sem fade"));
            fadeIn->setValue(qBound(0, cmd.params.value(QStringLiteral("fadeInMs"), 0).toInt(), 60000));
            form->addRow(QObject::tr("Aparecer em:"), fadeIn);
            if(channelType == QLatin1String("bgm") || channelType == QLatin1String("bgs")){
            smoothTransition = new QCheckBox(QObject::tr("Transição suave ao trocar a faixa"), &dialog);
            transitionTime = new QSpinBox(&dialog);
            transitionTime->setRange(50, 60000); transitionTime->setSuffix(QStringLiteral(" ms"));
            transitionTime->setValue(qBound(50, cmd.params.value(QStringLiteral("transitionMs"), 1000).toInt(), 60000));
            smoothTransition->setChecked(cmd.params.value(QStringLiteral("transitionMs"), 0).toInt() > 0);
            transitionTime->setEnabled(smoothTransition->isChecked());
            QObject::connect(smoothTransition, &QCheckBox::toggled, transitionTime, &QWidget::setEnabled);
            form->addRow(smoothTransition);
            form->addRow(QObject::tr("Duração da transição:"), transitionTime);
            auto* note = new QLabel(QObject::tr("A faixa atual diminui enquanto a nova começa a tocar, evitando uma troca brusca."), &dialog);
            note->setWordWrap(true); note->setStyleSheet(QStringLiteral("color:#999"));
            layout->addWidget(note);
            }
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    cmd.params.clear();
    if (channel) {
        cmd.params[QStringLiteral("channel")] = channel->currentData();
        cmd.params[QStringLiteral("fadeOutMs")] = fadeOut ? fadeOut->value() : 0;
    } else {
        if (source->text().isEmpty()) {
            QMessageBox::information(parent, QObject::tr("Áudio"),
                                     QObject::tr("Escolha um arquivo de áudio."));
            return false;
        }
        cmd.params[QStringLiteral("source")] = source->text();
        cmd.params[QStringLiteral("volume")] = volume->value();
        if (loop) cmd.params[QStringLiteral("loop")] = loop->isChecked();
        cmd.params[QStringLiteral("pitch")]=pitch?pitch->value():100;
        cmd.params[QStringLiteral("pan")]=pan?pan->value():0;
        if (fadeIn) cmd.params[QStringLiteral("fadeInMs")] = fadeIn->value();
        if (smoothTransition)
            cmd.params[QStringLiteral("transitionMs")] = smoothTransition->isChecked() ? transitionTime->value() : 0;
    }
    return true;
}

static bool editPortraitVoiceCommand(core::Editor& ed,core::EventCommand& cmd,QWidget* parent)
{
    if(cmd.type==QLatin1String("voice.stop")||cmd.type==QLatin1String("voice.waitForEnd"))return true;
    QDialog d(parent);d.setWindowTitle(cmd.type.startsWith(QLatin1String("voice."))?QObject::tr("Voz do personagem"):QObject::tr("Retrato do personagem"));auto* v=new QVBoxLayout(&d);auto* form=new QFormLayout;v->addLayout(form);
    auto* speaker=new QComboBox(&d);speaker->addItem(QObject::tr("Selecione…"),QString());for(const core::SpeakerProfile& profile:ed.speakerDatabase.speakers)speaker->addItem(profile.name,profile.id);speaker->setCurrentIndex(qMax(0,speaker->findData(cmd.params.value(QStringLiteral("speakerId")).toString())));form->addRow(QObject::tr("Personagem:"),speaker);
    QLineEdit *expression=nullptr,*source=nullptr,*lineId=nullptr,*localizationKey=nullptr;QComboBox* position=nullptr;QSpinBox* volume=nullptr;
    if(cmd.type==QLatin1String("voice.play")){lineId=new QLineEdit(cmd.params.value(QStringLiteral("lineId")).toString(),&d);source=new QLineEdit(cmd.params.value(QStringLiteral("source")).toString(),&d);localizationKey=new QLineEdit(cmd.params.value(QStringLiteral("localizationKey")).toString(),&d);volume=new QSpinBox(&d);volume->setRange(0,100);volume->setSuffix(QStringLiteral("%"));volume->setValue(cmd.params.value(QStringLiteral("volume"),90).toInt());form->addRow(QObject::tr("Identificador da fala:"),lineId);form->addRow(QObject::tr("Arquivo (opcional):"),source);form->addRow(QObject::tr("Chave de tradução (opcional):"),localizationKey);form->addRow(QObject::tr("Volume:"),volume);}
    else if(cmd.type!=QLatin1String("portrait.hide")){expression=new QLineEdit(cmd.params.value(QStringLiteral("expression")).toString(),&d);form->addRow(QObject::tr("Expressão:"),expression);if(cmd.type==QLatin1String("portrait.show")){source=new QLineEdit(cmd.params.value(QStringLiteral("source")).toString(),&d);position=new QComboBox(&d);position->addItem(QObject::tr("Esquerda"),QStringLiteral("left"));position->addItem(QObject::tr("Direita"),QStringLiteral("right"));position->setCurrentIndex(qMax(0,position->findData(cmd.params.value(QStringLiteral("position"),QStringLiteral("left")))));form->addRow(QObject::tr("Imagem (opcional):"),source);form->addRow(QObject::tr("Lado:"),position);}}
    auto* info=new QLabel(QObject::tr("As falas seguem a ordem do diálogo. Se o personagem tiver retratos e expressões configurados, eles podem acompanhar a mensagem automaticamente."),&d);info->setWordWrap(true);v->addWidget(info);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;
    cmd.params.clear();cmd.params[QStringLiteral("speakerId")]=speaker->currentData();if(expression)cmd.params[QStringLiteral("expression")]=expression->text().trimmed();if(source&&!source->text().trimmed().isEmpty())cmd.params[QStringLiteral("source")]=source->text().trimmed();if(lineId)cmd.params[QStringLiteral("lineId")]=lineId->text().trimmed();if(localizationKey&&!localizationKey->text().trimmed().isEmpty())cmd.params[QStringLiteral("localizationKey")]=localizationKey->text().trimmed();if(position)cmd.params[QStringLiteral("position")]=position->currentData();if(volume)cmd.params[QStringLiteral("volume")]=volume->value();return true;
}

static bool editSpeechBubbleCommand(core::Editor& ed,core::EventCommand& cmd,QWidget* parent)
{
    Q_UNUSED(ed)
    if(cmd.type==QLatin1String("bubble.hideAll")||cmd.type==QLatin1String("notification.hide"))return true;
    QDialog d(parent);const bool notification=cmd.type.startsWith(QLatin1String("notification."));const bool hide=cmd.type==QLatin1String("bubble.hide");d.setWindowTitle(notification?QObject::tr("Notificação"):QObject::tr("Balão de fala"));d.resize(580,hide?220:560);auto* v=new QVBoxLayout(&d);auto* form=new QFormLayout;v->addLayout(form);
    QComboBox* target=nullptr;QPlainTextEdit* text=nullptr;QLineEdit *speaker=nullptr,*bgColor=nullptr,*textColor=nullptr;QSpinBox *duration=nullptr,*maxWidth=nullptr,*fontSize=nullptr,*offsetX=nullptr,*offsetY=nullptr;QComboBox *tail=nullptr,*position=nullptr;QCheckBox* typewriter=nullptr;
    if(!notification){target=new QComboBox(&d);target->setEditable(true);target->addItem(QObject::tr("Este evento"),QStringLiteral("self"));target->addItem(QObject::tr("Jogador"),QStringLiteral("player"));for(const core::MapEvent& event:ed.events())target->addItem(event.name,QStringLiteral("event:")+event.id);const QString saved=cmd.params.value(QStringLiteral("target"),QStringLiteral("self")).toString();int i=target->findData(saved);if(i<0){target->addItem(saved,saved);i=target->count()-1;}target->setCurrentIndex(i);form->addRow(QObject::tr("Alvo:"),target);}
    if(!hide){text=new QPlainTextEdit(cmd.params.value(QStringLiteral("text")).toString(),&d);text->setMaximumHeight(110);speaker=new QLineEdit(cmd.params.value(QStringLiteral("speaker")).toString(),&d);duration=new QSpinBox(&d);duration->setRange(1,36000);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(qBound(1,cmd.params.value(QStringLiteral("duration"),180).toInt(),36000));maxWidth=new QSpinBox(&d);maxWidth->setRange(120,1200);maxWidth->setSuffix(QObject::tr(" px"));maxWidth->setValue(qBound(120,cmd.params.value(QStringLiteral("maxWidth"),360).toInt(),1200));fontSize=new QSpinBox(&d);fontSize->setRange(8,96);fontSize->setSuffix(QObject::tr(" px"));fontSize->setValue(qBound(8,cmd.params.value(QStringLiteral("fontSize"),18).toInt(),96));bgColor=new QLineEdit(cmd.params.value(QStringLiteral("bgColor"),QStringLiteral("#e60c101c")).toString(),&d);textColor=new QLineEdit(cmd.params.value(QStringLiteral("textColor"),QStringLiteral("#ffffff")).toString(),&d);typewriter=new QCheckBox(QObject::tr("Revelar texto progressivamente"),&d);typewriter->setChecked(cmd.params.value(QStringLiteral("typewriter"),false).toBool());form->addRow(QObject::tr("Texto:"),text);if(!notification)form->addRow(QObject::tr("Nome do personagem:"),speaker);form->addRow(QObject::tr("Duração:"),duration);form->addRow(QObject::tr("Largura máxima:"),maxWidth);form->addRow(QObject::tr("Fonte:"),fontSize);form->addRow(QObject::tr("Cor de fundo:"),bgColor);form->addRow(QObject::tr("Cor do texto:"),textColor);form->addRow(QString(),typewriter);
        if(notification){position=new QComboBox(&d);for(const auto& item:QList<QPair<QString,QString>>{{QObject::tr("Superior esquerdo"),"top-left"},{QObject::tr("Superior centro"),"top-center"},{QObject::tr("Superior direito"),"top-right"},{QObject::tr("Inferior esquerdo"),"bottom-left"},{QObject::tr("Inferior centro"),"bottom-center"},{QObject::tr("Inferior direito"),"bottom-right"}})position->addItem(item.first,item.second);position->setCurrentIndex(qMax(0,position->findData(cmd.params.value(QStringLiteral("position"),QStringLiteral("top-right")))));form->addRow(QObject::tr("Posição:"),position);}else{tail=new QComboBox(&d);tail->addItem(QObject::tr("Para baixo"),QStringLiteral("down"));tail->addItem(QObject::tr("Para cima"),QStringLiteral("up"));tail->addItem(QObject::tr("Sem cauda"),QStringLiteral("none"));tail->setCurrentIndex(qMax(0,tail->findData(cmd.params.value(QStringLiteral("tailDirection"),QStringLiteral("down")))));offsetX=new QSpinBox(&d);offsetY=new QSpinBox(&d);for(QSpinBox* s:{offsetX,offsetY})s->setRange(-2000,2000);offsetX->setValue(cmd.params.value(QStringLiteral("offsetX")).toInt());offsetY->setValue(cmd.params.value(QStringLiteral("offsetY"),-16).toInt());auto* offsets=new QWidget(&d);auto* row=new QHBoxLayout(offsets);row->setContentsMargins(0,0,0,0);row->addWidget(new QLabel(QStringLiteral("X"),offsets));row->addWidget(offsetX);row->addWidget(new QLabel(QStringLiteral("Y"),offsets));row->addWidget(offsetY);form->addRow(QObject::tr("Cauda:"),tail);form->addRow(QObject::tr("Ajuste de posição:"),offsets);}}
    auto* info=new QLabel(notification?QObject::tr("Notificações ficam presas à tela e não acompanham a câmera."):QObject::tr("Balões acompanham o jogador ou o evento escolhido enquanto ele se move pelo mapa."),&d);info->setWordWrap(true);v->addWidget(info);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;
    cmd.params.clear();if(target)cmd.params[QStringLiteral("target")]=target->currentData().toString().isEmpty()?target->currentText().trimmed():target->currentData();if(text){cmd.params[QStringLiteral("text")]=text->toPlainText().trimmed();cmd.params[QStringLiteral("duration")]=duration->value();cmd.params[QStringLiteral("maxWidth")]=maxWidth->value();cmd.params[QStringLiteral("fontSize")]=fontSize->value();cmd.params[QStringLiteral("bgColor")]=bgColor->text().trimmed();cmd.params[QStringLiteral("textColor")]=textColor->text().trimmed();cmd.params[QStringLiteral("typewriter")]=typewriter->isChecked();}if(speaker&&!speaker->text().trimmed().isEmpty())cmd.params[QStringLiteral("speaker")]=speaker->text().trimmed();if(position)cmd.params[QStringLiteral("position")]=position->currentData();if(tail){cmd.params[QStringLiteral("tailDirection")]=tail->currentData();cmd.params[QStringLiteral("offsetX")]=offsetX->value();cmd.params[QStringLiteral("offsetY")]=offsetY->value();}return true;
}

struct FilterUiCommonControls
{
    QSpinBox* slot=nullptr;
    QCheckBox* world=nullptr;
    QCheckBox* pictures=nullptr;
    QCheckBox* hud=nullptr;
    QSpinBox* duration=nullptr;
    QCheckBox* wait=nullptr;
};

static FilterUiCommonControls addFilterUiCommon(QDialog& d,QFormLayout* form,const QVariantMap& params)
{
    FilterUiCommonControls c;
    c.slot=new QSpinBox(&d);c.slot->setRange(core::LudoFilterMinSlot,core::LudoFilterMaxSlot);c.slot->setValue(qBound(core::LudoFilterMinSlot,params.value(QStringLiteral("slot"),1).toInt(),core::LudoFilterMaxSlot));c.slot->setToolTip(QObject::tr("Slots 1–99. Dentro do mesmo tipo, o maior slot ativo tem prioridade; remover esse slot revela o anterior."));
    c.world=new QCheckBox(QObject::tr("Mapa, eventos, clima e névoa"),&d);c.world->setChecked(params.value(QStringLiteral("affectWorld"),true).toBool());
    c.pictures=new QCheckBox(QObject::tr("Imagens"),&d);c.pictures->setChecked(params.value(QStringLiteral("affectPictures"),false).toBool());
    c.hud=new QCheckBox(QObject::tr("Interface, HUD e legendas"),&d);c.hud->setChecked(params.value(QStringLiteral("affectHud"),false).toBool());
    c.duration=new QSpinBox(&d);c.duration->setRange(0,3600);c.duration->setSuffix(QObject::tr(" quadros"));c.duration->setValue(qBound(0,params.value(QStringLiteral("duration"),30).toInt(),3600));
    c.wait=new QCheckBox(QObject::tr("Esperar a transição terminar"),&d);c.wait->setChecked(params.value(QStringLiteral("wait"),false).toBool());
    form->addRow(QObject::tr("Camada do efeito:"),c.slot);form->addRow(QObject::tr("Afetar:"),c.world);form->addRow(QString(),c.pictures);form->addRow(QString(),c.hud);form->addRow(QObject::tr("Transição:"),c.duration);form->addRow(QString(),c.wait);
    return c;
}

static bool filterUiScopeValid(const FilterUiCommonControls& c,QWidget* parent)
{
    if(c.world->isChecked()||c.pictures->isChecked()||c.hud->isChecked())return true;
    QMessageBox::warning(parent,QObject::tr("Ludo Filter System"),QObject::tr("Escolha pelo menos um escopo para o filtro."));return false;
}

static void storeFilterUiCommon(QVariantMap& params,const FilterUiCommonControls& c)
{
    params[QStringLiteral("slot")]=c.slot->value();params[QStringLiteral("affectWorld")]=c.world->isChecked();params[QStringLiteral("affectPictures")]=c.pictures->isChecked();params[QStringLiteral("affectHud")]=c.hud->isChecked();params[QStringLiteral("duration")]=c.duration->value();params[QStringLiteral("wait")]=c.wait->isChecked();
}

static void addFilterScrollableForm(QDialog& d,QVBoxLayout* root,QFormLayout* form) { auto* scroll=new QScrollArea(&d); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); auto* body=new QWidget(scroll); body->setLayout(form); scroll->setWidget(body); root->addWidget(scroll,1); }

static bool editLudoCommand(core::Editor& ed,core::EventCommand& cmd,QWidget* parent)
{
    const QString t=cmd.type;
    if(t==QLatin1String("ludo.filter.chromaticAberration")){
        QDialog d(parent);d.setWindowTitle(QObject::tr("Filtro de aberração cromática"));d.resize(580,590);auto* v=new QVBoxLayout(&d);auto* info=new QLabel(QObject::tr("Separe levemente as cores para criar um efeito de lente ou distorção. O modo Lente concentra o efeito nas bordas da imagem."),&d);info->setWordWrap(true);v->addWidget(info);auto* form=new QFormLayout;
        auto* mode=new QComboBox(&d);mode->addItem(QObject::tr("Normal"),QStringLiteral("normal"));mode->addItem(QObject::tr("Lente"),QStringLiteral("lens"));mode->setCurrentIndex(qMax(0,mode->findData(cmd.params.value(QStringLiteral("mode"),QStringLiteral("lens")))));
        auto* intensity=new QDoubleSpinBox(&d);intensity->setRange(0,32);intensity->setDecimals(1);intensity->setSingleStep(.5);intensity->setSuffix(QObject::tr(" px"));intensity->setValue(qBound(0.0,cmd.params.value(QStringLiteral("intensity"),4.0).toDouble(),32.0));
        auto* edgeStart=new QSpinBox(&d);edgeStart->setRange(0,95);edgeStart->setSuffix(QStringLiteral("%"));edgeStart->setValue(qBound(0,int(std::lround(cmd.params.value(QStringLiteral("edgeStart"),.55).toDouble()*100.0)),95));
        auto* falloff=new QDoubleSpinBox(&d);falloff->setRange(.25,8);falloff->setDecimals(2);falloff->setSingleStep(.25);falloff->setValue(qBound(.25,cmd.params.value(QStringLiteral("falloff"),2.0).toDouble(),8.0));
        auto* mix=new QSpinBox(&d);mix->setRange(0,100);mix->setSuffix(QStringLiteral("%"));double sm=cmd.params.value(QStringLiteral("mix"),1.0).toDouble();if(sm<=1.0)sm*=100;mix->setValue(qBound(0,int(std::lround(sm)),100));
        form->addRow(QObject::tr("Modo:"),mode);form->addRow(QObject::tr("Intensidade:"),intensity);form->addRow(QObject::tr("Começar na borda:"),edgeStart);form->addRow(QObject::tr("Curva da lente:"),falloff);form->addRow(QObject::tr("Mistura:"),mix);auto common=addFilterUiCommon(d,form,cmd.params);addFilterScrollableForm(d,v,form);
        auto refreshMode=[=]{const bool lens=mode->currentData().toString()==QLatin1String("lens");edgeStart->setEnabled(lens);falloff->setEnabled(lens);};QObject::connect(mode,&QComboBox::currentIndexChanged,&d,[=](int){refreshMode();});refreshMode();
        auto* hint=new QLabel(QObject::tr("Use “Ver prévia” no Event Editor para conferir o resultado antes de testar o jogo."),&d);hint->setWordWrap(true);v->addWidget(hint);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted||!filterUiScopeValid(common,parent))return false;
        cmd.params.clear();cmd.params[QStringLiteral("mode")]=mode->currentData();cmd.params[QStringLiteral("intensity")]=intensity->value();cmd.params[QStringLiteral("edgeStart")]=edgeStart->value()/100.0;cmd.params[QStringLiteral("falloff")]=falloff->value();cmd.params[QStringLiteral("mix")]=mix->value()/100.0;storeFilterUiCommon(cmd.params,common);return true;
    }
    if(t==QLatin1String("ludo.filter.noise")){ QDialog d(parent);d.setWindowTitle(QObject::tr("Filtro de ruído e granulação"));d.resize(600,690);auto* v=new QVBoxLayout(&d); auto* proceduralInfo=new QLabel(QObject::tr("Adicione granulação ou ruído à imagem. O modo clássico preserva projetos antigos; a granulação de filme produz um resultado mais orgânico."),&d);proceduralInfo->setWordWrap(true);v->addWidget(proceduralInfo); auto* form=new QFormLayout; auto* preset=new QComboBox(&d);preset->addItem(QObject::tr("Personalizado"),QStringLiteral("custom"));preset->addItem(QObject::tr("Cinema sutil"),QStringLiteral("cinematic"));preset->addItem(QObject::tr("Filme 35 mm"),QStringLiteral("35mm"));preset->addItem(QObject::tr("Filme 16 mm"),QStringLiteral("16mm"));preset->addItem(QObject::tr("VHS"),QStringLiteral("vhs"));preset->addItem(QObject::tr("Sensor digital"),QStringLiteral("digital"));preset->addItem(QObject::tr("Estática de terror"),QStringLiteral("horror")); auto* style=new QComboBox(&d);style->addItem(core::noiseStyleLabel(core::NoiseStyle::FilmGrain),QStringLiteral("film"));style->addItem(core::noiseStyleLabel(core::NoiseStyle::LegacyBlock),QStringLiteral("legacy"));style->setCurrentIndex(qMax(0,style->findData(cmd.params.value(QStringLiteral("style"),QStringLiteral("legacy"))))); auto* intensity=new QSpinBox(&d);intensity->setRange(0,100);intensity->setSuffix(QStringLiteral("%"));intensity->setValue(qBound(0,int(std::lround(cmd.params.value(QStringLiteral("intensity"),.12).toDouble()*100)),100)); auto* size=new QDoubleSpinBox(&d);size->setRange(1,64);size->setDecimals(1);size->setSuffix(QObject::tr(" px"));size->setValue(qBound(1.0,cmd.params.value(QStringLiteral("size"),2.0).toDouble(),64.0)); auto* contrast=new QDoubleSpinBox(&d);contrast->setRange(.25,4.0);contrast->setDecimals(2);contrast->setSingleStep(.05);contrast->setValue(qBound(.25,cmd.params.value(QStringLiteral("contrast"),1.0).toDouble(),4.0)); auto* colorAmount=new QSpinBox(&d);colorAmount->setRange(0,100);colorAmount->setSuffix(QStringLiteral("%"));{double raw=cmd.params.value(QStringLiteral("colorAmount"),0.0).toDouble();if(raw<=1.0)raw*=100;colorAmount->setValue(qBound(0,int(std::lround(raw)),100));} auto* temporal=new QComboBox(&d);for(auto mode:{core::NoiseTemporalMode::Static,core::NoiseTemporalMode::Flicker,core::NoiseTemporalMode::Drift,core::NoiseTemporalMode::Smooth})temporal->addItem(core::noiseTemporalModeLabel(mode),core::noiseTemporalModeId(mode));temporal->setCurrentIndex(qMax(0,temporal->findData(cmd.params.value(QStringLiteral("temporal"),QStringLiteral("flicker"))))); auto* speed=new QDoubleSpinBox(&d);speed->setRange(0,20);speed->setDecimals(2);speed->setValue(qBound(0.0,cmd.params.value(QStringLiteral("speed"),1.0).toDouble(),20.0)); auto* seed=new QSpinBox(&d);seed->setRange(0,65535);seed->setValue(qBound(0,cmd.params.value(QStringLiteral("seed"),0).toInt(),65535)); form->addRow(QObject::tr("Modelo pronto:"),preset);form->addRow(QObject::tr("Tipo:"),style);form->addRow(QObject::tr("Intensidade:"),intensity);form->addRow(QObject::tr("Tamanho do grão:"),size);form->addRow(QObject::tr("Contraste:"),contrast);form->addRow(QObject::tr("Ruído colorido:"),colorAmount);form->addRow(QObject::tr("Animação do ruído:"),temporal);form->addRow(QObject::tr("Velocidade:"),speed);form->addRow(QObject::tr("Variação:"),seed); auto applyPreset=[&](const QString&id){if(id==QLatin1String("custom"))return;style->setCurrentIndex(style->findData(QStringLiteral("film")));if(id==QLatin1String("cinematic")){intensity->setValue(8);size->setValue(1.8);contrast->setValue(.85);colorAmount->setValue(0);temporal->setCurrentIndex(temporal->findData(QStringLiteral("smooth")));speed->setValue(.65);seed->setValue(1337);}else if(id==QLatin1String("35mm")){intensity->setValue(14);size->setValue(2.2);contrast->setValue(1.10);colorAmount->setValue(6);temporal->setCurrentIndex(temporal->findData(QStringLiteral("smooth")));speed->setValue(1.0);seed->setValue(3500);}else if(id==QLatin1String("16mm")){intensity->setValue(23);size->setValue(3.0);contrast->setValue(1.35);colorAmount->setValue(4);temporal->setCurrentIndex(temporal->findData(QStringLiteral("flicker")));speed->setValue(1.0);seed->setValue(1600);}else if(id==QLatin1String("vhs")){intensity->setValue(18);size->setValue(2.0);contrast->setValue(1.55);colorAmount->setValue(18);temporal->setCurrentIndex(temporal->findData(QStringLiteral("drift")));speed->setValue(1.4);seed->setValue(1985);}else if(id==QLatin1String("digital")){intensity->setValue(12);size->setValue(1.0);contrast->setValue(1.8);colorAmount->setValue(65);temporal->setCurrentIndex(temporal->findData(QStringLiteral("flicker")));speed->setValue(1.8);seed->setValue(2026);}else if(id==QLatin1String("horror")){intensity->setValue(38);size->setValue(1.4);contrast->setValue(2.4);colorAmount->setValue(12);temporal->setCurrentIndex(temporal->findData(QStringLiteral("flicker")));speed->setValue(3.2);seed->setValue(666);}}; QObject::connect(preset,&QComboBox::currentIndexChanged,&d,[&](int){applyPreset(preset->currentData().toString());}); QObject::connect(temporal,&QComboBox::currentIndexChanged,&d,[&](int){speed->setEnabled(temporal->currentData().toString()!=QLatin1String("static"));});speed->setEnabled(temporal->currentData().toString()!=QLatin1String("static")); auto common=addFilterUiCommon(d,form,cmd.params);addFilterScrollableForm(d,v,form);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted||!filterUiScopeValid(common,parent))return false; cmd.params.clear();cmd.params[QStringLiteral("intensity")]=intensity->value()/100.0;cmd.params[QStringLiteral("size")]=size->value();cmd.params[QStringLiteral("speed")]=speed->value();cmd.params[QStringLiteral("style")]=style->currentData();cmd.params[QStringLiteral("temporal")]=temporal->currentData();cmd.params[QStringLiteral("seed")]=seed->value();cmd.params[QStringLiteral("contrast")]=contrast->value();cmd.params[QStringLiteral("colorAmount")]=colorAmount->value()/100.0;storeFilterUiCommon(cmd.params,common);return true; }
    if(t==QLatin1String("ludo.filter.scanlines")){ QDialog d(parent);d.setWindowTitle(QObject::tr("Filtro de linhas de tela"));d.resize(640,820);auto*v=new QVBoxLayout(&d);auto*info=new QLabel(QObject::tr("Simule linhas de monitores e televisores antigos. Você pode ajustar o estilo, o movimento, o entrelaçamento e a faixa de varredura."),&d);info->setWordWrap(true);v->addWidget(info);auto*form=new QFormLayout;auto*preset=new QComboBox(&d);preset->addItem(QObject::tr("Personalizado"),"custom");preset->addItem(QObject::tr("CRT sutil"),"subtle");preset->addItem(QObject::tr("Monitor de arcade"),"arcade");preset->addItem(QObject::tr("TV antiga"),"oldtv");preset->addItem(QObject::tr("Reprodução VHS"),"vhs");preset->addItem(QObject::tr("Câmera de segurança"),"security");preset->addItem(QObject::tr("Terminal verde"),"terminal");auto*style=new QComboBox(&d);for(auto st:{core::ScanlineStyle::Legacy,core::ScanlineStyle::SoftCrt,core::ScanlineStyle::SharpCrt,core::ScanlineStyle::Interlaced})style->addItem(core::scanlineStyleLabel(st),core::scanlineStyleId(st));style->setCurrentIndex(qMax(0,style->findData(cmd.params.value("style",QStringLiteral("legacy")))));auto*intensity=new QSpinBox(&d);intensity->setRange(0,100);intensity->setSuffix("%");intensity->setValue(qBound(0,int(std::lround(cmd.params.value("intensity",.28).toDouble()*100)),100));auto*spacing=new QDoubleSpinBox(&d);spacing->setRange(2,32);spacing->setDecimals(1);spacing->setSuffix(QObject::tr(" px"));spacing->setValue(qBound(2.0,cmd.params.value("spacing",3.0).toDouble(),32.0));auto pct=[&](const char*k,int def){auto*q=new QSpinBox(&d);q->setRange(0,100);q->setSuffix("%");double raw=cmd.params.value(QString::fromLatin1(k),def/100.0).toDouble();if(raw<=1)raw*=100;q->setValue(qBound(0,int(std::lround(raw)),100));return q;};auto*thickness=pct("thickness",50);thickness->setMinimum(5);thickness->setMaximum(95);auto*softness=pct("softness",22);softness->setMaximum(95);auto*phase=new QDoubleSpinBox(&d);phase->setRange(-32,32);phase->setDecimals(1);phase->setSuffix(QObject::tr(" px"));phase->setValue(qBound(-32.0,cmd.params.value("phase",0.0).toDouble(),32.0));auto*scroll=new QDoubleSpinBox(&d);scroll->setRange(-120,120);scroll->setDecimals(1);scroll->setSuffix(QObject::tr(" px/s"));scroll->setValue(qBound(-120.0,cmd.params.value("scrollSpeed",0.0).toDouble(),120.0));auto*interlaceAmount=pct("interlaceAmount",55);auto*interlaceSpeed=new QDoubleSpinBox(&d);interlaceSpeed->setRange(0,120);interlaceSpeed->setDecimals(1);interlaceSpeed->setSuffix(QObject::tr(" Hz"));interlaceSpeed->setValue(qBound(0.0,cmd.params.value("interlaceSpeed",60.0).toDouble(),120.0));auto*sweep=new QCheckBox(QObject::tr("Ativar faixa de varredura"),&d);sweep->setChecked(cmd.params.value("whiteSweep",false).toBool());auto*sweepSpeed=new QDoubleSpinBox(&d);sweepSpeed->setRange(0,5);sweepSpeed->setDecimals(2);sweepSpeed->setValue(qBound(0.0,cmd.params.value("sweepSpeed",.35).toDouble(),5.0));auto*sweepWidth=pct("sweepWidth",6);sweepWidth->setMinimum(1);sweepWidth->setMaximum(50);auto*sweepSoft=pct("sweepSoftness",100);auto*sweepBright=pct("sweepIntensity",45);QColor sweepColor(qBound(0,cmd.params.value("sweepRed",255).toInt(),255),qBound(0,cmd.params.value("sweepGreen",255).toInt(),255),qBound(0,cmd.params.value("sweepBlue",255).toInt(),255));auto*colorBtn=new QPushButton(&d);auto refreshColor=[&]{colorBtn->setText(sweepColor.name(QColor::HexRgb));colorBtn->setStyleSheet(QStringLiteral("background:%1;color:%2;").arg(sweepColor.name(),sweepColor.lightness()>128?"#111":"#fff"));};refreshColor();QObject::connect(colorBtn,&QPushButton::clicked,&d,[&]{const QColor c=QColorDialog::getColor(sweepColor,&d,QObject::tr("Cor da faixa"));if(c.isValid()){sweepColor=c;refreshColor();}});auto*delay=new QSpinBox(&d);delay->setRange(0,3600);delay->setSuffix(QObject::tr(" quadros"));delay->setValue(qBound(0,cmd.params.value("sweepDelay",0).toInt(),3600));form->addRow(QObject::tr("Modelo pronto:"),preset);form->addRow(QObject::tr("Estilo:"),style);form->addRow(QObject::tr("Intensidade:"),intensity);form->addRow(QObject::tr("Espaçamento:"),spacing);form->addRow(QObject::tr("Espessura:"),thickness);form->addRow(QObject::tr("Suavidade:"),softness);form->addRow(QObject::tr("Fase:"),phase);form->addRow(QObject::tr("Movimento das linhas:"),scroll);form->addRow(QObject::tr("Força do entrelaçamento:"),interlaceAmount);form->addRow(QObject::tr("Velocidade do entrelaçamento:"),interlaceSpeed);form->addRow(QString(),sweep);form->addRow(QObject::tr("Velocidade da faixa:"),sweepSpeed);form->addRow(QObject::tr("Largura da faixa:"),sweepWidth);form->addRow(QObject::tr("Suavidade da faixa:"),sweepSoft);form->addRow(QObject::tr("Brilho da faixa:"),sweepBright);form->addRow(QObject::tr("Cor da faixa:"),colorBtn);form->addRow(QObject::tr("Delay entre varreduras:"),delay);auto applyPreset=[&](const QString&id){if(id=="custom")return;if(id=="subtle"){style->setCurrentIndex(style->findData("softCrt"));intensity->setValue(16);spacing->setValue(4);thickness->setValue(35);softness->setValue(32);scroll->setValue(0);sweep->setChecked(false);}else if(id=="arcade"){style->setCurrentIndex(style->findData("sharpCrt"));intensity->setValue(32);spacing->setValue(4);thickness->setValue(38);softness->setValue(8);scroll->setValue(0);sweep->setChecked(false);}else if(id=="oldtv"){style->setCurrentIndex(style->findData("interlaced"));intensity->setValue(34);spacing->setValue(3);thickness->setValue(50);softness->setValue(18);interlaceAmount->setValue(55);interlaceSpeed->setValue(60);scroll->setValue(0);sweep->setChecked(true);sweepSpeed->setValue(.28);sweepWidth->setValue(7);sweepSoft->setValue(70);sweepBright->setValue(22);sweepColor=QColor(225,238,255);refreshColor();}else if(id=="vhs"){style->setCurrentIndex(style->findData("softCrt"));intensity->setValue(25);spacing->setValue(3);thickness->setValue(48);softness->setValue(30);scroll->setValue(7);sweep->setChecked(true);sweepSpeed->setValue(.42);sweepWidth->setValue(5);sweepSoft->setValue(90);sweepBright->setValue(18);sweepColor=QColor(220,235,255);refreshColor();}else if(id=="security"){style->setCurrentIndex(style->findData("interlaced"));intensity->setValue(42);spacing->setValue(4);thickness->setValue(45);softness->setValue(12);interlaceAmount->setValue(72);interlaceSpeed->setValue(30);scroll->setValue(2);sweep->setChecked(false);}else if(id=="terminal"){style->setCurrentIndex(style->findData("sharpCrt"));intensity->setValue(28);spacing->setValue(3);thickness->setValue(32);softness->setValue(6);scroll->setValue(0);sweep->setChecked(true);sweepSpeed->setValue(.22);sweepWidth->setValue(10);sweepSoft->setValue(100);sweepBright->setValue(16);sweepColor=QColor(100,255,145);refreshColor();}};QObject::connect(preset,&QComboBox::currentIndexChanged,&d,[&](int){applyPreset(preset->currentData().toString());});auto updateEnabled=[&]{const bool modern=style->currentData().toString()!="legacy",inter=style->currentData().toString()=="interlaced";for(QWidget* w : std::initializer_list<QWidget*>{thickness, softness, phase, scroll}) w->setEnabled(modern);interlaceAmount->setEnabled(inter);interlaceSpeed->setEnabled(inter);for(QWidget* w : std::initializer_list<QWidget*>{sweepSpeed, sweepWidth, sweepSoft, sweepBright, colorBtn, delay}) w->setEnabled(sweep->isChecked());};QObject::connect(style,&QComboBox::currentIndexChanged,&d,[&](int){updateEnabled();});QObject::connect(sweep,&QCheckBox::toggled,&d,[&](bool){updateEnabled();});updateEnabled();auto common=addFilterUiCommon(d,form,cmd.params);addFilterScrollableForm(d,v,form);auto*box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted||!filterUiScopeValid(common,parent))return false;cmd.params.clear();cmd.params["intensity"]=intensity->value()/100.0;cmd.params["spacing"]=spacing->value();cmd.params["style"]=style->currentData();cmd.params["thickness"]=thickness->value()/100.0;cmd.params["softness"]=softness->value()/100.0;cmd.params["phase"]=phase->value();cmd.params["scrollSpeed"]=scroll->value();cmd.params["interlaceAmount"]=interlaceAmount->value()/100.0;cmd.params["interlaceSpeed"]=interlaceSpeed->value();cmd.params["whiteSweep"]=sweep->isChecked();cmd.params["sweepSpeed"]=sweepSpeed->value();cmd.params["sweepWidth"]=sweepWidth->value()/100.0;cmd.params["sweepSoftness"]=sweepSoft->value()/100.0;cmd.params["sweepIntensity"]=sweepBright->value()/100.0;cmd.params["sweepRed"]=sweepColor.red();cmd.params["sweepGreen"]=sweepColor.green();cmd.params["sweepBlue"]=sweepColor.blue();cmd.params["sweepDelay"]=delay->value();storeFilterUiCommon(cmd.params,common);return true; }
    if(t==QLatin1String("ludo.filter.vignette")){
        QDialog d(parent);d.setWindowTitle(QObject::tr("Ludo Filter System — Vignette"));d.resize(550,500);auto* v=new QVBoxLayout(&d);auto* form=new QFormLayout;auto* intensity=new QSpinBox(&d);auto* radius=new QSpinBox(&d);auto* softness=new QSpinBox(&d);for(QSpinBox* q:{intensity,radius,softness}){q->setRange(0,100);q->setSuffix(QStringLiteral("%"));}intensity->setValue(qBound(0,int(cmd.params.value(QStringLiteral("intensity"),.45).toDouble()*100),100));radius->setRange(5,100);radius->setValue(qBound(5,int(cmd.params.value(QStringLiteral("radius"),.68).toDouble()*100),100));softness->setRange(1,100);softness->setValue(qBound(1,int(cmd.params.value(QStringLiteral("softness"),.28).toDouble()*100),100));form->addRow(QObject::tr("Intensidade:"),intensity);form->addRow(QObject::tr("Raio claro:"),radius);form->addRow(QObject::tr("Suavidade:"),softness);auto common=addFilterUiCommon(d,form,cmd.params);addFilterScrollableForm(d,v,form);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted||!filterUiScopeValid(common,parent))return false;cmd.params.clear();cmd.params[QStringLiteral("intensity")]=intensity->value()/100.0;cmd.params[QStringLiteral("radius")]=radius->value()/100.0;cmd.params[QStringLiteral("softness")]=softness->value()/100.0;storeFilterUiCommon(cmd.params,common);return true;
    }
    if(t==QLatin1String("ludo.filter.blur")){ QDialog d(parent);d.setWindowTitle(QObject::tr("Filtro de desfoque"));d.resize(600,620);d.setMaximumHeight(720); auto* v=new QVBoxLayout(&d); auto* qualityInfo=new QLabel(QObject::tr("Desfoque a cena com diferentes estilos. Ajuste a força, a qualidade e quanto as bordas devem permanecer definidas."),&d);qualityInfo->setWordWrap(true);v->addWidget(qualityInfo); auto* form=new QFormLayout; auto* preset=new QComboBox(&d);preset->addItem(QObject::tr("Personalizado"),QStringLiteral("custom"));preset->addItem(QObject::tr("Suave e sutil"),QStringLiteral("subtle"));preset->addItem(QObject::tr("Fundo cinematográfico"),QStringLiteral("cinematic"));preset->addItem(QObject::tr("Foco em diálogo"),QStringLiteral("dialogue"));preset->addItem(QObject::tr("Sonho"),QStringLiteral("dream"));preset->addItem(QObject::tr("Movimento horizontal"),QStringLiteral("motionH"));preset->addItem(QObject::tr("Movimento vertical"),QStringLiteral("motionV"));preset->addItem(QObject::tr("Censura em pixels"),QStringLiteral("pixel")); auto* style=new QComboBox(&d);for(auto st:{core::BlurStyle::LegacyGaussian,core::BlurStyle::GaussianSoft,core::BlurStyle::GaussianSharp,core::BlurStyle::Box,core::BlurStyle::Directional,core::BlurStyle::Pixel})style->addItem(core::blurStyleLabel(st),core::blurStyleId(st));style->setCurrentIndex(qMax(0,style->findData(cmd.params.value(QStringLiteral("style"),QStringLiteral("legacy"))))); auto* radius=new QDoubleSpinBox(&d);radius->setRange(0,24);radius->setDecimals(1);radius->setSingleStep(.5);radius->setSuffix(QObject::tr(" px"));radius->setValue(qBound(0.0,cmd.params.value(QStringLiteral("radius"),4.0).toDouble(),24.0)); auto* strength=new QSpinBox(&d);strength->setRange(0,100);strength->setSuffix(QStringLiteral("%"));{double raw=cmd.params.value(QStringLiteral("strength"),1.0).toDouble();if(raw<=1.0)raw*=100;strength->setValue(qBound(0,int(std::lround(raw)),100));} auto* direction=new QComboBox(&d);direction->addItem(QObject::tr("Horizontal"),QStringLiteral("horizontal"));direction->addItem(QObject::tr("Vertical"),QStringLiteral("vertical"));direction->addItem(QObject::tr("Completo / 2D"),QStringLiteral("full"));direction->setCurrentIndex(qMax(0,direction->findData(cmd.params.value(QStringLiteral("direction"),QStringLiteral("full"))))); auto* quality=new QComboBox(&d);for(auto q:{core::BlurQuality::Low,core::BlurQuality::Medium,core::BlurQuality::High,core::BlurQuality::Ultra})quality->addItem(core::blurQualityLabel(q),core::blurQualityId(q));quality->setCurrentIndex(qMax(0,quality->findData(cmd.params.value(QStringLiteral("quality"),QStringLiteral("medium"))))); auto* edge=new QSpinBox(&d);edge->setRange(0,100);edge->setSuffix(QStringLiteral("%"));{double raw=cmd.params.value(QStringLiteral("edgePreservation"),0.0).toDouble();if(raw<=1.0)raw*=100;edge->setValue(qBound(0,int(std::lround(raw)),100));} auto* angle=new QDoubleSpinBox(&d);angle->setRange(-180,180);angle->setDecimals(1);angle->setSuffix(QStringLiteral("°"));angle->setValue(qBound(-180.0,cmd.params.value(QStringLiteral("angle"),0.0).toDouble(),180.0)); form->addRow(QObject::tr("Modelo pronto:"),preset);form->addRow(QObject::tr("Estilo:"),style);form->addRow(QObject::tr("Raio / comprimento:"),radius);form->addRow(QObject::tr("Força:"),strength);form->addRow(QObject::tr("Direção do desfoque:"),direction);form->addRow(QObject::tr("Qualidade:"),quality);form->addRow(QObject::tr("Preservar bordas:"),edge);form->addRow(QObject::tr("Ângulo do movimento:"),angle); auto applyPreset=[&](const QString&id){if(id==QLatin1String("custom"))return;if(id==QLatin1String("subtle")){style->setCurrentIndex(style->findData(QStringLiteral("gaussianSoft")));radius->setValue(4);strength->setValue(25);direction->setCurrentIndex(direction->findData(QStringLiteral("full")));quality->setCurrentIndex(quality->findData(QStringLiteral("medium")));edge->setValue(15);}else if(id==QLatin1String("cinematic")){style->setCurrentIndex(style->findData(QStringLiteral("gaussianSoft")));radius->setValue(10);strength->setValue(62);direction->setCurrentIndex(direction->findData(QStringLiteral("full")));quality->setCurrentIndex(quality->findData(QStringLiteral("high")));edge->setValue(18);}else if(id==QLatin1String("dialogue")){style->setCurrentIndex(style->findData(QStringLiteral("gaussianSoft")));radius->setValue(6);strength->setValue(35);direction->setCurrentIndex(direction->findData(QStringLiteral("full")));quality->setCurrentIndex(quality->findData(QStringLiteral("medium")));edge->setValue(25);}else if(id==QLatin1String("dream")){style->setCurrentIndex(style->findData(QStringLiteral("gaussianSoft")));radius->setValue(12);strength->setValue(68);direction->setCurrentIndex(direction->findData(QStringLiteral("full")));quality->setCurrentIndex(quality->findData(QStringLiteral("high")));edge->setValue(5);}else if(id==QLatin1String("motionH")){style->setCurrentIndex(style->findData(QStringLiteral("directional")));radius->setValue(16);strength->setValue(70);quality->setCurrentIndex(quality->findData(QStringLiteral("high")));edge->setValue(8);angle->setValue(0);}else if(id==QLatin1String("motionV")){style->setCurrentIndex(style->findData(QStringLiteral("directional")));radius->setValue(16);strength->setValue(70);quality->setCurrentIndex(quality->findData(QStringLiteral("high")));edge->setValue(8);angle->setValue(90);}else if(id==QLatin1String("pixel")){style->setCurrentIndex(style->findData(QStringLiteral("pixel")));radius->setValue(10);strength->setValue(100);quality->setCurrentIndex(quality->findData(QStringLiteral("low")));edge->setValue(0);}}; QObject::connect(preset,&QComboBox::currentIndexChanged,&d,[&](int){applyPreset(preset->currentData().toString());}); auto updateEnabled=[&]{const QString st=style->currentData().toString();const bool legacy=st==QLatin1String("legacy"),directional=st==QLatin1String("directional"),pixel=st==QLatin1String("pixel");direction->setEnabled(!directional&&!pixel);quality->setEnabled(!legacy&&!pixel);edge->setEnabled(!legacy&&!pixel);angle->setEnabled(directional);strength->setEnabled(!legacy);}; QObject::connect(style,&QComboBox::currentIndexChanged,&d,[&](int){updateEnabled();});updateEnabled(); auto common=addFilterUiCommon(d,form,cmd.params);addFilterScrollableForm(d,v,form); auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject); if(d.exec()!=QDialog::Accepted||!filterUiScopeValid(common,parent))return false; cmd.params.clear();cmd.params[QStringLiteral("radius")]=radius->value();cmd.params[QStringLiteral("direction")]=direction->currentData();cmd.params[QStringLiteral("style")]=style->currentData();cmd.params[QStringLiteral("quality")]=quality->currentData();cmd.params[QStringLiteral("strength")]=strength->value()/100.0;cmd.params[QStringLiteral("edgePreservation")]=edge->value()/100.0;cmd.params[QStringLiteral("angle")]=angle->value();storeFilterUiCommon(cmd.params,common);return true; }
    if(t==QLatin1String("ludo.filter.tiltShift")){ QDialog d(parent);d.setWindowTitle(QObject::tr("Filtro de foco seletivo (Tilt-Shift)"));d.resize(610,700);auto*v=new QVBoxLayout(&d);auto*info=new QLabel(QObject::tr("Mantenha uma faixa da cena em foco e desfoque o restante. Você pode inclinar o plano de foco e controlar o desfoque acima e abaixo separadamente."),&d);info->setWordWrap(true);v->addWidget(info);auto*form=new QFormLayout;auto*preset=new QComboBox(&d);preset->addItem(QObject::tr("Personalizado"),"custom");preset->addItem(QObject::tr("Foco sutil"),"subtle");preset->addItem(QObject::tr("Miniatura / diorama"),"miniature");preset->addItem(QObject::tr("Foco em diálogo"),"dialogue");preset->addItem(QObject::tr("Foco de sonho"),"dream");preset->addItem(QObject::tr("Diorama vertical"),"vertical");auto*style=new QComboBox(&d);for(auto st:{core::TiltShiftStyle::LegacyGaussian,core::TiltShiftStyle::GaussianSoft,core::TiltShiftStyle::GaussianSharp,core::TiltShiftStyle::Box})style->addItem(core::tiltShiftStyleLabel(st),core::tiltShiftStyleId(st));style->setCurrentIndex(qMax(0,style->findData(cmd.params.value("style",QStringLiteral("legacy")))));auto*quality=new QComboBox(&d);for(auto q:{core::BlurQuality::Low,core::BlurQuality::Medium,core::BlurQuality::High,core::BlurQuality::Ultra})quality->addItem(core::blurQualityLabel(q),core::blurQualityId(q));quality->setCurrentIndex(qMax(0,quality->findData(cmd.params.value("quality",QStringLiteral("medium")))));auto*blur=new QDoubleSpinBox(&d);blur->setRange(0,24);blur->setDecimals(1);blur->setSuffix(QObject::tr(" px"));blur->setValue(qBound(0.0,cmd.params.value("blur",6.0).toDouble(),24.0));auto pct=[&](const char*k,int def,int lo=0,int hi=100){auto*q=new QSpinBox(&d);q->setRange(lo,hi);q->setSuffix("%");double raw=cmd.params.value(QString::fromLatin1(k),def/100.0).toDouble();if(raw<=2.0)raw*=100.0;q->setValue(qBound(lo,int(std::lround(raw)),hi));return q;};auto*strength=pct("strength",100);auto*center=pct("centerY",50);auto*width=pct("focusWidth",25,1);auto*falloff=pct("falloff",20,1);auto*edge=pct("edgePreservation",0);auto*upper=pct("upperBlur",100,0,200);auto*lower=pct("lowerBlur",100,0,200);auto*angle=new QDoubleSpinBox(&d);angle->setRange(-180,180);angle->setDecimals(1);angle->setSuffix(QStringLiteral("°"));angle->setValue(qBound(-180.0,cmd.params.value("angle",0.0).toDouble(),180.0));form->addRow(QObject::tr("Modelo pronto:"),preset);form->addRow(QObject::tr("Estilo:"),style);form->addRow(QObject::tr("Qualidade:"),quality);form->addRow(QObject::tr("Raio máximo:"),blur);form->addRow(QObject::tr("Força:"),strength);form->addRow(QObject::tr("Centro do plano:"),center);form->addRow(QObject::tr("Faixa em foco:"),width);form->addRow(QObject::tr("Transição do foco:"),falloff);form->addRow(QObject::tr("Ângulo do plano:"),angle);form->addRow(QObject::tr("Preservar bordas:"),edge);form->addRow(QObject::tr("Desfoque acima:"),upper);form->addRow(QObject::tr("Desfoque abaixo:"),lower);auto applyPreset=[&](const QString&id){if(id=="custom")return;style->setCurrentIndex(style->findData("gaussianSoft"));quality->setCurrentIndex(quality->findData("medium"));edge->setValue(10);upper->setValue(100);lower->setValue(100);angle->setValue(0);if(id=="subtle"){blur->setValue(5);strength->setValue(35);center->setValue(50);width->setValue(38);falloff->setValue(24);edge->setValue(20);}else if(id=="miniature"){style->setCurrentIndex(style->findData("gaussianSharp"));quality->setCurrentIndex(quality->findData("high"));blur->setValue(12);strength->setValue(85);center->setValue(52);width->setValue(20);falloff->setValue(18);edge->setValue(12);upper->setValue(120);lower->setValue(100);angle->setValue(-4);}else if(id=="dialogue"){blur->setValue(7);strength->setValue(48);center->setValue(52);width->setValue(46);falloff->setValue(28);edge->setValue(30);}else if(id=="dream"){blur->setValue(14);strength->setValue(72);center->setValue(50);width->setValue(30);falloff->setValue(34);edge->setValue(0);upper->setValue(85);lower->setValue(115);}else if(id=="vertical"){style->setCurrentIndex(style->findData("gaussianSharp"));quality->setCurrentIndex(quality->findData("high"));blur->setValue(11);strength->setValue(82);center->setValue(50);width->setValue(22);falloff->setValue(18);edge->setValue(15);angle->setValue(90);}};QObject::connect(preset,&QComboBox::currentIndexChanged,&d,[&](int){applyPreset(preset->currentData().toString());});auto updateEnabled=[&]{const bool modern=style->currentData().toString()!="legacy";for(QWidget*w:std::initializer_list<QWidget*>{quality,strength,angle,edge,upper,lower})w->setEnabled(modern);};QObject::connect(style,&QComboBox::currentIndexChanged,&d,[&](int){updateEnabled();});updateEnabled();auto common=addFilterUiCommon(d,form,cmd.params);addFilterScrollableForm(d,v,form);auto*box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted||!filterUiScopeValid(common,parent))return false;cmd.params.clear();cmd.params["blur"]=blur->value();cmd.params["centerY"]=center->value()/100.0;cmd.params["focusWidth"]=width->value()/100.0;cmd.params["falloff"]=falloff->value()/100.0;cmd.params["style"]=style->currentData();cmd.params["quality"]=quality->currentData();cmd.params["strength"]=strength->value()/100.0;cmd.params["edgePreservation"]=edge->value()/100.0;cmd.params["angle"]=angle->value();cmd.params["upperBlur"]=upper->value()/100.0;cmd.params["lowerBlur"]=lower->value()/100.0;storeFilterUiCommon(cmd.params,common);return true; }
    if(t==QLatin1String("ludo.filter.clear")){
        QDialog d(parent);d.setWindowTitle(QObject::tr("Remover filtro"));d.resize(520,360);auto* v=new QVBoxLayout(&d);auto* info=new QLabel(QObject::tr("Remova um filtro de uma camada específica ou limpe todas as camadas desse efeito. Se houver uma configuração anterior, ela volta a aparecer automaticamente."),&d);info->setWordWrap(true);v->addWidget(info);auto* form=new QFormLayout;auto* filter=new QComboBox(&d);filter->addItem(QObject::tr("Todos os filtros"),QStringLiteral("all"));filter->addItem(QObject::tr("Aberração Cromática"),QStringLiteral("chromaticAberration"));filter->addItem(QObject::tr("Ruído"),QStringLiteral("noise"));filter->addItem(QObject::tr("Linhas de tela"),QStringLiteral("scanlines"));filter->addItem(QObject::tr("Vinheta"),QStringLiteral("vignette"));filter->addItem(QObject::tr("Desfoque"),QStringLiteral("blur"));filter->addItem(QObject::tr("Foco seletivo (Tilt-Shift)"),QStringLiteral("tiltShift"));filter->setCurrentIndex(qMax(0,filter->findData(cmd.params.value(QStringLiteral("filter"),QStringLiteral("all")))));auto* slot=new QSpinBox(&d);slot->setRange(1,99);slot->setValue(qBound(1,cmd.params.value(QStringLiteral("slot"),1).toInt(),99));auto* allSlots=new QCheckBox(QObject::tr("Remover todas as camadas deste filtro"),&d);allSlots->setChecked(cmd.params.value(QStringLiteral("allSlots"),true).toBool());slot->setEnabled(!allSlots->isChecked());QObject::connect(allSlots,&QCheckBox::toggled,slot,&QWidget::setDisabled);auto* duration=new QSpinBox(&d);duration->setRange(0,3600);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(qBound(0,cmd.params.value(QStringLiteral("duration"),30).toInt(),3600));auto* wait=new QCheckBox(QObject::tr("Esperar a transição terminar"),&d);wait->setChecked(cmd.params.value(QStringLiteral("wait"),false).toBool());form->addRow(QObject::tr("Filtro:"),filter);form->addRow(QObject::tr("Camada do efeito:"),slot);form->addRow(QString(),allSlots);form->addRow(QObject::tr("Transição:"),duration);form->addRow(QString(),wait);v->addLayout(form);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;cmd.params.clear();cmd.params[QStringLiteral("filter")]=filter->currentData();cmd.params[QStringLiteral("slot")]=slot->value();cmd.params[QStringLiteral("allSlots")]=allSlots->isChecked();cmd.params[QStringLiteral("duration")]=duration->value();cmd.params[QStringLiteral("wait")]=wait->isChecked();return true;
    }
    if(isScreenToneCommand(t)) return editScreenToneCommand(ed,cmd,parent);
    if(t==QLatin1String("ludo.screen.clearEffects")) return true;
    if(t==QLatin1String("ludo.screen.flash")||t==QLatin1String("ludo.screen.fade")||t==QLatin1String("ludo.screen.shake")){
        QDialog d(parent);d.setWindowTitle(t.endsWith(QLatin1String("flash"))?QObject::tr("Piscar a tela"):t.endsWith(QLatin1String("fade"))?QObject::tr("Escurecer / revelar a tela"):QObject::tr("Tremer a tela"));d.resize(520,360);
        auto* v=new QVBoxLayout(&d);auto* info=new QLabel(QObject::tr("Aplica o efeito à tela inteira. A câmera e o zoom não mudam sua intensidade, e a interface permanece legível por cima. As opções de acessibilidade podem suavizar flashes e tremores."),&d);info->setWordWrap(true);v->addWidget(info);auto* form=new QFormLayout;
        QSpinBox *red=nullptr,*green=nullptr,*blue=nullptr,*alpha=nullptr,*duration=nullptr;QDoubleSpinBox *sx=nullptr,*sy=nullptr,*frequency=nullptr;QComboBox* direction=nullptr;
        if(t!=QLatin1String("ludo.screen.shake")){
            red=new QSpinBox(&d);green=new QSpinBox(&d);blue=new QSpinBox(&d);alpha=new QSpinBox(&d);for(QSpinBox* c:{red,green,blue,alpha})c->setRange(0,255);
            red->setValue(cmd.params.value("red",t.endsWith(QLatin1String("flash"))?255:0).toInt());green->setValue(cmd.params.value("green",t.endsWith(QLatin1String("flash"))?255:0).toInt());blue->setValue(cmd.params.value("blue",t.endsWith(QLatin1String("flash"))?255:0).toInt());alpha->setValue(cmd.params.value("alpha",t.endsWith(QLatin1String("flash"))?180:255).toInt());
            form->addRow(QObject::tr("Vermelho:"),red);form->addRow(QObject::tr("Verde:"),green);form->addRow(QObject::tr("Azul:"),blue);form->addRow(QObject::tr("Intensidade:"),alpha);
            if(t==QLatin1String("ludo.screen.fade")){direction=new QComboBox(&d);direction->addItem(QObject::tr("Escurecer a cena"),QStringLiteral("out"));direction->addItem(QObject::tr("Revelar a cena"),QStringLiteral("in"));direction->setCurrentIndex(qMax(0,direction->findData(cmd.params.value("direction",QStringLiteral("out")))));form->insertRow(0,QObject::tr("Direção:"),direction);}
        }else{
            sx=new QDoubleSpinBox(&d);sy=new QDoubleSpinBox(&d);frequency=new QDoubleSpinBox(&d);for(QDoubleSpinBox* q:{sx,sy}){q->setRange(0,256);q->setDecimals(1);q->setSuffix(QObject::tr(" px"));}sx->setValue(cmd.params.value("x",6.0).toDouble());sy->setValue(cmd.params.value("y",3.0).toDouble());frequency->setRange(.5,60);frequency->setDecimals(1);frequency->setSuffix(QObject::tr(" Hz"));frequency->setValue(cmd.params.value("frequency",12.0).toDouble());form->addRow(QObject::tr("Movimento horizontal:"),sx);form->addRow(QObject::tr("Movimento vertical:"),sy);form->addRow(QObject::tr("Frequência:"),frequency);
        }
        duration=new QSpinBox(&d);duration->setRange(0,3600);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(cmd.params.value("duration",t.endsWith(QLatin1String("flash"))?20:30).toInt());auto* wait=new QCheckBox(QObject::tr("Esperar o efeito terminar"),&d);wait->setChecked(cmd.params.value("wait",false).toBool());form->addRow(QObject::tr("Duração:"),duration);form->addRow(wait);v->addLayout(form);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;cmd.params.clear();if(red){cmd.params["red"]=red->value();cmd.params["green"]=green->value();cmd.params["blue"]=blue->value();cmd.params["alpha"]=alpha->value();}if(direction)cmd.params["direction"]=direction->currentData();if(sx){cmd.params["x"]=sx->value();cmd.params["y"]=sy->value();cmd.params["frequency"]=frequency->value();}cmd.params["duration"]=duration->value();cmd.params["wait"]=wait->isChecked();return true;
    }
    if(t==QLatin1String("ludo.screen.set")||t==QLatin1String("ludo.screen.clear")){cmd.type=QStringLiteral("comment");cmd.params={{QStringLiteral("text"),QObject::tr("Ludo Screen Filters foi removido.")}};return true;}
    if(t==QLatin1String("ludo.command")){
        QDialog d(parent);d.setWindowTitle(QObject::tr("Comando Ludo"));d.resize(610,420);
        auto* v=new QVBoxLayout(&d);
        auto* info=new QLabel(QObject::tr("Escreva um comando textual compatível. Ao confirmar, ele é validado pelo schema central e convertido imediatamente para um Event Command nativo. Tags <Ludo...> em Comentários pertencem ao ciclo OnPageActivated; Begin/End de Cutscene são estruturais e devem ficar no fluxo normal do evento."),&d);info->setWordWrap(true);v->addWidget(info);
        auto* text=new QPlainTextEdit(&d);text->setPlaceholderText(QStringLiteral("<LudoCamera alvo=player duracao=30>"));text->setPlainText(cmd.params.value(QStringLiteral("text")).toString());v->addWidget(text,1);
        auto* automatic=new QCheckBox(QObject::tr("Executar uma vez ao ativar esta página (OnPageActivated)"),&d);automatic->setChecked(cmd.params.value(QStringLiteral("automatic"),true).toBool());v->addWidget(automatic);
        auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::Help,&d);v->addWidget(box);
        QObject::connect(box,&QDialogButtonBox::helpRequested,&d,[&]{
            QMessageBox::information(&d,QObject::tr("Ajuda — Ludo Command System 2.0"),
                QObject::tr(
                    "Comandos textuais reconhecidos:\n\n"
                    "<LudoCamera alvo=player duracao=30 zoom=2 suavidade=6>\n"
                    "<LudoFade alvo=self opacidade=0 duracao=30>\n"
                    "<LudoPhantom alvo=self perto=1 distancia=6 minimo=32 maximo=255>\n"
                    "<LudoCutscene> e <LudoCutscene fim>\n\n"
                    "Nome, parâmetros, tipos e limites são validados antes da criação. Um nome desconhecido, como <LudoBanana>, é rejeitado em vez de virar no-op.\n\n"
                    "OnPageActivated executa no ciclo de ativação da página e não volta a executar quando o Interpreter percorre normalmente o evento."));
        });
        QObject::connect(box,&QDialogButtonBox::accepted,&d,[&]{
            const QString source=text->toPlainText().trimmed();
            if(source.isEmpty()){QMessageBox::information(&d,QObject::tr("Comando Ludo"),QObject::tr("Informe um comando do Ludo System."));return;}
            const core::EventExecutionMode mode=automatic->isChecked()?core::EventExecutionMode::OnPageActivated:core::EventExecutionMode::Normal;
            const core::LudoCommandParseResult parsed=core::parseLudoCommandText(source,mode);
            if(!parsed.valid){QMessageBox::warning(&d,QObject::tr("Comando Ludo inválido"),parsed.message);return;}
            cmd=parsed.command;
            d.accept();
        });
        QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        return d.exec()==QDialog::Accepted;
    }
    if(t==QLatin1String("ludo.cutscene.settings")){
        QDialog d(parent);d.setWindowTitle(QObject::tr("Configurar próxima cutscene"));auto* v=new QVBoxLayout(&d);auto* form=new QFormLayout;auto* allowed=new QCheckBox(QObject::tr("Permitir pular esta região"),&d);allowed->setChecked(cmd.params.value("skipAllowed",true).toBool());auto* action=new QComboBox(&d);for(GameAction a:allGameActions())action->addItem(gameActionLabel(a),gameActionId(a));action->setCurrentIndex(qMax(0,action->findData(cmd.params.value("skipAction",QStringLiteral("skipCutscene")))));auto* nesting=new QComboBox(&d);nesting->addItem(QObject::tr("Permitir regiões aninhadas"),QStringLiteral("allow"));nesting->addItem(QObject::tr("Avisar se aninhada"),QStringLiteral("warn"));nesting->addItem(QObject::tr("Proibir aninhamento"),QStringLiteral("forbid"));nesting->setCurrentIndex(qMax(0,nesting->findData(cmd.params.value("nesting",QStringLiteral("allow")))));form->addRow(allowed);form->addRow(QObject::tr("Ação de input:"),action);form->addRow(QObject::tr("Aninhamento:"),nesting);v->addLayout(form);auto* hint=new QLabel(QObject::tr("Este comando configura o próximo Início no mesmo fluxo. A ação usa o Input Map e funciona igualmente com teclado e controle."),&d);hint->setWordWrap(true);v->addWidget(hint);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;cmd.params={{QStringLiteral("skipAllowed"),allowed->isChecked()},{QStringLiteral("skipAction"),action->currentData()},{QStringLiteral("nesting"),nesting->currentData()}};return true;
    }
    if(t==QLatin1String("ludo.camera.save")||t==QLatin1String("ludo.camera.release")||t==QLatin1String("ludo.sprite.clear")||t==QLatin1String("ludo.cutscene.begin")||t==QLatin1String("ludo.cutscene.end")||t==QLatin1String("ludo.cutscene.enable")||t==QLatin1String("ludo.cutscene.disable"))return true;
    if(t==QLatin1String("comment")){bool ok=false;const QString value=QInputDialog::getMultiLineText(parent,QObject::tr("Comentário de evento"),QObject::tr("Comentário:"),cmd.params.value("text").toString(),&ok);if(ok)cmd.params["text"]=value;return ok;}
    QDialog d(parent);d.setWindowTitle(t.startsWith("ludo.camera")?QObject::tr("Câmera"):QObject::tr("Ludo Sprite Effects"));auto* v=new QVBoxLayout(&d);auto* info=new QLabel(t.startsWith("ludo.camera")?QObject::tr("Escolha para onde a câmera deve olhar e como ela deve se mover."):QObject::tr("Efeitos No-Code do Ludo System."),&d);info->setWordWrap(true);v->addWidget(info);auto* form=new QFormLayout;
    auto targetCombo=[&]{auto* c=new QComboBox(&d);c->addItem(QObject::tr("Jogador"),QStringLiteral("player"));c->addItem(QObject::tr("Este evento"),QStringLiteral("self"));for(const MapEvent& e:ed.events())c->addItem(e.name,QStringLiteral("event:")+e.id);return c;};
    QComboBox* target=nullptr;QDoubleSpinBox* x=nullptr;QDoubleSpinBox* y=nullptr;QDoubleSpinBox* zoom=nullptr;QDoubleSpinBox* scaleX=nullptr;QDoubleSpinBox* scaleY=nullptr;QDoubleSpinBox* shakeFrequency=nullptr;QSpinBox* duration=nullptr;QComboBox* ease=nullptr;QCheckBox* follow=nullptr;QDoubleSpinBox* followSpeed=nullptr;QDoubleSpinBox* deadzone=nullptr;QCheckBox* disableSmooth=nullptr;QPushButton* pickCameraTarget=nullptr;QCheckBox* wait=nullptr;QSpinBox* opacity=nullptr;QComboBox* phantomMode=nullptr;QDoubleSpinBox* nearDistance=nullptr;QDoubleSpinBox* distance=nullptr;QSpinBox* minimum=nullptr;QSpinBox* maximum=nullptr;QSpinBox* phantomSmoothness=nullptr;
    const bool cameraWithTarget=t==QLatin1String("ludo.camera.move")||t==QLatin1String("ludo.camera.moveOnly");
    if(cameraWithTarget){
        target=targetCombo();target->addItem(QObject::tr("Posição do mapa"),QStringLiteral("position"));target->setCurrentIndex(qMax(0,target->findData(cmd.params.value("target","player"))));
        x=new QDoubleSpinBox(&d);y=new QDoubleSpinBox(&d);for(auto* s:{x,y}){s->setRange(-99999,99999);s->setDecimals(2);}x->setValue(cmd.params.value("x").toDouble());y->setValue(cmd.params.value("y").toDouble());
        form->addRow(QObject::tr("Alvo:"),target);
        pickCameraTarget=new QPushButton(QObject::tr("Selecionar alvo no mapa…"),&d);form->addRow(QString(),pickCameraTarget);
        form->addRow(QObject::tr("Célula X (se posição):"),x);form->addRow(QObject::tr("Célula Y:"),y);
        if(t==QLatin1String("ludo.camera.move")){zoom=new QDoubleSpinBox(&d);zoom->setRange(.25,8);zoom->setSingleStep(.25);zoom->setSuffix(QObject::tr("×"));zoom->setValue(cmd.params.value("zoom",2).toDouble());form->addRow(QObject::tr("Aproximação:"),zoom);}
        follow=new QCheckBox(QObject::tr("Continuar seguindo o alvo"),&d);follow->setChecked(cmd.params.value("follow",true).toBool());form->addRow(follow);
        followSpeed=new QDoubleSpinBox(&d);followSpeed->setRange(.1,30);followSpeed->setValue(cmd.params.value("followSpeed",6.0).toDouble());followSpeed->setSuffix(QObject::tr(" suavidade"));
        deadzone=new QDoubleSpinBox(&d);deadzone->setRange(0,9999);deadzone->setValue(cmd.params.value("deadzone",0).toDouble());deadzone->setSuffix(QObject::tr(" px"));
        form->addRow(QObject::tr("Suavidade ao seguir:"),followSpeed);form->addRow(QObject::tr("Zona morta:"),deadzone);
        disableSmooth=new QCheckBox(QObject::tr("Desativar Suavidade"),&d);disableSmooth->setChecked(cmd.params.value("disableSmoothing",false).toBool());disableSmooth->setToolTip(QObject::tr("A câmera salta diretamente para o alvo e, se estiver seguindo, acompanha sem interpolação."));form->addRow(disableSmooth);
        auto choosePosition=[&]{
            if(!ed.doc())return;
            QDialog picker(&d);picker.setWindowTitle(QObject::tr("Selecionar alvo da câmera"));picker.resize(700,500);auto* pv=new QVBoxLayout(&picker);
            auto* note=new QLabel(QObject::tr("Clique no mapa onde a câmera deve focar."),&picker);pv->addWidget(note);
            auto* map=new MapDestinationPreview(ed,&picker);map->setMapId(ed.doc()->id);map->setSelection(QPoint(qRound(x->value()),qRound(y->value())));map->setMarkerLabel(QObject::tr("Câmera"));pv->addWidget(map,1);
            auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&picker);pv->addWidget(buttons);
            QObject::connect(buttons,&QDialogButtonBox::accepted,&picker,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&picker,&QDialog::reject);
            if(picker.exec()==QDialog::Accepted){const QPoint cell=map->selection();x->setValue(cell.x());y->setValue(cell.y());QSignalBlocker blocker(target);target->setCurrentIndex(target->findData(QStringLiteral("position")));}
        };
        QObject::connect(pickCameraTarget,&QPushButton::clicked,&d,choosePosition);
        QObject::connect(target,&QComboBox::currentIndexChanged,&d,[&,choosePosition](int){if(target->currentData().toString()==QLatin1String("position"))choosePosition();});
    }
    if(t==QLatin1String("ludo.camera.zoomOnly")){zoom=new QDoubleSpinBox(&d);zoom->setRange(.25,8);zoom->setSingleStep(.25);zoom->setSuffix(QObject::tr("×"));zoom->setValue(cmd.params.value("zoom",2).toDouble());form->addRow(QObject::tr("Aproximação:"),zoom);disableSmooth=new QCheckBox(QObject::tr("Aplicar imediatamente"),&d);disableSmooth->setChecked(cmd.params.value("disableSmoothing",false).toBool());form->addRow(disableSmooth);}
    if(t.startsWith("ludo.camera")&&t!="ludo.camera.save"){duration=new QSpinBox(&d);duration->setRange(0,3600);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(cmd.params.value("duration",30).toInt());ease=new QComboBox(&d);ease->addItem(QObject::tr("Suave"),"smooth");ease->addItem(QObject::tr("Deslizamento suave"),"slide");ease->addItem(QObject::tr("Acelerar"),"easeIn");ease->addItem(QObject::tr("Desacelerar"),"easeOut");ease->addItem(QObject::tr("Linear"),"linear");ease->setCurrentIndex(qMax(0,ease->findData(cmd.params.value("ease","smooth"))));form->addRow(QObject::tr("Duração:"),duration);form->addRow(QObject::tr("Movimento:"),ease);}
    if(disableSmooth&&duration&&ease){duration->setEnabled(!disableSmooth->isChecked());ease->setEnabled(!disableSmooth->isChecked());QObject::connect(disableSmooth,&QCheckBox::toggled,&d,[=](bool on){duration->setEnabled(!on);ease->setEnabled(!on);});}
    if(t=="ludo.sprite.fade"||t=="ludo.sprite.phantom"){target=targetCombo();target->setCurrentIndex(qMax(0,target->findData(cmd.params.value("target","self"))));form->addRow(QObject::tr("Alvo:"),target);if(t.endsWith("fade")){opacity=new QSpinBox(&d);opacity->setRange(0,255);opacity->setValue(cmd.params.value("opacity",0).toInt());duration=new QSpinBox(&d);duration->setRange(0,3600);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(cmd.params.value("duration",30).toInt());form->addRow(QObject::tr("Opacidade final:"),opacity);form->addRow(QObject::tr("Duração:"),duration);}else{phantomMode=new QComboBox(&d);phantomMode->addItem(QObject::tr("Aparece perto e desaparece longe"),QStringLiteral("visibleNear"));phantomMode->addItem(QObject::tr("Desaparece perto e aparece longe"),QStringLiteral("visibleFar"));phantomMode->setCurrentIndex(qMax(0,phantomMode->findData(cmd.params.value("mode","visibleNear"))));nearDistance=new QDoubleSpinBox(&d);nearDistance->setRange(0,100);nearDistance->setSuffix(QObject::tr(" tiles"));nearDistance->setValue(cmd.params.value("near",1).toDouble());distance=new QDoubleSpinBox(&d);distance->setRange(.1,100);distance->setSuffix(QObject::tr(" tiles"));distance->setValue(cmd.params.value("distance",6).toDouble());minimum=new QSpinBox(&d);minimum->setRange(0,255);minimum->setValue(cmd.params.value("minimum",32).toInt());maximum=new QSpinBox(&d);maximum->setRange(0,255);maximum->setValue(cmd.params.value("maximum",255).toInt());phantomSmoothness=new QSpinBox(&d);phantomSmoothness->setRange(0,100);phantomSmoothness->setSuffix(QObject::tr("%"));phantomSmoothness->setValue(cmd.params.value("smoothness",100).toInt());form->addRow(QObject::tr("Comportamento:"),phantomMode);form->addRow(QObject::tr("Distância inicial:"),nearDistance);form->addRow(QObject::tr("Distância final:"),distance);form->addRow(QObject::tr("Opacidade mínima:"),minimum);form->addRow(QObject::tr("Opacidade máxima:"),maximum);form->addRow(QObject::tr("Suavidade da transição:"),phantomSmoothness);}}
    const bool spriteTransform=t==QLatin1String("ludo.sprite.offset")||t==QLatin1String("ludo.sprite.clearOffset")||t==QLatin1String("ludo.sprite.zoom")||t==QLatin1String("ludo.sprite.resetZoom")||t==QLatin1String("ludo.sprite.shake");
    if(spriteTransform){target=targetCombo();target->setCurrentIndex(qMax(0,target->findData(cmd.params.value("target","self"))));form->addRow(QObject::tr("Alvo:"),target);
        if(t==QLatin1String("ludo.sprite.offset")||t==QLatin1String("ludo.sprite.shake")){x=new QDoubleSpinBox(&d);y=new QDoubleSpinBox(&d);for(auto* s:{x,y}){s->setRange(-9999,9999);s->setDecimals(2);s->setSuffix(QObject::tr(" px"));}x->setValue(cmd.params.value("x",t.endsWith(QLatin1String("shake"))?4.0:0.0).toDouble());y->setValue(cmd.params.value("y",0.0).toDouble());form->addRow(t.endsWith(QLatin1String("shake"))?QObject::tr("Intensidade X:"):QObject::tr("Ajuste horizontal:"),x);form->addRow(t.endsWith(QLatin1String("shake"))?QObject::tr("Intensidade Y:"):QObject::tr("Ajuste vertical:"),y);}
        if(t==QLatin1String("ludo.sprite.zoom")){scaleX=new QDoubleSpinBox(&d);scaleY=new QDoubleSpinBox(&d);for(auto* s:{scaleX,scaleY}){s->setRange(.01,16);s->setDecimals(2);s->setSingleStep(.05);s->setSuffix(QObject::tr("×"));}scaleX->setValue(cmd.params.value("scaleX",1.0).toDouble());scaleY->setValue(cmd.params.value("scaleY",1.0).toDouble());form->addRow(QObject::tr("Escala X:"),scaleX);form->addRow(QObject::tr("Escala Y:"),scaleY);}
        if(t==QLatin1String("ludo.sprite.shake")){shakeFrequency=new QDoubleSpinBox(&d);shakeFrequency->setRange(.5,60);shakeFrequency->setDecimals(1);shakeFrequency->setSuffix(QObject::tr(" Hz"));shakeFrequency->setValue(cmd.params.value("frequency",12.0).toDouble());form->addRow(QObject::tr("Frequência:"),shakeFrequency);}
        duration=new QSpinBox(&d);duration->setRange(0,3600);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(cmd.params.value("duration",30).toInt());form->addRow(QObject::tr("Duração:"),duration);
    }
    if(duration){wait=new QCheckBox(QObject::tr("Esperar o efeito terminar"),&d);wait->setChecked(cmd.params.value("wait",false).toBool());form->addRow(wait);}v->addLayout(form);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);if(d.exec()!=QDialog::Accepted)return false;if(target)cmd.params["target"]=target->currentData();if(x)cmd.params["x"]=x->value();if(y)cmd.params["y"]=y->value();if(zoom)cmd.params["zoom"]=zoom->value();if(scaleX)cmd.params["scaleX"]=scaleX->value();if(scaleY)cmd.params["scaleY"]=scaleY->value();if(shakeFrequency)cmd.params["frequency"]=shakeFrequency->value();if(duration)cmd.params["duration"]=duration->value();if(ease)cmd.params["ease"]=ease->currentData();if(follow)cmd.params["follow"]=follow->isChecked();if(followSpeed)cmd.params["followSpeed"]=followSpeed->value();if(deadzone)cmd.params["deadzone"]=deadzone->value();if(disableSmooth)cmd.params["disableSmoothing"]=disableSmooth->isChecked();if(wait)cmd.params["wait"]=wait->isChecked();if(opacity)cmd.params["opacity"]=opacity->value();if(phantomMode)cmd.params["mode"]=phantomMode->currentData();if(nearDistance)cmd.params["near"]=nearDistance->value();if(distance)cmd.params["distance"]=distance->value();if(minimum)cmd.params["minimum"]=minimum->value();if(maximum)cmd.params["maximum"]=maximum->value();if(phantomSmoothness)cmd.params["smoothness"]=phantomSmoothness->value();return true;
}

namespace {
QColor eventCommandColor(const QString& type, bool darkTheme)
{
    const auto color=[darkTheme](const char* dark,const char* light){return QColor(QString::fromLatin1(darkTheme?dark:light));};
    if(type==QLatin1String("if")||type==QLatin1String("else")||type==QLatin1String("endIf")||
       type.startsWith(QLatin1String("loop."))||type==QLatin1String("label")||
       type==QLatin1String("jump")||type==QLatin1String("wait")||type==QLatin1String("wait.until")||type.startsWith(QLatin1String("parallel."))||type==QLatin1String("common.call")||type==QLatin1String("common.reserve")||
       type==QLatin1String("map.event.call")||type==QLatin1String("flow.exit")||type.startsWith(QLatin1String("repeat."))||
       type==QLatin1String("common.local.set")||type==QLatin1String("common.return"))
        return color("#8ec5ff","#155a9c");
    if(type.startsWith(QLatin1String("switch."))||type.startsWith(QLatin1String("selfSwitch."))||
       type.startsWith(QLatin1String("variable.")))return color("#ff9eb5","#9f2448");
    if(type==QLatin1String("message")||type.startsWith(QLatin1String("subtitle."))||
       type.startsWith(QLatin1String("choice."))||type.startsWith(QLatin1String("input.")))
        return color("#d8b4fe","#6d28a8");
    if(type.startsWith(QLatin1String("move.route")))return color("#ffbd7a","#9a4b00");
    if(type.startsWith(QLatin1String("picture.")))return color("#8ee6a7","#21743a");
    if(type.startsWith(QLatin1String("audio.")))return color("#d1a8ff","#7133a5");
    if(type.startsWith(QLatin1String("fog."))||type.startsWith(QLatin1String("weather."))||
       type.startsWith(QLatin1String("ludo.screen."))||type.startsWith(QLatin1String("ludo.filter.")))return color("#75d9d0","#08736a");
    if(type.startsWith(QLatin1String("ludo.camera."))||type.startsWith(QLatin1String("ludo.sprite.")))
        return color("#7ddcff","#006b91");
    if(type.startsWith(QLatin1String("map."))||type.startsWith(QLatin1String("game."))||
       type.startsWith(QLatin1String("battle."))||type.startsWith(QLatin1String("shop."))||
       type.startsWith(QLatin1String("quest."))||type.startsWith(QLatin1String("party."))||
       type.startsWith(QLatin1String("inventory."))||type.startsWith(QLatin1String("actor.")))
        return color("#ffd875","#815a00");
    if(type.startsWith(QLatin1String("localization.")))return color("#9fd3ff","#245b91");
    return color("#d6d6d6","#404040");
}

class CommandDragList final : public QListWidget
{
public:
    explicit CommandDragList(QWidget* parent=nullptr) : QListWidget(parent)
    {
        setAcceptDrops(true);
        setDragEnabled(true);
        setDropIndicatorShown(false); // indicador próprio, mais visível no tema escuro
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
    }
    std::function<void(int,int,bool)> moveCommand;
    std::function<void(QListWidgetItem*)> toggleCollapsed;
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QListWidgetItem* hit = itemAt(event->position().toPoint());
        if (hit && (event->modifiers() & Qt::ControlModifier) && toggleCollapsed) {
            const QString kind = hit->data(Qt::UserRole + 3).toString();
            if (kind == QLatin1String("top") || kind == QLatin1String("groupHeader")) {
                setCurrentItem(hit); toggleCollapsed(hit); event->accept(); return;
            }
        }
        QListWidget::mousePressEvent(event);
    }
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem* item=currentItem();
        if(!item||item->data(Qt::UserRole+3).toString()!=QLatin1String("top"))return;
        const int source=item->data(Qt::UserRole).toInt();
        if(source<0)return;
        auto* mime=new QMimeData;
        mime->setData(QStringLiteral("application/x-ludo-event-command"),QByteArray::number(source));
        auto* drag=new QDrag(this);drag->setMimeData(mime);drag->exec(Qt::MoveAction);
        clearDropGuide();
    }
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if(e->mimeData()->hasFormat(QStringLiteral("application/x-ludo-event-command")))e->acceptProposedAction();
        else QListWidget::dragEnterEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if(!e->mimeData()->hasFormat(QStringLiteral("application/x-ludo-event-command"))){clearDropGuide();QListWidget::dragMoveEvent(e);return;}
        QListWidgetItem* target=itemAt(e->position().toPoint());
        if(!target&&count()>0)target=item(count()-1);
        const QString kind=target?target->data(Qt::UserRole+3).toString():QString();
        if(target&&(kind==QLatin1String("top")||kind==QLatin1String("branchBoundary"))){
            const QRect r=visualItemRect(target);
            m_dropRow=row(target);
            m_dropAfter=e->position().y()>r.center().y();
            viewport()->update();
            e->acceptProposedAction();
        } else { clearDropGuide(); e->ignore(); }
    }
    void dragLeaveEvent(QDragLeaveEvent* e) override
    {
        clearDropGuide();
        QListWidget::dragLeaveEvent(e);
    }
    void dropEvent(QDropEvent* e) override
    {
        if(!e->mimeData()->hasFormat(QStringLiteral("application/x-ludo-event-command"))){clearDropGuide();QListWidget::dropEvent(e);return;}
        QListWidgetItem* target=itemAt(e->position().toPoint());
        bool forceAfter=false;
        if(!target&&count()>0){target=item(count()-1);forceAfter=true;}
        if(!target){clearDropGuide();return;}
        const QString kind=target->data(Qt::UserRole+3).toString();
        if(kind!=QLatin1String("top")&&kind!=QLatin1String("branchBoundary")){clearDropGuide();return;}
        bool ok=false;const int source=e->mimeData()->data(QStringLiteral("application/x-ludo-event-command")).toInt(&ok);
        const int targetCommand=target->data(Qt::UserRole).toInt();
        if(!ok||source<0||targetCommand<0){clearDropGuide();return;}
        const bool after=forceAfter||e->position().y()>visualItemRect(target).center().y();
        clearDropGuide();
        if(moveCommand)moveCommand(source,targetCommand,after);
        e->setDropAction(Qt::MoveAction);e->accept();
    }
    void paintEvent(QPaintEvent* e) override
    {
        QListWidget::paintEvent(e);
        QPainter decoration(viewport()); decoration.setRenderHint(QPainter::Antialiasing, true);
        for (int rowIndex = 0; rowIndex < count(); ++rowIndex) {
            QListWidgetItem* current = item(rowIndex); if (!current) continue;
            const QRect itemRect = visualItemRect(current); if (!itemRect.intersects(viewport()->rect())) continue;
            const int depth = current->data(Qt::UserRole + 5).toInt();
            const QColor groupColor = current->data(Qt::UserRole + 6).value<QColor>();
            if (groupColor.isValid()) { decoration.setPen(QPen(groupColor, 3)); decoration.drawLine(2, itemRect.top()+1, 2, itemRect.bottom()-1); }
            if (depth > 0) {
                decoration.setPen(QPen(QColor(105,145,190,95), 1));
                for (int d = 0; d < qMin(depth, 12); ++d) decoration.drawLine(10+d*7, itemRect.top(), 10+d*7, itemRect.bottom());
            }
            // Minimap estrutural: uma marca compacta por comando no extremo direito.
            const int miniWidth = qBound(4, 4 + depth * 2, 24);
            decoration.fillRect(QRect(viewport()->width()-miniWidth-3, itemRect.center().y(), miniWidth, 2), QColor(115,175,225,150));
        }
        decoration.end();
        if(m_dropRow<0||m_dropRow>=count())return;
        QListWidgetItem* target=item(m_dropRow);if(!target)return;
        const QRect r=visualItemRect(target);if(!r.isValid())return;
        const int y=m_dropAfter?r.bottom():r.top();
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing,true);
        const QColor accent(90,175,255,245);
        p.setPen(QPen(accent,3));
        p.drawLine(5,y,viewport()->width()-5,y);
        QPolygon left;left<<QPoint(5,y)<<QPoint(12,y-5)<<QPoint(12,y+5);
        p.setPen(Qt::NoPen);p.setBrush(accent);p.drawPolygon(left);
        const QString text=m_dropAfter?tr("Inserir abaixo"):tr("Inserir acima");
        QFont f=p.font();f.setPixelSize(qMax(9,f.pixelSize()>0?f.pixelSize():11));f.setBold(true);p.setFont(f);
        const int tw=p.fontMetrics().horizontalAdvance(text)+12;
        const int th=p.fontMetrics().height()+4;
        const int tx=qMax(18,viewport()->width()-tw-8);
        const int ty=qBound(1,y-th/2,viewport()->height()-th-1);
        p.setBrush(QColor(25,45,65,235));p.setPen(QPen(accent,1));p.drawRoundedRect(QRect(tx,ty,tw,th),4,4);
        p.setPen(QColor(225,242,255));p.drawText(QRect(tx+6,ty,tw-12,th),Qt::AlignVCenter|Qt::AlignLeft,text);
    }
private:
    void clearDropGuide(){if(m_dropRow<0)return;m_dropRow=-1;viewport()->update();}
    int m_dropRow=-1;
    bool m_dropAfter=false;
};
}

const core::CommonEvent* CommandListWidget::commonContext() const
{
    for (const core::CommonEvent& common : ed.commonEvents)
        if (&common.commands == &m_cmds) return &common;
    return nullptr;
}

core::ProjectReferenceLocation CommandListWidget::commandLocation(int index) const
{
    core::ProjectReferenceLocation location;location.commandIndex=index;
    if(const core::CommonEvent* common=commonContext()){location.ownerType=QStringLiteral("commonEvent");location.ownerId=common->id;location.ownerName=common->name;return location;}
    for(const core::MapDoc& map:ed.docs)for(const core::MapEvent& event:map.events)for(int page=0;page<event.pages.size();++page)if(&event.pages.at(page).commands==&m_cmds){location.ownerType=QStringLiteral("mapEvent");location.ownerId=event.id;location.ownerName=QStringLiteral("%1 / %2").arg(map.name,event.name);location.mapId=map.id;location.pageIndex=page;return location;}
    location.ownerType=QStringLiteral("commandList");location.ownerName=tr("Lista aninhada");return location;
}

CommandListWidget::CommandListWidget(core::Editor& editorRef, QVector<core::EventCommand>& cmds,
                                     QWidget* parent)
    : QWidget(parent), ed(editorRef), m_cmds(cmds)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* left = new QWidget(this);
    auto* v = new QVBoxLayout(left);
    v->setContentsMargins(0, 0, 0, 0);
    auto* commandDragList = new CommandDragList(left);
    m_lista = commandDragList;
    // RC2.54: a lista volta a ser direta. A hierarquia lógica continua
    // representada por recuo, mas sem accordion/cartões que escondam comandos.
    m_lista->setAlternatingRowColors(false);
    m_lista->setWordWrap(true);
    m_lista->setUniformItemSizes(false);
    m_lista->setMinimumHeight(150);
    QFont commandFont=m_lista->font();
    if(commandFont.pointSizeF()>0)commandFont.setPointSizeF(commandFont.pointSizeF()+1.0);
    else commandFont.setPixelSize(qMax(13,commandFont.pixelSize()+1));
    m_lista->setFont(commandFont);
    m_lista->setSpacing(0);
    m_lista->setStyleSheet(QStringLiteral(
        "QListWidget::item{padding:6px 5px;border-bottom:1px solid rgba(127,127,127,28);}"
        "QListWidget::item:selected{color:white;}"));
    v->addWidget(m_lista, 1);

    // RC2.64: a lista de comandos não reserva mais uma coluna para preview.
    // O conteúdo estrutural (hoje usado por Escolhas) é preparado aqui e
    // mostrado somente quando o autor clicar em "Ver prévia".
    m_previewLateral = new NarrativePreviewWidget(ed, this);
    m_previewLateralDialog = new CommandPreviewDialog(tr("Prévia narrativa"), m_previewLateral, this);

    auto* editar = new QPushButton(tr("Editar…"), this);
    auto* remover = new QPushButton(tr("Remover"), this);
    auto* subir = new QPushButton(QStringLiteral("▲"), this);
    auto* descer = new QPushButton(QStringLiteral("▼"), this);
    subir->setFixedWidth(32);
    descer->setFixedWidth(32);
    auto* linha = new QHBoxLayout;
    linha->addWidget(editar);
    linha->addWidget(remover);
    m_previewLateralButton = new QPushButton(tr("Ver prévia"), this);
    m_previewLateralButton->hide();
    linha->addWidget(m_previewLateralButton);
    m_playNarrativeButton = new QPushButton(tr("▶ Reproduzir prévia"), this);
    m_playNarrativeButton->hide(); linha->addWidget(m_playNarrativeButton);
    linha->addStretch(1);
    linha->addWidget(subir);
    linha->addWidget(descer);
    v->addLayout(linha);
    m_structureBreadcrumb = new QLabel(this);
    m_structureBreadcrumb->setWordWrap(true);
    m_structureBreadcrumb->setStyleSheet(QStringLiteral("color:#8fa8c8;padding:2px 5px;"));
    v->insertWidget(0, m_structureBreadcrumb);

    // RC2.85.2: a lista do evento permanece sempre visível. A coluna direita
    // contém somente Inspector | Comandos. O catálogo no-code é a via principal
    // para inserir comandos; previews continuam sob demanda, em seus próprios
    // diálogos, nunca como uma terceira aba permanente do Event Editor.
    root->addWidget(left,3);

    m_commandSideTabs=new QTabWidget(this);
    m_commandSideTabs->setMinimumWidth(300);
    m_commandInspector=new CommandInspector(ed,m_commandSideTabs);
    m_commandSideTabs->addTab(m_commandInspector,tr("Inspector"));

    auto* commandBrowserPage=new QWidget(m_commandSideTabs);
    auto* commandBrowserLayout=new QVBoxLayout(commandBrowserPage);
    commandBrowserLayout->setContentsMargins(8,8,8,8);
    commandBrowserLayout->setSpacing(8);
    auto* categoryLabel=new QLabel(tr("Categoria"),commandBrowserPage);
    auto* categoryCombo=new QComboBox(commandBrowserPage);
    categoryCombo->setAccessibleName(tr("Categoria de comandos"));
    categoryCombo->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    commandBrowserLayout->addWidget(categoryLabel);
    commandBrowserLayout->addWidget(categoryCombo);

    auto* commandScroll=new QScrollArea(commandBrowserPage);
    commandScroll->setWidgetResizable(true);
    commandScroll->setFrameShape(QFrame::NoFrame);
    commandScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* commandButtonsHost=new QWidget(commandScroll);
    auto* commandButtonsLayout=new QVBoxLayout(commandButtonsHost);
    commandButtonsLayout->setContentsMargins(0,2,0,2);
    commandButtonsLayout->setSpacing(7);
    commandScroll->setWidget(commandButtonsHost);
    commandBrowserLayout->addWidget(commandScroll,1);

    const QVector<CommandCatalogEntry> commandEntries=commandCatalogForEditor(ed);
    auto categoryForEntry=[this](const CommandCatalogEntry& entry){
        if(entry.type.startsWith(QLatin1String("plugin.call:"))){
            if(entry.path.size()>=3)return QStringLiteral("%1 · %2").arg(entry.path.at(1),entry.path.at(2));
            if(entry.path.size()>=2)return entry.path.at(1);
            return tr("Extensões");
        }
        return entry.path.isEmpty()?tr("Outros"):entry.path.first();
    };
    QStringList commandCategories;
    for(const CommandCatalogEntry& entry:commandEntries){
        const QString category=categoryForEntry(entry);
        if(!commandCategories.contains(category))commandCategories.push_back(category);
    }
    categoryCombo->addItems(commandCategories);

    auto rebuildCommandButtons=[this,commandEntries,categoryCombo,commandButtonsLayout,categoryForEntry]{
        while(QLayoutItem* item=commandButtonsLayout->takeAt(0)){
            if(QWidget* widget=item->widget())delete widget;
            delete item;
        }
        const QString selectedCategory=categoryCombo->currentText();
        int visibleCount=0;
        for(const CommandCatalogEntry& entry:commandEntries){
            if(categoryForEntry(entry)!=selectedCategory)continue;
            auto* button=new QPushButton(entry.label);
            button->setMinimumHeight(42);
            button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            button->setCursor(Qt::PointingHandCursor);
            button->setToolTip(entry.path.join(QStringLiteral(" › ")));
            if(!entry.iconPath.trimmed().isEmpty())button->setIcon(QIcon(entry.iconPath));
            connect(button,&QPushButton::clicked,this,[this,type=entry.type]{adicionar(type);});
            commandButtonsLayout->addWidget(button);
            ++visibleCount;
        }
        if(visibleCount==0){
            auto* empty=new QLabel(tr("Nenhum comando disponível nesta categoria."));
            empty->setWordWrap(true);
            empty->setAlignment(Qt::AlignCenter);
            commandButtonsLayout->addWidget(empty);
        }
        commandButtonsLayout->addStretch(1);
    };
    connect(categoryCombo,&QComboBox::currentTextChanged,this,[rebuildCommandButtons](const QString&){rebuildCommandButtons();});
    rebuildCommandButtons();

    const int commandsTab=m_commandSideTabs->addTab(commandBrowserPage,tr("Comandos"));
    m_commandSideTabs->setCurrentIndex(commandsTab);
    root->addWidget(m_commandSideTabs,2);
    connect(m_commandInspector, &CommandInspector::navigateRequested, this,
            [this](const core::ProjectReferenceLocation& location) {
        QWidget* current = this;
        while (current) {
            if (auto* mainWindow = qobject_cast<MainWindow*>(current)) {
                mainWindow->openReferenceLocation(location);
                return;
            }
            current = current->parentWidget();
        }
    });
    auto undo=std::make_shared<QVector<QVector<core::EventCommand>>>();auto redo=std::make_shared<QVector<QVector<core::EventCommand>>>();auto checkpoint=[this,undo,redo]{undo->push_back(m_cmds);if(undo->size()>100)undo->removeFirst();redo->clear();};
    auto topLevelSelected=[this]{return m_lista&&m_lista->currentItem()&&m_lista->currentItem()->data(Qt::UserRole+3).toString()==QLatin1String("top");};
    auto commandSpan=[this](int i){
        if(i<0||i>=m_cmds.size())return QPair<int,int>(i,i);
        const QString type=m_cmds.at(i).type;
        const int end=(type==QLatin1String("if")||type==QLatin1String("loop.begin")||type==QLatin1String("repeat.begin")||type==QLatin1String("database.each")||type==QLatin1String("parallel.begin"))?fimDoBloco(i):i;
        return QPair<int,int>(i,qMax(i,end));
    };

    commandDragList->moveCommand=[this,checkpoint,commandSpan](int source,int target,bool after){
        if(source<0||source>=m_cmds.size()||target<0||target>=m_cmds.size())return;
        const QPair<int,int> span=commandSpan(source);
        if(target>=span.first&&target<=span.second)return;
        int insertAt=target+(after?1:0);
        if(insertAt>=span.first&&insertAt<=span.second+1)return;
        const int requestedInsert = target + (after ? 1 : 0);
        QString structuralProblem;
        if (!structurallyValidMove(m_cmds, span.first, span.second, requestedInsert, &structuralProblem)) {
            QMessageBox::warning(this, tr("Movimento estrutural inválido"), structuralProblem); return;
        }
        checkpoint();
        QVector<core::EventCommand> block;
        for(int i=span.first;i<=span.second;++i)block.push_back(m_cmds.at(i));
        const int count=span.second-span.first+1;
        for(int i=span.second;i>=span.first;--i)m_cmds.removeAt(i);
        if(insertAt>span.second)insertAt-=count;
        insertAt=qBound(0,insertAt,m_cmds.size());
        for(int i=0;i<block.size();++i)m_cmds.insert(insertAt+i,block.at(i));

        recarregar();
        selecionarIndice(insertAt);
    };
    commandDragList->toggleCollapsed = [this, checkpoint](QListWidgetItem* item) {
        if (!item) return; const QString kind = item->data(Qt::UserRole + 3).toString();
        const int first = item->data(Qt::UserRole).toInt(); if (first < 0 || first >= m_cmds.size()) return;
        checkpoint();
        if (kind == QLatin1String("groupHeader")) {
            const int last = qBound(first, item->data(Qt::UserRole + 2).toInt(), m_cmds.size() - 1);
            QVariantMap group = m_cmds.at(first).editorMetadata.value(QStringLiteral("group")).toMap();
            group[QStringLiteral("collapsed")] = !group.value(QStringLiteral("collapsed"), false).toBool();
            for (int i = first; i <= last; ++i) m_cmds[i].editorMetadata[QStringLiteral("group")] = group;
        } else {
            const QVector<CommandStructureInfo> info = analyzeCommandStructure(m_cmds);
            if (first >= info.size() || !info.at(first).beginsSection || info.at(first).matchingIndex <= first) return;
            m_cmds[first].editorMetadata[QStringLiteral("collapsed")] = !m_cmds.at(first).editorMetadata.value(QStringLiteral("collapsed"), false).toBool();
        }
        recarregar(); selecionarIndice(first);
    };

    auto editarAtual = [this,checkpoint] {
        if(!m_lista || !m_lista->currentItem() || m_lista->currentItem()->data(Qt::UserRole+3).toString()!=QLatin1String("top"))return;
        const int i = selecionado();
        if (i < 0) return;
        core::EventCommand c = m_cmds[i];
        bool ok = false;
        if (c.type == QLatin1String("message") || c.type == QLatin1String("subtitle.show"))
            ok = TextCommandDialog(ed, c, this).exec() == QDialog::Accepted;
        else if(c.type==QLatin1String("subtitle.enqueue"))ok=SubtitleCommandDialog(ed,c,this).exec()==QDialog::Accepted;
        else if (c.type == QLatin1String("picture.eraseAll"))
            ok = true;
        else if (c.type.startsWith(QLatin1String("picture.")))
            ok = PictureCommandDialog(ed, c.type, c, this).exec() == QDialog::Accepted;
        else if (c.type == QLatin1String("move.route")) {
            core::MoveRoute r=core::moveRouteFromMap(c.params.value(QStringLiteral("route")).toMap());
            MoveRouteDialog d(ed,r,this,true);ok=d.exec()==QDialog::Accepted;if(ok)c.params[QStringLiteral("route")]=core::moveRouteToMap(d.route());
        } else if(c.type==QLatin1String("move.route.control")) ok=editMoveRouteControl(ed,c,this);
        else if(c.type.startsWith(QLatin1String("fog."))) ok=editFogCommand(ed,c,this);
        else if((c.type.startsWith(QLatin1String("map.")) && c.type != QLatin1String("map.event.call") && !c.type.startsWith(QLatin1String("map.runtime.")))||c.type.startsWith(QLatin1String("game."))||c.type.startsWith(QLatin1String("battle."))||c.type.startsWith(QLatin1String("shop.")))ok=editGameCommand(ed,c,this,commonContext());
        else if(c.type.startsWith(QLatin1String("weather.")))ok=editWeatherCommand(ed,c,this);
        else if(c.type==QLatin1String("localization.set"))ok=editLocalizationCommand(ed,c,this);
        else if(c.type.startsWith(QLatin1String("quest.")))ok=editQuestCommand(ed,c,this,commonContext());
        else if(c.type==QLatin1String("plugin.call"))ok=editPluginCommand(ed,c,this);
        else if(c.type.startsWith(QLatin1String("party."))||c.type.startsWith(QLatin1String("inventory."))||c.type.startsWith(QLatin1String("actor.")))ok=editRpgCommand(ed,c,this,commonContext());
        else if(c.type.startsWith(QLatin1String("audio.")))ok=editAudioCommand(ed,c,this);
        else if(c.type.startsWith(QLatin1String("voice."))||c.type.startsWith(QLatin1String("portrait.")))ok=editPortraitVoiceCommand(ed,c,this);
        else if(c.type.startsWith(QLatin1String("bubble."))||c.type.startsWith(QLatin1String("notification.")))ok=editSpeechBubbleCommand(ed,c,this);
        else if(c.type==QLatin1String("subtitle.configure")){
            core::SubtitleStyle style = c.params.value(QStringLiteral("style")).toMap().isEmpty()
                ? ed.subtitleStyle
                : core::SubtitleStyle::fromJson(QJsonObject::fromVariantMap(c.params.value(QStringLiteral("style")).toMap()));
            SubtitleStyleDialog d(ed, style, this);
            d.setWindowTitle(tr("Configurar legendas"));
            ok=d.exec()==QDialog::Accepted;
            if(ok)c.params[QStringLiteral("style")]=style.toJson().toVariantMap();
        }
        else if(c.type.startsWith(QLatin1String("ludo."))||c.type==QLatin1String("comment"))ok=editLudoCommand(ed,c,this);
        else if (c.type == QLatin1String("if")) {
            ConditionBuilderDialog builder(ed, core::conditionTreeFromCommandParams(c.params), commonContext(), this);
            ok = builder.exec() == QDialog::Accepted;
            if (ok) c.params = {{QStringLiteral("conditionTree"), builder.conditionTree()}};
        } else
            ok = LogicCommandDialog(ed, c.type, c, this, commonContext()).exec() == QDialog::Accepted;
        if (!ok) return;
        if(c.type==QLatin1String("if"))c.params.remove(QStringLiteral("createElse"));checkpoint();m_cmds[i] = c;
        recarregar();
        selecionarIndice(i);
    };
    connect(editar, &QPushButton::clicked, this, editarAtual);
    connect(m_lista, &QListWidget::itemDoubleClicked, this, editarAtual);
    connect(remover, &QPushButton::clicked, this, [this,checkpoint,topLevelSelected,commandSpan] {
        if(!topLevelSelected())return;
        const int i = selecionado();
        if (i < 0) return;
        const auto span=commandSpan(i);
        checkpoint();m_cmds.remove(span.first,span.second-span.first+1);
        recarregar();
    });
    connect(subir, &QPushButton::clicked, this, [this,checkpoint,topLevelSelected] {
        if(!topLevelSelected())return;
        const int i = selecionado();
        if (i <= 0) return;
        const QString current=m_cmds.at(i).type, previous=m_cmds.at(i-1).type;
        const auto structural=[](const QString& t){return t==QLatin1String("if")||t==QLatin1String("else")||t==QLatin1String("endIf")||t==QLatin1String("loop.begin")||t==QLatin1String("loop.end")||t==QLatin1String("repeat.begin")||t==QLatin1String("repeat.end")||t==QLatin1String("database.each")||t==QLatin1String("database.each.end")||t==QLatin1String("parallel.begin")||t==QLatin1String("parallel.end");};
        if(structural(current)||structural(previous))return;
        checkpoint();m_cmds.swapItemsAt(i, i - 1);
        recarregar();selecionarIndice(i - 1);
    });
    connect(descer, &QPushButton::clicked, this, [this,checkpoint,topLevelSelected] {
        if(!topLevelSelected())return;
        const int i = selecionado();
        if (i < 0 || i + 1 >= m_cmds.size()) return;
        const QString current=m_cmds.at(i).type, next=m_cmds.at(i+1).type;
        const auto structural=[](const QString& t){return t==QLatin1String("if")||t==QLatin1String("else")||t==QLatin1String("endIf")||t==QLatin1String("loop.begin")||t==QLatin1String("loop.end")||t==QLatin1String("repeat.begin")||t==QLatin1String("repeat.end")||t==QLatin1String("database.each")||t==QLatin1String("database.each.end")||t==QLatin1String("parallel.begin")||t==QLatin1String("parallel.end");};
        if(structural(current)||structural(next))return;
        checkpoint();m_cmds.swapItemsAt(i, i + 1);
        recarregar();selecionarIndice(i + 1);
    });
    auto shortcut=[this](const QKeySequence& key,std::function<void()> fn){auto* s=new QShortcut(key,m_lista);s->setContext(Qt::WidgetWithChildrenShortcut);connect(s,&QShortcut::activated,this,[fn]{fn();});};
    auto copy=[this,topLevelSelected,commandSpan]{
        if(!topLevelSelected())return;const int i=selecionado();if(i<0)return;
        const auto span=commandSpan(i);QVector<core::EventCommand> block;block.reserve(span.second-span.first+1);
        for(int n=span.first;n<=span.second;++n)block.push_back(m_cmds.at(n));
        writeEventCommandClipboard(block);
    };
    auto cut=[this,checkpoint,topLevelSelected,commandSpan]{
        if(!topLevelSelected())return;const int i=selecionado();if(i<0)return;
        const auto span=commandSpan(i);QVector<core::EventCommand> block;block.reserve(span.second-span.first+1);
        for(int n=span.first;n<=span.second;++n)block.push_back(m_cmds.at(n));
        writeEventCommandClipboard(block);
        checkpoint();m_cmds.remove(span.first,span.second-span.first+1);recarregar();
    };
    auto paste=[this,checkpoint,topLevelSelected,commandSpan]{
        const QVector<core::EventCommand> block=readEventCommandClipboard();
        if(block.isEmpty())return;
        const int i=selecionado();
        // Permite colar em Common Event/Evento vazio. Se houver seleção, ela
        // precisa representar um comando top-level para não quebrar branches.
        if(i>=0&&!topLevelSelected())return;
        checkpoint();
        const int at=i>=0?commandSpan(i).second+1:m_cmds.size();int pos=at;for(const core::EventCommand& command:block)m_cmds.insert(pos++,command);
        recarregar();selecionarIndice(at);
    };
    auto duplicate=[this,checkpoint,topLevelSelected,commandSpan]{
        if(!topLevelSelected())return;const int i=selecionado();if(i<0)return;const auto span=commandSpan(i);
        QVector<core::EventCommand> block;for(int n=span.first;n<=span.second;++n)block.push_back(m_cmds.at(n));
        checkpoint();int pos=span.second+1;const int at=pos;for(const core::EventCommand& command:block)m_cmds.insert(pos++,command);
        recarregar();selecionarIndice(at);
    };
    auto del=[this,checkpoint,topLevelSelected,commandSpan]{
        if(!topLevelSelected())return;const int i=selecionado();if(i<0)return;const auto span=commandSpan(i);
        checkpoint();m_cmds.remove(span.first,span.second-span.first+1);recarregar();
    };
    auto insertTemplate=[this,checkpoint,topLevelSelected,commandSpan](const core::CommandTemplate& commandTemplate){
        QVariantMap arguments;
        if(!commandTemplate.parameters.isEmpty()){
            QDialog dialog(this);dialog.setWindowTitle(tr("Inserir template — %1").arg(commandTemplate.name));dialog.resize(660,420);
            auto* layout=new QVBoxLayout(&dialog);auto* description=new QLabel(commandTemplate.description,&dialog);description->setWordWrap(true);layout->addWidget(description);
            auto* form=new QFormLayout;layout->addLayout(form);QVector<QPair<QString,std::shared_ptr<CommonSourceEditorState>>> editors;
            for(const core::CommandTemplateParameter& parameter:commandTemplate.parameters){auto editor=makeCommonSourceEditor(&dialog,ed,parameter.type,parameter.defaultSource,commonContext());form->addRow(parameter.name+QStringLiteral(":"),editor->widget);editors.push_back({parameter.id,editor});}
            auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            if(dialog.exec()!=QDialog::Accepted)return;for(const auto& pair:editors)arguments[pair.first]=pair.second->value();
        }
        const QVector<core::EventCommand> commands=core::instantiateCommandTemplate(commandTemplate,arguments);QString problem;
        if(commands.isEmpty()||!structurallyValidCommands(commands,&problem)){QMessageBox::warning(this,tr("Template inválido"),problem.isEmpty()?tr("O template não possui comandos."):problem);return;}
        const int selected=selecionado();if(selected>=0&&!topLevelSelected())return;const int at=selected>=0?commandSpan(selected).second+1:m_cmds.size();
        checkpoint();int position=at;for(const core::EventCommand& command:commands)m_cmds.insert(position++,command);ed.markDirty();recarregar();selecionarIndice(at);
    };
    auto doUndo=[this,undo,redo]{if(undo->isEmpty())return;redo->push_back(m_cmds);m_cmds=undo->takeLast();recarregar();};auto doRedo=[this,undo,redo]{if(redo->isEmpty())return;undo->push_back(m_cmds);m_cmds=redo->takeLast();recarregar();};
    shortcut(QKeySequence::Copy,copy);shortcut(QKeySequence::Cut,cut);shortcut(QKeySequence::Paste,paste);shortcut(QKeySequence(Qt::CTRL|Qt::Key_D),duplicate);shortcut(QKeySequence::Delete,del);shortcut(QKeySequence(Qt::Key_Return),editarAtual);shortcut(QKeySequence::Undo,doUndo);shortcut(QKeySequence::Redo,doRedo);shortcut(QKeySequence(Qt::Key_F12),[this]{if(m_commandInspector)m_commandInspector->goToCurrentDefinition();});
    m_lista->setContextMenuPolicy(Qt::CustomContextMenu);connect(m_lista,&QListWidget::customContextMenuRequested,this,[=](const QPoint& pos){
        QMenu m(this); QListWidgetItem* current=m_lista->itemAt(pos); if(current)m_lista->setCurrentItem(current);
        m.addAction(tr("Editar"),editarAtual);m.addSeparator();m.addAction(tr("Recortar"),cut);m.addAction(tr("Copiar"),copy);QAction* pa=m.addAction(tr("Colar"),paste);pa->setEnabled(eventCommandClipboardHasCommands());m.addAction(tr("Duplicar"),duplicate);
        m.addSeparator();
        QAction* collapse=m.addAction(tr("Recolher/expandir seção ou grupo"),[=]{if(current)commandDragList->toggleCollapsed(current);});
        const QString kind=current?current->data(Qt::UserRole+3).toString():QString();collapse->setEnabled(kind==QLatin1String("groupHeader")||kind==QLatin1String("top"));
        QAction* createGroup=m.addAction(tr("Criar grupo deste bloco…"),[=]{
            if(!topLevelSelected())return;const int first=selecionado();if(first<0)return;const auto span=commandSpan(first);
            bool ok=false;const QString name=QInputDialog::getText(this,tr("Criar grupo"),tr("Nome do grupo:"),QLineEdit::Normal,tr("Nova seção"),&ok).trimmed();if(!ok||name.isEmpty())return;
            const QColor color=QColorDialog::getColor(QColor(QStringLiteral("#6aa9ff")),this,tr("Cor do grupo"));if(!color.isValid())return;
            checkpoint();CommandGroup group;group.id=QUuid::createUuid().toString(QUuid::WithoutBraces);group.name=name;group.color=color;
            for(int i=span.first;i<=span.second;++i)m_cmds[i].editorMetadata[QStringLiteral("group")]=group.toVariantMap();recarregar();selecionarIndice(span.first);
        });createGroup->setEnabled(topLevelSelected());
        QAction* removeGroup=m.addAction(tr("Remover do grupo"),[=]{if(!current)return;const int index=current->data(Qt::UserRole).toInt();if(index<0||index>=m_cmds.size())return;const QString id=m_cmds.at(index).editorMetadata.value(QStringLiteral("group")).toMap().value(QStringLiteral("id")).toString();if(id.isEmpty())return;checkpoint();for(auto& command:m_cmds)if(command.editorMetadata.value(QStringLiteral("group")).toMap().value(QStringLiteral("id")).toString()==id)command.editorMetadata.remove(QStringLiteral("group"));recarregar();selecionarIndice(index);});
        removeGroup->setEnabled(current&&current->data(Qt::UserRole).toInt()>=0&&current->data(Qt::UserRole).toInt()<m_cmds.size()&&!m_cmds.at(current->data(Qt::UserRole).toInt()).editorMetadata.value(QStringLiteral("group")).toMap().isEmpty());
        m.addSeparator();
        QAction* savePreset=m.addAction(tr("Salvar comando como modelo…"),[=]{
            if(!topLevelSelected())return;const int index=selecionado();if(index<0)return;const QString type=m_cmds.at(index).type;
            const auto structural=[](const QString& value){return value==QLatin1String("if")||value==QLatin1String("else")||value==QLatin1String("endIf")||value==QLatin1String("loop.begin")||value==QLatin1String("loop.end")||value==QLatin1String("repeat.begin")||value==QLatin1String("repeat.end")||value==QLatin1String("database.each")||value==QLatin1String("database.each.end")||value==QLatin1String("parallel.begin")||value==QLatin1String("parallel.end");};
            if(structural(type)){QMessageBox::information(this,tr("Modelo de comando"),tr("Blocos completos de comandos devem ser salvos como um modelo completo, para preservar toda a estrutura."));return;}
            bool ok=false;const QString name=QInputDialog::getText(this,tr("Salvar modelo"),tr("Nome do modelo:"),QLineEdit::Normal,descricao(ed,m_cmds.at(index)),&ok).trimmed();if(!ok||name.isEmpty())return;
            const QString description=QInputDialog::getMultiLineText(this,tr("Salvar modelo"),tr("Descrição:"),QString(),&ok).left(2048);if(!ok)return;
            CommandPreset preset;preset.id=core::idGen();preset.name=name;preset.commandType=type;preset.params=m_cmds.at(index).params;preset.description=description;
            if(!saveCommandPreset(preset))QMessageBox::warning(this,tr("Salvar modelo"),tr("Não foi possível salvar o modelo."));
        });savePreset->setEnabled(topLevelSelected());
        QAction* saveTemplate=m.addAction(tr("Salvar bloco como template…"),[=]{
            if(!topLevelSelected())return;const int first=selecionado();if(first<0)return;const auto span=commandSpan(first);QVector<core::EventCommand> block;for(int i=span.first;i<=span.second;++i)block.push_back(m_cmds.at(i));
            bool ok=false;const QString name=QInputDialog::getText(this,tr("Novo template"),tr("Nome:"),QLineEdit::Normal,tr("Fluxo reutilizável"),&ok).trimmed();if(!ok||name.isEmpty())return;
            const QString description=QInputDialog::getMultiLineText(this,tr("Novo template"),tr("Descrição:"),QString(),&ok).left(4096);if(!ok)return;
            const core::ParameterizedCommandBlock parameterized=core::parameterizeCommandBlock(block,QStringLiteral("templateParameter"));core::CommandTemplate commandTemplate;commandTemplate.name=name;commandTemplate.description=description;commandTemplate.commands=parameterized.commands;commandTemplate.parameters=parameterized.parameters;ed.commandTemplates.push_back(commandTemplate);ed.markDirty();
            QMessageBox::information(this,tr("Template criado"),tr("“%1” foi salvo. Parâmetros detectados: %2.").arg(name).arg(commandTemplate.parameters.size()));
        });saveTemplate->setEnabled(topLevelSelected());
        QMenu* templates=m.addMenu(tr("Inserir template"));for(const core::CommandTemplate& commandTemplate:ed.commandTemplates)templates->addAction(commandTemplate.name,[=]{insertTemplate(commandTemplate);});templates->setEnabled(!ed.commandTemplates.isEmpty()&&(selecionado()<0||topLevelSelected()));
        QAction* manageTemplates=m.addAction(tr("Gerenciar templates…"),[=]{
            if(ed.commandTemplates.isEmpty())return;QStringList names;for(const core::CommandTemplate& commandTemplate:ed.commandTemplates)names.push_back(commandTemplate.name);bool ok=false;const QString chosen=QInputDialog::getItem(this,tr("Gerenciar templates"),tr("Escolha o template que deseja excluir:"),names,0,false,&ok);if(!ok)return;const int index=names.indexOf(chosen);if(index<0)return;if(QMessageBox::question(this,tr("Excluir template"),tr("Excluir “%1” do projeto?").arg(chosen))!=QMessageBox::Yes)return;ed.commandTemplates.removeAt(index);ed.markDirty();
        });manageTemplates->setEnabled(!ed.commandTemplates.isEmpty());
        QAction* extract=m.addAction(tr("Extrair bloco para Evento Comum…"),[=]{
            if(!topLevelSelected()||commonContext())return;const int first=selecionado();if(first<0)return;const auto span=commandSpan(first);QVector<core::EventCommand> block;for(int i=span.first;i<=span.second;++i)block.push_back(m_cmds.at(i));
            bool ok=false;const QString name=QInputDialog::getText(this,tr("Extrair para Evento Comum"),tr("Nome do novo Evento Comum:"),QLineEdit::Normal,tr("Fluxo extraído"),&ok).trimmed();if(!ok||name.isEmpty())return;
            int number=1;for(const core::CommonEvent& common:ed.commonEvents)number=qMax(number,common.number+1);const core::ExtractedCommonEvent extracted=core::buildExtractedCommonEvent(block,name,number);
            const QString detail=tr("O bloco será substituído por uma chamada. Parâmetros externos detectados automaticamente: %1.").arg(extracted.commonEvent.parameters.size());if(QMessageBox::question(this,tr("Confirmar extração"),detail)!=QMessageBox::Yes)return;
            checkpoint();m_cmds.remove(span.first,span.second-span.first+1);m_cmds.insert(span.first,extracted.callCommand);ed.commonEvents.push_back(extracted.commonEvent);ed.markDirty();recarregar();selecionarIndice(span.first);
        });extract->setEnabled(topLevelSelected()&&!commonContext());extract->setToolTip(commonContext()?tr("A extração é iniciada a partir de eventos de mapa; Eventos Comuns já são fluxos reutilizáveis."):QString());
        m.addSeparator();m.addAction(tr("Excluir"),del);m.exec(m_lista->mapToGlobal(pos));
    });
    connect(m_previewLateralButton, &QPushButton::clicked, this, [this] {
        if (m_frameworkPreviewDialog) m_frameworkPreviewDialog->present();
        else if (m_previewLateralDialog) m_previewLateralDialog->present();
    });
    connect(m_playNarrativeButton, &QPushButton::clicked, this, [this] {
        if (!m_previewLateral || !m_previewLateralDialog) return;
        m_previewLateral->play(); m_previewLateralDialog->present();
    });
    connect(m_lista, &QListWidget::currentRowChanged, this, [this](int) {
        const int index=selecionado();const auto structure=analyzeCommandStructure(m_cmds);int begin=index,end=index;
        if(index>=0&&index<structure.size()){
            for(int i=index;i>=0;--i)if(structure.at(i).beginsSection&&structure.at(i).matchingIndex>=index){begin=i;end=structure.at(i).matchingIndex;break;}
        }
        for(int row=0;row<m_lista->count();++row){auto* item=m_lista->item(row);if(!item||item->data(Qt::UserRole+3).toString()==QLatin1String("groupHeader"))continue;const int ci=item->data(Qt::UserRole).toInt();item->setBackground(ci>=begin&&ci<=end&&begin!=end?QBrush(QColor(80,130,190,28)):QBrush());}
        atualizarPreviewLateral();
    });
    recarregar();
}

void CommandListWidget::atualizarPreviewLateral()
{
    if (!m_previewLateralButton || !m_playNarrativeButton || !m_previewLateral) return;
    const int index = selecionado();
    if (m_frameworkPreviewDialog) {
        delete m_frameworkPreviewDialog;
        m_frameworkPreviewDialog = nullptr;
        m_frameworkPreview = nullptr;
    } else if (m_frameworkPreview) {
        delete m_frameworkPreview;
        m_frameworkPreview = nullptr;
    }
    if (index < 0) {
        m_previewLateralButton->hide();
        m_playNarrativeButton->hide(); m_previewLateral->clearCommand();
        if (m_previewLateralDialog) m_previewLateralDialog->hide();
        if(m_commandInspector)m_commandInspector->setCommand(nullptr);
        return;
    }
    const core::EventCommand& command = m_cmds.at(index); m_previewLateral->setCommand(command);
    const core::ProjectReferenceLocation location=commandLocation(index);if(m_commandInspector)m_commandInspector->setCommand(&command,location);
    const bool visualPreview=PreviewFramework::supportsPreview(command);
    if(visualPreview){
        PreviewContext context;context.editor=&ed;context.viewportSize=ed.gameResolution;context.animated=true;
        m_frameworkPreview=PreviewFramework::create(command,context,this);
        if(m_frameworkPreview)m_frameworkPreviewDialog=new CommandPreviewDialog(tr("Prévia do comando"),m_frameworkPreview,this);
    }
    const bool narrative = m_previewLateral->isPlayable();
    m_previewLateralButton->setVisible(visualPreview||narrative);
    m_playNarrativeButton->setVisible(narrative);
    if (!narrative && m_previewLateralDialog) m_previewLateralDialog->hide();
    const auto structure = analyzeCommandStructure(m_cmds);
    if (m_structureBreadcrumb && index < structure.size()) {
        QStringList path = structure.at(index).breadcrumb; path.push_back(descricao(ed, command));
        m_structureBreadcrumb->setText(tr("Estrutura: %1").arg(path.join(QStringLiteral("  ›  "))));
    }
}

int CommandListWidget::selecionado() const
{
    if (!m_lista || !m_lista->currentItem()) return -1;
    const int i = m_lista->currentItem()->data(Qt::UserRole).toInt();
    return (i >= 0 && i < m_cmds.size()) ? i : -1;
}

void CommandListWidget::adicionar(const QString& tipo, int choiceCommand, int choiceBranch)
{
    if(tipo.startsWith(QLatin1String("preset:"))){
        const CommandPreset preset=commandPresetById(tipo.mid(7));if(preset.commandType.isEmpty())return;
        core::EventCommand presetCommand;presetCommand.type=preset.commandType;presetCommand.params=preset.params;
        if(m_lista&&m_lista->currentItem()){
            const int ownerIndex=m_lista->currentItem()->data(Qt::UserRole).toInt();const int branch=m_lista->currentItem()->data(Qt::UserRole+1).toInt();
            if(ownerIndex>=0&&ownerIndex<m_cmds.size()&&branch>=0&&m_cmds.at(ownerIndex).type==QLatin1String("choice.show")){
                QVariantList branches=m_cmds.at(ownerIndex).params.value(QStringLiteral("branches")).toList();while(branches.size()<=branch)branches.push_back(QVariantList());QVariantList nested=branches.at(branch).toList();nested.push_back(core::eventCommandToVariantMap(presetCommand));branches[branch]=nested;m_cmds[ownerIndex].params[QStringLiteral("branches")]=branches;recarregar();selecionarIndice(ownerIndex);return;
            }
        }
        const int selected=selecionado(),at=selected>=0?fimDoBloco(selected)+1:m_cmds.size();QVector<core::EventCommand> candidate=m_cmds;candidate.insert(at,presetCommand);QString problem;
        if(!structurallyValidCommands(candidate,&problem)){QMessageBox::warning(this,tr("Modelo incompatível"),problem);return;}m_cmds=candidate;recarregar();selecionarIndice(at);return;
    }
    core::EventCommand c;
    c.type = tipo;
    bool ok = true;
    if ((tipo == QLatin1String("common.local.set") || tipo == QLatin1String("common.return")) && !commonContext()) {
        QMessageBox::information(this, tr("Comando de Evento Comum"),
                                 tr("Este comando só pode ser usado dentro de um Evento Comum com Assinatura."));
        return;
    }
    if (tipo.startsWith(QLatin1String("plugin.call:"))) {
        const QStringList parts=tipo.split(QLatin1Char(':'));
        if(parts.size()!=3)return;c.type=QStringLiteral("plugin.call");
        c.params={{QStringLiteral("pluginId"),parts[1]},{QStringLiteral("commandId"),parts[2]},
                  {QStringLiteral("arguments"),QVariantMap()}};
        ok=editPluginCommand(ed,c,this);
    } else if (tipo == QLatin1String("text.show") || tipo == QLatin1String("message") ||
        tipo == QLatin1String("subtitle.show")) {
        // "text.show" é só o atalho do menu: o que fica gravado é `message`
        // ou `subtitle.show`, conforme o modo escolhido na janela.
        c.type = (tipo == QLatin1String("subtitle.show")) ? tipo : QStringLiteral("message");
        c.params[QStringLiteral("text")] = QString();
        if (c.type == QLatin1String("message"))
            c.params[QStringLiteral("position")] = QStringLiteral("bottom");
        ok = TextCommandDialog(ed, c, this).exec() == QDialog::Accepted;
    } else if(tipo==QLatin1String("subtitle.enqueue")){
        c.params={{QStringLiteral("text"),QString()},{QStringLiteral("track"),QStringLiteral("dialogue")},{QStringLiteral("duration"),0}};ok=SubtitleCommandDialog(ed,c,this).exec()==QDialog::Accepted;
    } else if(tipo==QLatin1String("subtitle.clearQueue")){
        const QStringList labels{tr("Diálogo"),tr("Notificação"),tr("Sistema"),tr("Personalizada 1"),tr("Personalizada 2")};const QStringList ids{QStringLiteral("dialogue"),QStringLiteral("notification"),QStringLiteral("system"),QStringLiteral("custom1"),QStringLiteral("custom2")};bool chosen=false;const QString label=QInputDialog::getItem(this,tr("Limpar fila"),tr("Track"),labels,0,false,&chosen);ok=chosen;if(ok)c.params[QStringLiteral("track")]=ids.value(labels.indexOf(label));
    } else if (tipo == QLatin1String("subtitle.configure")) {
        core::SubtitleStyle style = ed.subtitleStyle;
        SubtitleStyleDialog d(ed, style, this);
        d.setWindowTitle(tr("Configurar legendas"));
        ok = d.exec() == QDialog::Accepted;
        if (ok) c.params[QStringLiteral("style")] = style.toJson().toVariantMap();
    } else if (tipo == QLatin1String("move.route")) {
        core::MoveRoute r;r.repeat=false;r.target=QStringLiteral("player");
        MoveRouteDialog d(ed,r,this,true);ok=d.exec()==QDialog::Accepted;if(ok)c.params[QStringLiteral("route")]=core::moveRouteToMap(d.route());
    } else if (tipo == QLatin1String("move.route.control")) {
        c.params={{QStringLiteral("target"),QStringLiteral("player")},{QStringLiteral("action"),QStringLiteral("pause")}};
        ok=editMoveRouteControl(ed,c,this);
    } else if (tipo.startsWith(QLatin1String("fog."))) {
        ok=editFogCommand(ed,c,this);
    } else if (tipo.startsWith(QLatin1String("weather."))) {
        c.params = {{QStringLiteral("type"), QStringLiteral("rain")},
                    {QStringLiteral("intensity"), 50}};
        ok=editWeatherCommand(ed,c,this);
    } else if (tipo.startsWith(QLatin1String("quest."))) {
        if(tipo==QLatin1String("quest.start"))c.params[QStringLiteral("target")]=1;
        else if(tipo==QLatin1String("quest.progress")){c.params[QStringLiteral("operation")]=QStringLiteral("add");c.params[QStringLiteral("amount")]=1;}
        ok=editQuestCommand(ed,c,this,commonContext());
    } else if (tipo == QLatin1String("localization.set")) {
        c.params[QStringLiteral("locale")] = ed.localization.defaultLocale;
        ok = editLocalizationCommand(ed, c, this);
    } else if ((tipo.startsWith(QLatin1String("map.")) && tipo != QLatin1String("map.event.call") && !tipo.startsWith(QLatin1String("map.runtime."))) || tipo.startsWith(QLatin1String("game.")) ||
               tipo.startsWith(QLatin1String("battle.")) || tipo.startsWith(QLatin1String("shop."))) {
        if(tipo==QLatin1String("game.restart")||tipo==QLatin1String("game.gameOver")||tipo==QLatin1String("game.returnTitle")){ok=true;}
        else {
        if (tipo == QLatin1String("map.transfer")) {
            c.params[QStringLiteral("useSpawn")] = false;
            c.params[QStringLiteral("direction")] = 0;
        } else {
            c.params[QStringLiteral("slot")] = 1;
        }
        ok=editGameCommand(ed,c,this,commonContext());
        }
    } else if (tipo.startsWith(QLatin1String("party.")) || tipo.startsWith(QLatin1String("inventory.")) ||
               tipo.startsWith(QLatin1String("actor."))) {
        ok=editRpgCommand(ed,c,this,commonContext());
    } else if (tipo.startsWith(QLatin1String("audio."))) {
        ok=editAudioCommand(ed,c,this);
    } else if(tipo.startsWith(QLatin1String("voice."))||tipo.startsWith(QLatin1String("portrait."))){
        ok=editPortraitVoiceCommand(ed,c,this);
    } else if(tipo.startsWith(QLatin1String("bubble."))||tipo.startsWith(QLatin1String("notification."))){
        if(tipo==QLatin1String("bubble.show")){c.params={{QStringLiteral("target"),QStringLiteral("self")},{QStringLiteral("duration"),180},{QStringLiteral("maxWidth"),360},{QStringLiteral("typewriter"),false}};}
        else if(tipo==QLatin1String("notification.show")){c.params={{QStringLiteral("duration"),180},{QStringLiteral("position"),QStringLiteral("top-right")},{QStringLiteral("maxWidth"),360}};}
        else if(tipo==QLatin1String("bubble.hide"))c.params={{QStringLiteral("target"),QStringLiteral("self")}};
        ok=editSpeechBubbleCommand(ed,c,this);
    } else if(tipo.startsWith(QLatin1String("ludo."))||tipo==QLatin1String("comment")){
        if(tipo==QLatin1String("ludo.camera.reset")||tipo==QLatin1String("ludo.camera.restore")){c.params["duration"]=30;c.params["ease"]=QStringLiteral("smooth");c.params["wait"]=true;}
        else if(tipo==QLatin1String("ludo.camera.zoomOnly")){c.params["zoom"]=2.0;c.params["duration"]=30;c.params["ease"]=QStringLiteral("smooth");c.params["wait"]=true;}
        else if(tipo==QLatin1String("ludo.camera.moveOnly")){c.params["target"]=QStringLiteral("player");c.params["duration"]=30;c.params["ease"]=QStringLiteral("smooth");c.params["follow"]=true;c.params["wait"]=true;}
        else if(tipo==QLatin1String("ludo.sprite.fade")){c.params["target"]=QStringLiteral("self");c.params["opacity"]=0;c.params["duration"]=30;c.params["wait"]=true;}
        else if(tipo==QLatin1String("ludo.sprite.phantom")){c.params["target"]=QStringLiteral("self");c.params["mode"]=QStringLiteral("visibleNear");c.params["near"]=1.0;c.params["distance"]=6.0;c.params["minimum"]=32;c.params["maximum"]=255;}
        else if(tipo==QLatin1String("ludo.filter.chromaticAberration")){c.params["mode"]=QStringLiteral("lens");c.params["intensity"]=4.0;c.params["edgeStart"]=.55;c.params["falloff"]=2.0;c.params["mix"]=1.0;c.params["slot"]=1;c.params["affectWorld"]=true;c.params["affectPictures"]=false;c.params["affectHud"]=false;c.params["duration"]=30;c.params["wait"]=false;}
        else if(tipo==QLatin1String("ludo.filter.noise")){c.params["intensity"]=.12;c.params["size"]=2.0;c.params["speed"]=1.0;c.params["style"]=QStringLiteral("film");c.params["temporal"]=QStringLiteral("smooth");c.params["seed"]=1337;c.params["contrast"]=1.0;c.params["colorAmount"]=0.0;c.params["slot"]=1;c.params["affectWorld"]=true;c.params["affectPictures"]=false;c.params["affectHud"]=false;c.params["duration"]=30;c.params["wait"]=false;}
        else if(tipo==QLatin1String("ludo.filter.scanlines")){c.params["intensity"]=.18;c.params["spacing"]=4.0;c.params["style"]=QStringLiteral("softCrt");c.params["thickness"]=.35;c.params["softness"]=.32;c.params["phase"]=0.0;c.params["scrollSpeed"]=0.0;c.params["interlaceAmount"]=.55;c.params["interlaceSpeed"]=60.0;c.params["whiteSweep"]=false;c.params["sweepSpeed"]=.35;c.params["sweepWidth"]=.055;c.params["sweepSoftness"]=1.0;c.params["sweepIntensity"]=.45;c.params["sweepRed"]=255;c.params["sweepGreen"]=255;c.params["sweepBlue"]=255;c.params["sweepDelay"]=0;c.params["slot"]=1;c.params["affectWorld"]=true;c.params["affectPictures"]=false;c.params["affectHud"]=false;c.params["duration"]=30;c.params["wait"]=false;}
        else if(tipo==QLatin1String("ludo.filter.vignette")){c.params["intensity"]=.45;c.params["radius"]=.68;c.params["softness"]=.28;c.params["slot"]=1;c.params["affectWorld"]=true;c.params["affectPictures"]=false;c.params["affectHud"]=false;c.params["duration"]=30;c.params["wait"]=false;}
        else if(tipo==QLatin1String("ludo.filter.blur")){c.params["radius"]=6.0;c.params["direction"]=QStringLiteral("full");c.params["style"]=QStringLiteral("gaussianSoft");c.params["quality"]=QStringLiteral("medium");c.params["strength"]=.45;c.params["edgePreservation"]=.15;c.params["angle"]=0.0;c.params["slot"]=1;c.params["affectWorld"]=true;c.params["affectPictures"]=false;c.params["affectHud"]=false;c.params["duration"]=30;c.params["wait"]=false;}
        else if(tipo==QLatin1String("ludo.filter.tiltShift")){c.params["blur"]=7.0;c.params["centerY"]=.5;c.params["focusWidth"]=.30;c.params["falloff"]=.24;c.params["style"]="gaussianSoft";c.params["quality"]="medium";c.params["strength"]=.55;c.params["edgePreservation"]=.15;c.params["angle"]=0.0;c.params["upperBlur"]=1.0;c.params["lowerBlur"]=1.0;c.params["slot"]=1;c.params["affectWorld"]=true;c.params["affectPictures"]=false;c.params["affectHud"]=false;c.params["duration"]=30;c.params["wait"]=false;}
        else if(tipo==QLatin1String("ludo.filter.clear")){c.params["filter"]=QStringLiteral("all");c.params["slot"]=1;c.params["allSlots"]=true;c.params["duration"]=30;c.params["wait"]=false;}
        ok=editLudoCommand(ed,c,this);
    } else if (tipo == QLatin1String("parallel.begin")) {
        ok = true;
    } else if (tipo == QLatin1String("picture.eraseAll")) {
        ok = true;
    } else if (tipo == QLatin1String("picture.setGroup") || tipo == QLatin1String("picture.moveGroup") ||
               tipo == QLatin1String("picture.eraseGroup") || tipo == QLatin1String("picture.attach") ||
               tipo == QLatin1String("picture.detach") || tipo.startsWith(QLatin1String("picture.timeline.")) ||
               tipo == QLatin1String("picture.onClick") || tipo == QLatin1String("picture.onTouch")) {
        ok = editPictureWorldCommand(ed,c,this);
    } else if (tipo.startsWith(QLatin1String("picture."))) {
        if (ed.pictures.isEmpty() && (tipo == QLatin1String("picture.show") || tipo == QLatin1String("picture.showByName"))) {
            // Sem imagem na biblioteca o comando não teria o que mostrar:
            // abre a biblioteca antes, em vez de deixar o autor no escuro.
            QMessageBox::information(this, tr("Mostrar imagem"),
                                     tr("A biblioteca de imagens está vazia. "
                                        "Importe ao menos uma imagem primeiro."));
            PictureLibraryDialog(ed, this).exec();
            if (ed.pictures.isEmpty()) return;
        }
        ok = PictureCommandDialog(ed, tipo, c, this).exec() == QDialog::Accepted;
    } else if ((tipo.startsWith(QLatin1String("database.")) && tipo != QLatin1String("database.each.end")) ||
               tipo.startsWith(QLatin1String("map.runtime."))) {
        ok = LogicCommandDialog(ed, tipo, c, this, commonContext()).exec() == QDialog::Accepted;
    } else if (tipo == QLatin1String("switch.set") || tipo == QLatin1String("selfSwitch.set") ||
               tipo == QLatin1String("variable.set") || tipo == QLatin1String("variable.math") ||
               tipo == QLatin1String("string.set") || tipo == QLatin1String("value.get") || tipo == QLatin1String("if") || tipo == QLatin1String("wait.until") ||
               tipo == QLatin1String("choice.show") || tipo == QLatin1String("input.number") || tipo == QLatin1String("input.text") ||
               tipo == QLatin1String("input.confirm") || tipo == QLatin1String("input.item") || tipo == QLatin1String("input.wait") ||
               tipo == QLatin1String("label") || tipo == QLatin1String("jump") ||
               tipo == QLatin1String("wait") || tipo == QLatin1String("common.call") || tipo == QLatin1String("common.reserve") ||
               tipo == QLatin1String("map.event.call") || tipo == QLatin1String("flow.exit") || tipo == QLatin1String("repeat.begin") ||
               tipo == QLatin1String("common.local.set") || tipo == QLatin1String("common.return") ||
               tipo == QLatin1String("dialogue.fastForward") || tipo == QLatin1String("dialogue.skipMode")) {
        ok = LogicCommandDialog(ed, tipo, c, this, commonContext()).exec() == QDialog::Accepted;
    }
    if (!ok) return;

    // Se o usuário selecionou um ramo virtual de Escolhas, o novo comando é
    // gravado diretamente naquele ramo. Assim não é necessário reabrir
    // "Exibir Escolhas" só para colocar conteúdo dentro dele.
    if (choiceCommand < 0 && m_lista && m_lista->currentItem()) {
        const QListWidgetItem* item = m_lista->currentItem();
        const int top = item->data(Qt::UserRole).toInt();
        const int branch = item->data(Qt::UserRole + 1).toInt();
        if (top >= 0 && top < m_cmds.size() && branch >= 0 &&
            m_cmds.at(top).type == QLatin1String("choice.show")) {
            choiceCommand = top;
            choiceBranch = branch;
        }
    }
    if (choiceCommand >= 0 && choiceCommand < m_cmds.size() && choiceBranch >= 0 &&
        m_cmds.at(choiceCommand).type == QLatin1String("choice.show")) {
        core::EventCommand& owner = m_cmds[choiceCommand];
        QVariantList branches = owner.params.value(QStringLiteral("branches")).toList();
        QStringList choices = owner.params.value(QStringLiteral("choices")).toStringList();
        if (choices.isEmpty()) for (const QVariant& value : owner.params.value(QStringLiteral("choices")).toList()) choices.push_back(value.toString());
        while (branches.size() < choices.size()) branches.push_back(QVariantList());
        if (choiceBranch >= branches.size()) return;

        QVariantList nested = branches.at(choiceBranch).toList();
        auto pushCommand = [&nested](const core::EventCommand& command) {
            nested.push_back(core::eventCommandToVariantMap(command));
        };
        if (c.type == QLatin1String("if")) {
            const bool withElse = c.params.take(QStringLiteral("createElse")).toBool();
            pushCommand(c);
            if (withElse) pushCommand(core::EventCommand{QStringLiteral("else"), {}});
            pushCommand(core::EventCommand{QStringLiteral("endIf"), {}});
        } else if (c.type == QLatin1String("loop.begin")) {
            pushCommand(c); pushCommand(core::EventCommand{QStringLiteral("loop.end"), {}});
        } else if (c.type == QLatin1String("repeat.begin")) {
            pushCommand(c); pushCommand(core::EventCommand{QStringLiteral("repeat.end"), {}});
        } else if (c.type == QLatin1String("database.each")) {
            pushCommand(c); pushCommand(core::EventCommand{QStringLiteral("database.each.end"), {}});
        } else if (c.type == QLatin1String("parallel.begin")) {
            pushCommand(c); pushCommand(core::EventCommand{QStringLiteral("parallel.end"), {}});
        } else pushCommand(c);
        branches[choiceBranch] = nested;
        owner.params[QStringLiteral("branches")] = branches;

        recarregar();
        selecionarIndice(choiceCommand);
        return;
    }

    const int i = selecionado(), at = i >= 0 ? i + 1 : m_cmds.size();
    if(c.type==QLatin1String("if")){const bool withElse=c.params.take("createElse").toBool();m_cmds.insert(at,c);int pos=at+1;if(withElse)m_cmds.insert(pos++,core::EventCommand{QStringLiteral("else"),{}});m_cmds.insert(pos,core::EventCommand{QStringLiteral("endIf"),{}});}else if(c.type==QLatin1String("loop.begin")){m_cmds.insert(at,c);m_cmds.insert(at+1,core::EventCommand{QStringLiteral("loop.end"),{}});}else if(c.type==QLatin1String("repeat.begin")){m_cmds.insert(at,c);m_cmds.insert(at+1,core::EventCommand{QStringLiteral("repeat.end"),{}});}else if(c.type==QLatin1String("database.each")){m_cmds.insert(at,c);m_cmds.insert(at+1,core::EventCommand{QStringLiteral("database.each.end"),{}});}else if(c.type==QLatin1String("parallel.begin")){m_cmds.insert(at,c);m_cmds.insert(at+1,core::EventCommand{QStringLiteral("parallel.end"),{}});}else m_cmds.insert(at,c);

    recarregar();selecionarIndice(at);
}

int CommandListWidget::fimDoBloco(int inicio) const
{
    if (inicio < 0 || inicio >= m_cmds.size()) return inicio;
    const QString type = m_cmds.at(inicio).type;
    if (type == QLatin1String("choice.show")) return inicio;
    if (type == QLatin1String("loop.begin")) {
        int depth = 0;
        for (int i = inicio; i < m_cmds.size(); ++i) {
            if (m_cmds.at(i).type == QLatin1String("loop.begin")) ++depth;
            else if (m_cmds.at(i).type == QLatin1String("loop.end") && --depth == 0) return i;
        }
    }
    if (type == QLatin1String("repeat.begin")) {
        int depth = 0;
        for (int i = inicio; i < m_cmds.size(); ++i) {
            if (m_cmds.at(i).type == QLatin1String("repeat.begin")) ++depth;
            else if (m_cmds.at(i).type == QLatin1String("repeat.end") && --depth == 0) return i;
        }
    }
    if (type == QLatin1String("parallel.begin")) {
        int depth = 0;
        for (int i = inicio; i < m_cmds.size(); ++i) {
            if (m_cmds.at(i).type == QLatin1String("parallel.begin")) ++depth;
            else if (m_cmds.at(i).type == QLatin1String("parallel.end") && --depth == 0) return i;
        }
    }
    if (type == QLatin1String("database.each")) {
        int depth = 0;
        for (int i = inicio; i < m_cmds.size(); ++i) {
            if (m_cmds.at(i).type == QLatin1String("database.each")) ++depth;
            else if (m_cmds.at(i).type == QLatin1String("database.each.end") && --depth == 0) return i;
        }
    }
    if (type == QLatin1String("if") || type == QLatin1String("else")) {
        int depth = type == QLatin1String("if") ? 0 : 1;
        for (int i = (type == QLatin1String("if") ? inicio : inicio + 1); i < m_cmds.size(); ++i) {
            const QString t = m_cmds.at(i).type;
            if (t == QLatin1String("if")) ++depth;
            else if (t == QLatin1String("endIf")) { if (--depth == 0) return i; }
        }
    }
    return inicio;
}

void CommandListWidget::selecionarIndice(int indice)
{
    if (!m_lista) return;
    for (int row = 0; row < m_lista->count(); ++row) {
        QListWidgetItem* item = m_lista->item(row);
        if (item && item->data(Qt::UserRole).toInt() == indice) {
            const QString kind=item->data(Qt::UserRole + 3).toString();
            if(kind!=QLatin1String("top") && kind!=QLatin1String("branchBoundary"))continue;
            m_lista->setCurrentRow(row);
            return;
        }
    }
}

void CommandListWidget::recarregar()
{
    int selectedCommand = -1, selectedBranch = -1;
    if (m_lista && m_lista->currentItem()) {
        selectedCommand = m_lista->currentItem()->data(Qt::UserRole).toInt();
        selectedBranch = m_lista->currentItem()->data(Qt::UserRole + 1).toInt();
    }
    m_lista->clear();
    const bool darkTheme=m_lista->palette().color(QPalette::Base).lightness()<128;
    const auto colorItem=[darkTheme](QListWidgetItem* item,const QString& type){
        if(item)item->setForeground(QBrush(eventCommandColor(type,darkTheme)));
    };

    // Decorações inline usam exatamente o mesmo validador do painel
    // Problemas. O índice do comando é extraído da localização estável emitida
    // pelo validador; não existe uma segunda lista de regras na interface.
    QHash<int, QVector<core::ValidationIssue>> issuesByCommand;
    if (!m_cmds.isEmpty()) {
        core::ProjectValidationResult inlineValidation;
        const core::CommonEvent* common = commonContext();
        core::validateEventCommands(ed, m_cmds, tr("Lista de comandos"), inlineValidation,
                                    common ? nullptr : ed.doc(), common);
        const QRegularExpression commandIndex(QStringLiteral("/ comando (\\d+)"));
        for (const core::ValidationIssue& issue : inlineValidation.issues) {
            const QRegularExpressionMatch match = commandIndex.match(issue.location);
            if (match.hasMatch()) issuesByCommand[match.captured(1).toInt() - 1].push_back(issue);
        }
    }

    const QVector<CommandStructureInfo> structure = analyzeCommandStructure(m_cmds);
    int nivel = 0;
    for (int i = 0; i < m_cmds.size(); ++i) {
        const core::EventCommand& c = m_cmds.at(i);
        const QString type = c.type;
        const CommandGroup group = CommandGroup::fromVariantMap(c.editorMetadata.value(QStringLiteral("group")).toMap());
        const QString previousGroup = i > 0 ? m_cmds.at(i - 1).editorMetadata.value(QStringLiteral("group")).toMap().value(QStringLiteral("id")).toString() : QString();
        if (!group.id.isEmpty() && group.id != previousGroup) {
            int groupEnd = i;
            while (groupEnd + 1 < m_cmds.size() && m_cmds.at(groupEnd + 1).editorMetadata.value(QStringLiteral("group")).toMap().value(QStringLiteral("id")).toString() == group.id) ++groupEnd;
            auto* header = new QListWidgetItem(QStringLiteral("%1  %2  (%3)").arg(group.collapsed ? QStringLiteral("▸") : QStringLiteral("▾"), group.name.isEmpty() ? tr("Grupo") : group.name).arg(groupEnd - i + 1), m_lista);
            header->setData(Qt::UserRole, i); header->setData(Qt::UserRole + 2, groupEnd);
            header->setData(Qt::UserRole + 3, QStringLiteral("groupHeader")); header->setData(Qt::UserRole + 6, group.color);
            header->setBackground(QBrush(QColor(group.color.red(), group.color.green(), group.color.blue(), darkTheme ? 55 : 35)));
            QFont font = header->font(); font.setBold(true); header->setFont(font); header->setForeground(group.color);
            if (group.collapsed) { i = groupEnd; continue; }
        }
        if (type == QLatin1String("endIf") || type == QLatin1String("else") || type == QLatin1String("loop.end") || type == QLatin1String("repeat.end") || type == QLatin1String("database.each.end") || type == QLatin1String("parallel.end"))
            nivel = qMax(0, nivel - 1);

        const bool structuralStart = type == QLatin1String("if") || type == QLatin1String("else") ||
                                     type == QLatin1String("loop.begin") || type == QLatin1String("repeat.begin") ||
                                     type == QLatin1String("database.each") || type == QLatin1String("parallel.begin") || type == QLatin1String("choice.show");
        const bool collapsed = structuralStart && c.editorMetadata.value(QStringLiteral("collapsed"), false).toBool() &&
                               i < structure.size() && structure.at(i).matchingIndex > i;
        auto* top = new QListWidgetItem(QString(nivel * 3, QLatin1Char(' ')) + descricao(ed, c), m_lista);
        top->setData(Qt::UserRole, i);
        top->setData(Qt::UserRole + 1, -1);
        top->setData(Qt::UserRole + 2, -1);
        const bool boundary=type==QLatin1String("else")||type==QLatin1String("endIf")||type==QLatin1String("loop.end")||type==QLatin1String("repeat.end")||type==QLatin1String("database.each.end")||type==QLatin1String("parallel.end");
        top->setData(Qt::UserRole + 3, boundary ? QStringLiteral("branchBoundary") : QStringLiteral("top"));
        top->setData(Qt::UserRole + 5, i < structure.size() ? structure.at(i).depth : nivel);
        if (group.color.isValid() && !group.id.isEmpty()) top->setData(Qt::UserRole + 6, group.color);
        colorItem(top,type);
        if(type==QLatin1String("if")&&!c.params.value(QStringLiteral("conditionTree")).toMap().isEmpty()){
            const bool any=core::normalizedConditionGroupMode(c.params.value(QStringLiteral("conditionTree")).toMap().value(QStringLiteral("mode")).toString())==QLatin1String("any");
            top->setText((any?QStringLiteral("[OR] "):QStringLiteral("[AND] "))+top->text());
            top->setForeground(QBrush(any?QColor(220,142,52):QColor(72,145,220)));
        }
        const QVector<core::ValidationIssue> commandIssues = issuesByCommand.value(i);
        if (!commandIssues.isEmpty()) {
            core::ValidationSeverity highest = core::ValidationSeverity::Info;
            QStringList tooltip;
            for (const core::ValidationIssue& issue : commandIssues) {
                if (issue.severity == core::ValidationSeverity::Error ||
                    (issue.severity == core::ValidationSeverity::Warning && highest == core::ValidationSeverity::Info))
                    highest = issue.severity;
                QString detail = QStringLiteral("[%1] %2").arg(issue.code, issue.message);
                if (!issue.suggestion.isEmpty()) detail += QStringLiteral("\n") + tr("Sugestão: %1").arg(issue.suggestion);
                tooltip.push_back(detail);
            }
            const QString marker = highest == core::ValidationSeverity::Error ? QStringLiteral("⛔ ")
                                 : highest == core::ValidationSeverity::Warning ? QStringLiteral("⚠ ")
                                 : QStringLiteral("ℹ ");
            top->setText(marker + top->text());
            top->setToolTip(tooltip.join(QStringLiteral("\n\n")));
            top->setData(Qt::UserRole + 4, int(highest));
            QFont decorated = top->font();
            decorated.setUnderline(true);
            top->setFont(decorated);
        }
        if (structuralStart) {
            QFont f = top->font(); f.setBold(true); top->setFont(f);
            top->setText((collapsed ? QStringLiteral("▸ ") : QStringLiteral("▾ ")) + top->text());
        }
        if (collapsed) {
            const int end = structure.at(i).matchingIndex;
            top->setText(top->text() + tr("  ·  comandos recolhidos: %1").arg(qMax(0, end - i - 1)));
            i = end;
            continue;
        }

        // Escolhas mantém os ramos no próprio comando. A lista simples exibe
        // esses comandos por recuo, sem criar uma segunda representação/modelo.
        if (type == QLatin1String("choice.show") && !collapsed) {
            QStringList choices = c.params.value(QStringLiteral("choices")).toStringList();
            if (choices.isEmpty()) for (const QVariant& value : c.params.value(QStringLiteral("choices")).toList()) choices.push_back(value.toString());
            const QVariantList branches = c.params.value(QStringLiteral("branches")).toList();
            for (int branch = 0; branch < choices.size(); ++branch) {
                auto* header = new QListWidgetItem(QString((nivel + 1) * 3, QLatin1Char(' ')) + tr("Quando %1").arg(choices.at(branch)), m_lista);
                header->setData(Qt::UserRole, i); header->setData(Qt::UserRole + 1, branch); header->setData(Qt::UserRole + 2, -1); header->setData(Qt::UserRole + 3, QStringLiteral("choiceBranch"));
                colorItem(header,QStringLiteral("choice.show"));
                QFont f = header->font(); f.setBold(true); header->setFont(f);
                const QVariantList nested = branch < branches.size() ? branches.at(branch).toList() : QVariantList();
                if (nested.isEmpty()) {
                    auto* empty = new QListWidgetItem(QString((nivel + 2) * 3, QLatin1Char(' ')) + tr("◆  (adicionar comando aqui)"), m_lista);
                    empty->setData(Qt::UserRole, i); empty->setData(Qt::UserRole + 1, branch); empty->setData(Qt::UserRole + 2, -1); empty->setData(Qt::UserRole + 3, QStringLiteral("choiceBranch"));
                    colorItem(empty,QStringLiteral("choice.show"));
                    QFont ef = empty->font(); ef.setItalic(true); empty->setFont(ef);
                } else {
                    int nestedLevel = 0;
                    const QVector<core::EventCommand> nestedCommands = core::eventCommandsFromVariantList(nested);
                    for (int n = 0; n < nestedCommands.size(); ++n) {
                        const core::EventCommand& nc = nestedCommands.at(n);
                        if (nc.type == QLatin1String("endIf") || nc.type == QLatin1String("else") || nc.type == QLatin1String("loop.end") || nc.type == QLatin1String("repeat.end") || nc.type == QLatin1String("database.each.end") || nc.type == QLatin1String("parallel.end")) nestedLevel = qMax(0, nestedLevel - 1);
                        auto* ni = new QListWidgetItem(QString((nivel + 2 + nestedLevel) * 3, QLatin1Char(' ')) + QStringLiteral("◆ ") + descricao(ed, nc), m_lista);
                        ni->setData(Qt::UserRole, i); ni->setData(Qt::UserRole + 1, branch); ni->setData(Qt::UserRole + 2, n); ni->setData(Qt::UserRole + 3, QStringLiteral("choiceNested"));
                        colorItem(ni,nc.type);
                        if (nc.type == QLatin1String("if") || nc.type == QLatin1String("else") || nc.type == QLatin1String("loop.begin") || nc.type == QLatin1String("repeat.begin") || nc.type == QLatin1String("database.each") || nc.type == QLatin1String("parallel.begin")) ++nestedLevel;
                    }
                }
            }
            auto* end = new QListWidgetItem(QString(nivel * 3, QLatin1Char(' ')) + tr("◆ Fim das Escolhas"), m_lista);
            end->setData(Qt::UserRole, i); end->setData(Qt::UserRole + 1, -1); end->setData(Qt::UserRole + 2, -1); end->setData(Qt::UserRole + 3, QStringLiteral("choiceEnd"));
            colorItem(end,QStringLiteral("choice.show"));
        }

        // A rota deixa de ser uma caixa-preta: seus passos aparecem na lista
        // principal, com a mesma ordem e os mesmos rótulos do editor de rota.
        if(type==QLatin1String("move.route")&&!collapsed){
            const core::MoveRoute route=core::moveRouteFromMap(c.params.value(QStringLiteral("route")).toMap());
            if(route.commands.isEmpty()){
                auto* empty=new QListWidgetItem(QString((nivel+1)*3,QLatin1Char(' '))+tr("◆  (rota vazia)"),m_lista);
                empty->setData(Qt::UserRole,i);empty->setData(Qt::UserRole+1,-1);empty->setData(Qt::UserRole+2,-1);empty->setData(Qt::UserRole+3,QStringLiteral("routeStep"));
                QFont f=empty->font();f.setItalic(true);empty->setFont(f);colorItem(empty,type);
            }else for(int step=0;step<route.commands.size();++step){
                auto* child=new QListWidgetItem(QString((nivel+1)*3,QLatin1Char(' '))+tr("◆ %1. %2").arg(step+1).arg(MoveRouteDialog::commandLabel(route.commands.at(step))),m_lista);
                child->setData(Qt::UserRole,i);child->setData(Qt::UserRole+1,-1);child->setData(Qt::UserRole+2,step);child->setData(Qt::UserRole+3,QStringLiteral("routeStep"));colorItem(child,type);
            }
        }

        if (type == QLatin1String("if") || type == QLatin1String("else") || type == QLatin1String("loop.begin") || type == QLatin1String("repeat.begin") || type == QLatin1String("database.each") || type == QLatin1String("parallel.begin")) ++nivel;
    }
    if (m_cmds.isEmpty()) {
        auto* empty = new QListWidgetItem(tr("(nenhum comando)"), m_lista);
        empty->setData(Qt::UserRole, -1); empty->setData(Qt::UserRole + 1, -1); empty->setData(Qt::UserRole + 3, QStringLiteral("empty"));
    }

    // Recupera a seleção pelo índice lógico, não pela linha visual: Choices
    // e rotas inserem linhas auxiliares sem alterar o vetor de EventCommand.
    int fallback = -1;
    for (int row = 0; row < m_lista->count(); ++row) {
        QListWidgetItem* item = m_lista->item(row);
        if (!item || item->data(Qt::UserRole).toInt() != selectedCommand) continue;
        if (fallback < 0) fallback = row;
        if (selectedBranch >= 0 && item->data(Qt::UserRole + 1).toInt() == selectedBranch) { fallback = row; break; }
        if (selectedBranch < 0 && item->data(Qt::UserRole + 3).toString() == QLatin1String("top")) { fallback = row; break; }
    }
    if (fallback >= 0) m_lista->setCurrentRow(fallback);
    atualizarPreviewLateral();
}

// ============================================================================
//  Condições da página
// ============================================================================
PageConditionsDialog::PageConditionsDialog(core::Editor& editorRef, QVariantMap& conditions,
                                           QWidget* parent)
    : QDialog(parent), ed(editorRef), m_cond(conditions)
{
    setWindowTitle(tr("Condições desta página"));
    resize(520, 320);
    const core::PageConditions c =
        core::PageConditions::fromJson(QJsonObject::fromVariantMap(conditions));

    auto* v = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("A página só vale quando <b>todas</b> as condições marcadas "
                               "batem. No jogo, vale a <b>última página válida</b> — por isso "
                               "as páginas de baixo são os estados mais avançados da história."),
                            this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    auto* form = new QFormLayout;
    auto* usaSwA = new QCheckBox(tr("Interruptor ligado:"), this);
    usaSwA->setChecked(c.useSwitchA);
    auto* swA = comboInterruptores(this, ed, c.switchAId);
    auto* usaSwB = new QCheckBox(tr("E também:"), this);
    usaSwB->setChecked(c.useSwitchB);
    auto* swB = comboInterruptores(this, ed, c.switchBId);
    auto* usaVar = new QCheckBox(tr("Variável:"), this);
    usaVar->setChecked(c.useVariable);
    auto* varId = comboVariaveis(this, ed, c.variableId);
    auto* op = new QComboBox(this);
    for (const QString& o : { QStringLiteral(">="), QStringLiteral("<="), QStringLiteral("=="),
                              QStringLiteral("!="), QStringLiteral(">"), QStringLiteral("<") })
        op->addItem(o, o);
    op->setCurrentIndex(qMax(0, op->findData(c.variableOp)));
    auto* valor = new QSpinBox(this);
    valor->setRange(-999999, 999999);
    valor->setValue(c.variableValue);
    auto* usaSelf = new QCheckBox(tr("Interruptor próprio ligado:"), this);
    usaSelf->setChecked(c.useSelfSwitch);
    auto* letra = new QComboBox(this);
    for (const QString& l : { QStringLiteral("A"), QStringLiteral("B"),
                              QStringLiteral("C"), QStringLiteral("D") })
        letra->addItem(l, l);
    letra->setCurrentIndex(qMax(0, letra->findData(c.selfSwitchLetter)));

    form->addRow(usaSwA, swA);
    form->addRow(usaSwB, swB);
    auto* linhaVar = new QHBoxLayout;
    linhaVar->addWidget(varId);
    linhaVar->addWidget(op);
    linhaVar->addWidget(valor);
    form->addRow(usaVar, linhaVar);
    form->addRow(usaSelf, letra);
    v->addLayout(form);
    v->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::accepted, this, [=, &conditions] {
        core::PageConditions n;
        n.useSwitchA = usaSwA->isChecked();
        n.switchAId = swA->currentData().toInt();
        n.useSwitchB = usaSwB->isChecked();
        n.switchBId = swB->currentData().toInt();
        n.useVariable = usaVar->isChecked();
        n.variableId = varId->currentData().toInt();
        n.variableOp = op->currentData().toString();
        n.variableValue = valor->value();
        n.useSelfSwitch = usaSelf->isChecked();
        n.selfSwitchLetter = letra->currentData().toString();
        conditions = n.toJson().toVariantMap();
    });
}

// ============================================================================
//  Eventos comuns
namespace {
QComboBox* commonTypeCombo(QWidget* parent, core::CommonValueType current)
{
    auto* box = new QComboBox(parent);
    for (core::CommonValueType type : { core::CommonValueType::Number,
                                        core::CommonValueType::Boolean,
                                        core::CommonValueType::Text })
        box->addItem(core::commonValueTypeLabel(type), core::commonValueTypeId(type));
    box->setCurrentIndex(qMax(0, box->findData(core::commonValueTypeId(current))));
    return box;
}

QVariant commonValueFromEditorText(const QString& text, core::CommonValueType type)
{
    return core::normalizeCommonValue(text, type);
}

bool editCommonEventSignature(core::Editor& ed, core::CommonEvent& common, QWidget* parent)
{
    QDialog d(parent);
    d.setWindowTitle(QObject::tr("Assinatura — %1").arg(common.name));
    d.resize(860, 560);
    auto* root = new QVBoxLayout(&d);
    auto* note = new QLabel(QObject::tr("Defina a interface No-Code deste Evento Comum. IDs internos permanecem estáveis ao renomear, para que chamadas existentes não quebrem."), &d);
    note->setWordWrap(true); root->addWidget(note);
    auto* tabs = new QTabWidget(&d); root->addWidget(tabs, 1);

    auto* paramPage = new QWidget(tabs); auto* paramLayout = new QVBoxLayout(paramPage);
    auto* params = new QTableWidget(paramPage); params->setColumnCount(5);
    params->setHorizontalHeaderLabels({QObject::tr("Nome"), QObject::tr("Tipo"), QObject::tr("Padrão"), QObject::tr("Obrigatório"), QObject::tr("Descrição")});
    params->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    params->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    params->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    params->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    params->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    paramLayout->addWidget(params, 1);
    auto* paramButtons = new QHBoxLayout; auto* addParam = new QPushButton(QObject::tr("Adicionar parâmetro"), paramPage); auto* delParam = new QPushButton(QObject::tr("Remover"), paramPage);
    paramButtons->addWidget(addParam); paramButtons->addWidget(delParam); paramButtons->addStretch(); paramLayout->addLayout(paramButtons);
    tabs->addTab(paramPage, QObject::tr("Parâmetros"));

    auto addParamRow = [&](const core::CommonEventParameter& def) {
        const int row = params->rowCount(); params->insertRow(row);
        auto* name = new QTableWidgetItem(def.name); name->setData(Qt::UserRole, def.id); params->setItem(row, 0, name);
        params->setCellWidget(row, 1, commonTypeCombo(params, def.type));
        params->setItem(row, 2, new QTableWidgetItem(def.defaultValue.toString()));
        auto* required = new QCheckBox(params); required->setChecked(def.required); required->setStyleSheet(QStringLiteral("margin-left:18px")); params->setCellWidget(row, 3, required);
        params->setItem(row, 4, new QTableWidgetItem(def.description));
    };
    for (const auto& def : common.parameters) addParamRow(def);
    QObject::connect(addParam, &QPushButton::clicked, &d, [&]{ core::CommonEventParameter def; def.name = QObject::tr("Parâmetro %1").arg(params->rowCount()+1); addParamRow(def); params->setCurrentCell(params->rowCount()-1,0); });
    QObject::connect(delParam, &QPushButton::clicked, &d, [&]{ if(params->currentRow()>=0) params->removeRow(params->currentRow()); });

    auto* localPage = new QWidget(tabs); auto* localLayout = new QVBoxLayout(localPage);
    auto* locals = new QTableWidget(localPage); locals->setColumnCount(3);
    locals->setHorizontalHeaderLabels({QObject::tr("Nome"), QObject::tr("Tipo"), QObject::tr("Valor inicial")});
    locals->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    locals->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    locals->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    localLayout->addWidget(locals, 1);
    auto* localButtons = new QHBoxLayout; auto* addLocal = new QPushButton(QObject::tr("Adicionar local"), localPage); auto* delLocal = new QPushButton(QObject::tr("Remover"), localPage);
    localButtons->addWidget(addLocal); localButtons->addWidget(delLocal); localButtons->addStretch(); localLayout->addLayout(localButtons);
    tabs->addTab(localPage, QObject::tr("Variáveis locais"));
    auto addLocalRow = [&](const core::CommonEventLocal& def) {
        const int row = locals->rowCount(); locals->insertRow(row);
        auto* name = new QTableWidgetItem(def.name); name->setData(Qt::UserRole, def.id); locals->setItem(row,0,name);
        locals->setCellWidget(row,1,commonTypeCombo(locals,def.type)); locals->setItem(row,2,new QTableWidgetItem(def.initialValue.toString()));
    };
    for (const auto& def : common.locals) addLocalRow(def);
    QObject::connect(addLocal,&QPushButton::clicked,&d,[&]{ core::CommonEventLocal def; def.name=QObject::tr("Local %1").arg(locals->rowCount()+1); addLocalRow(def); locals->setCurrentCell(locals->rowCount()-1,0); });
    QObject::connect(delLocal,&QPushButton::clicked,&d,[&]{ if(locals->currentRow()>=0) locals->removeRow(locals->currentRow()); });

    auto* returnPage = new QWidget(tabs); auto* returnForm = new QFormLayout(returnPage);
    auto* returnEnabled = new QCheckBox(QObject::tr("Este Evento Comum retorna um valor"), returnPage); returnEnabled->setChecked(common.returnValue.enabled);
    auto* returnName = new QLineEdit(common.returnValue.name, returnPage);
    auto* returnType = commonTypeCombo(returnPage, common.returnValue.type);
    auto* returnDefault = new QLineEdit(common.returnValue.defaultValue.toString(), returnPage);
    returnForm->addRow(returnEnabled); returnForm->addRow(QObject::tr("Nome da saída"),returnName); returnForm->addRow(QObject::tr("Tipo"),returnType); returnForm->addRow(QObject::tr("Valor padrão"),returnDefault);
    tabs->addTab(returnPage, QObject::tr("Retorno"));
    auto updateReturn = [&]{ const bool on=returnEnabled->isChecked(); returnName->setEnabled(on); returnType->setEnabled(on); returnDefault->setEnabled(on); }; updateReturn();
    QObject::connect(returnEnabled,&QCheckBox::toggled,&d,[&](bool){updateReturn();});

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d); root->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&d,[&]{
        QSet<QString> seenNames;
        QSet<QString> seenIds;
        auto validateSymbol = [&](QTableWidget* table, int row, const QString& kind)->bool {
            QTableWidgetItem* nameItem = table->item(row,0);
            const QString name = nameItem ? nameItem->text().trimmed() : QString();
            if (name.isEmpty()) {
                tabs->setCurrentWidget(table == params ? paramPage : localPage);
                table->setCurrentCell(row,0);
                QMessageBox::warning(&d, QObject::tr("Assinatura inválida"),
                                     QObject::tr("%1 precisa ter um nome.").arg(kind));
                return false;
            }
            const QString normalized = name.toCaseFolded();
            if (seenNames.contains(normalized)) {
                tabs->setCurrentWidget(table == params ? paramPage : localPage);
                table->setCurrentCell(row,0);
                QMessageBox::warning(&d, QObject::tr("Assinatura inválida"),
                                     QObject::tr("Parâmetros e variáveis locais precisam ter nomes únicos. “%1” está repetido.").arg(name));
                return false;
            }
            seenNames.insert(normalized);
            const QString stableId = nameItem ? nameItem->data(Qt::UserRole).toString() : QString();
            if (!stableId.isEmpty()) {
                if (seenIds.contains(stableId)) {
                    QMessageBox::warning(&d, QObject::tr("Assinatura inválida"),
                                         QObject::tr("A assinatura contém um ID interno duplicado. Remova e recrie o símbolo afetado."));
                    return false;
                }
                seenIds.insert(stableId);
            }
            return true;
        };
        for (int row=0; row<params->rowCount(); ++row) {
            if (!validateSymbol(params,row,QObject::tr("O parâmetro"))) return;
            auto* required=qobject_cast<QCheckBox*>(params->cellWidget(row,3));
            if (common.trigger != core::CommonTrigger::None && required && required->isChecked()) {
                tabs->setCurrentWidget(paramPage); params->setCurrentCell(row,0);
                QMessageBox::warning(&d, QObject::tr("Assinatura incompatível com o gatilho"),
                    QObject::tr("Eventos Comuns Automáticos/Paralelos não recebem argumentos do chamador. O parâmetro “%1” precisa ser opcional ou o gatilho deve ser “Só quando chamado”.")
                        .arg(params->item(row,0)->text().trimmed()));
                return;
            }
        }
        for (int row=0; row<locals->rowCount(); ++row)
            if (!validateSymbol(locals,row,QObject::tr("A variável local"))) return;
        if (returnEnabled->isChecked() && returnName->text().trimmed().isEmpty()) {
            tabs->setCurrentWidget(returnPage); returnName->setFocus();
            QMessageBox::warning(&d, QObject::tr("Assinatura inválida"),
                                 QObject::tr("Dê um nome ao valor de retorno ou desative o retorno."));
            return;
        }
        d.accept();
    });
    if (d.exec()!=QDialog::Accepted) return false;

    QVector<core::CommonEventParameter> newParams;
    for(int row=0;row<params->rowCount();++row){
        core::CommonEventParameter def; def.id=params->item(row,0)->data(Qt::UserRole).toString(); if(def.id.isEmpty())def.id=core::idGen();
        def.name=params->item(row,0)->text().trimmed().left(128); if(def.name.isEmpty())def.name=QObject::tr("Parâmetro %1").arg(row+1);
        auto* typeBox=qobject_cast<QComboBox*>(params->cellWidget(row,1)); def.type=core::commonValueTypeFromId(typeBox?typeBox->currentData().toString():QString());
        def.defaultValue=commonValueFromEditorText(params->item(row,2)?params->item(row,2)->text():QString(),def.type);
        auto* required=qobject_cast<QCheckBox*>(params->cellWidget(row,3)); def.required=required&&required->isChecked();
        def.description=params->item(row,4)?params->item(row,4)->text().left(1024):QString(); newParams.push_back(def);
    }
    QVector<core::CommonEventLocal> newLocals;
    for(int row=0;row<locals->rowCount();++row){ core::CommonEventLocal def; def.id=locals->item(row,0)->data(Qt::UserRole).toString(); if(def.id.isEmpty())def.id=core::idGen(); def.name=locals->item(row,0)->text().trimmed().left(128); if(def.name.isEmpty())def.name=QObject::tr("Local %1").arg(row+1); auto* typeBox=qobject_cast<QComboBox*>(locals->cellWidget(row,1)); def.type=core::commonValueTypeFromId(typeBox?typeBox->currentData().toString():QString()); def.initialValue=commonValueFromEditorText(locals->item(row,2)?locals->item(row,2)->text():QString(),def.type); newLocals.push_back(def); }
    common.parameters=newParams; common.locals=newLocals; common.returnValue.enabled=returnEnabled->isChecked(); common.returnValue.name=returnName->text().trimmed().left(128); common.returnValue.type=core::commonValueTypeFromId(returnType->currentData().toString()); common.returnValue.defaultValue=commonValueFromEditorText(returnDefault->text(),common.returnValue.type);
    ed.markDirty(); return true;
}
} // namespace

// ============================================================================
CommonEventsDialog::CommonEventsDialog(core::Editor& editorRef, QWidget* parent,
                                       const QString& initialCommonId)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Eventos Comuns — Lógica do jogo"));
    resize(980, 680);
    auto* v = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Os <b>Eventos Comuns</b> são o centro da lógica No-Code da LUDO: "
                               "reutilize sistemas, menus por Pictures, cutscenes e regras globais em qualquer mapa."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    auto* filtros = new QHBoxLayout;
    m_busca = new QLineEdit(this);
    m_busca->setPlaceholderText(tr("Pesquisar por número, nome, categoria ou anotação…"));
    m_busca->setClearButtonEnabled(true);
    m_categoriaFiltro = new QComboBox(this);
    m_categoriaFiltro->setMinimumWidth(190);
    filtros->addWidget(new QLabel(tr("Pesquisar:"), this));
    filtros->addWidget(m_busca, 1);
    filtros->addWidget(new QLabel(tr("Categoria:"), this));
    filtros->addWidget(m_categoriaFiltro);
    v->addLayout(filtros);

    auto* linha = new QHBoxLayout;
    m_lista = new QListWidget(this);
    m_lista->setMinimumWidth(280);
    m_lista->setMaximumWidth(340);
    m_lista->setAlternatingRowColors(true);
    linha->addWidget(m_lista);

    auto* direita = new QVBoxLayout;
    auto* form = new QFormLayout;
    auto* nome = new QLineEdit(this);
    auto* categoria = new QLineEdit(this);
    categoria->setPlaceholderText(tr("Ex.: Sistema, Menu, Batalha, Missões"));
    auto* descricao = new QPlainTextEdit(this);
    descricao->setPlaceholderText(tr("Anotações sobre a finalidade, Switches e Variáveis usados por este evento."));
    descricao->setMaximumHeight(72);
    auto* gatilho = new QComboBox(this);
    for (core::CommonTrigger t : { core::CommonTrigger::None, core::CommonTrigger::Autorun,
                                   core::CommonTrigger::Parallel })
        gatilho->addItem(core::commonTriggerLabel(t), core::commonTriggerId(t));
    auto* swGatilho = comboInterruptores(this, ed, 1);
    form->addRow(tr("Nome"), nome);
    form->addRow(tr("Categoria"), categoria);
    form->addRow(tr("Anotações"), descricao);
    form->addRow(tr("Quando roda"), gatilho);
    form->addRow(tr("Interruptor do gatilho"), swGatilho);
    direita->addLayout(form);

    auto* executionHint = new QLabel(tr("Nenhum: use “Chamar evento comum”. Automático e Paralelo rodam enquanto o Switch escolhido estiver ligado."), this);
    executionHint->setWordWrap(true);
    executionHint->setStyleSheet(QStringLiteral("color:#777;font-size:11px"));
    direita->addWidget(executionHint);

    auto* areaCmds = new QGroupBox(tr("Comandos"), this);
    auto* areaLay = new QVBoxLayout(areaCmds);
    direita->addWidget(areaCmds, 1);
    linha->addLayout(direita, 1);
    v->addLayout(linha, 1);

    auto* botoes = new QHBoxLayout;
    auto* novo = new QPushButton(tr("Novo evento comum"), this);
    auto* assinatura = new QPushButton(tr("Assinatura…"), this);
    auto* agendamento = new QPushButton(tr("Gatilho e agendamento…"), this);
    auto* duplicar = new QPushButton(tr("Duplicar"), this);
    auto* importar = new QPushButton(tr("Importar .ludocommon…"), this);
    auto* exportar = new QPushButton(tr("Exportar .ludocommon…"), this);
    auto* apagar = new QPushButton(tr("Excluir"), this);
    botoes->addWidget(novo);
    botoes->addWidget(assinatura);
    botoes->addWidget(agendamento);
    botoes->addWidget(duplicar);
    botoes->addWidget(importar);
    botoes->addWidget(exportar);
    botoes->addWidget(apagar);
    botoes->addStretch(1);
    v->addLayout(botoes);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);

    const auto selectedIndex = [this]() {
        const QListWidgetItem* item = m_lista->currentItem();
        if (!item) return -1;
        const QString id = item->data(Qt::UserRole).toString();
        for (int i = 0; i < ed.commonEvents.size(); ++i)
            if (ed.commonEvents.at(i).id == id) return i;
        return -1;
    };

    auto mostrar = [=](int) {
        QLayoutItem* velho = nullptr;
        while ((velho = areaLay->takeAt(0)) != nullptr) {
            delete velho->widget();
            delete velho;
        }
        const int index = selectedIndex();
        const bool valido = index >= 0 && index < ed.commonEvents.size();
        nome->setEnabled(valido);
        categoria->setEnabled(valido);
        descricao->setEnabled(valido);
        gatilho->setEnabled(valido);
        assinatura->setEnabled(valido);
        agendamento->setEnabled(valido);
        duplicar->setEnabled(valido);
        apagar->setEnabled(valido);
        if (!valido) { swGatilho->setEnabled(false); return; }
        core::CommonEvent& ce = ed.commonEvents[index];
        QSignalBlocker b1(nome), b2(categoria), b3(descricao), b4(gatilho), b5(swGatilho);
        nome->setText(ce.name);
        categoria->setText(ce.category);
        descricao->setPlainText(ce.description);
        gatilho->setCurrentIndex(qMax(0, gatilho->findData(core::commonTriggerId(ce.trigger))));
        swGatilho->setCurrentIndex(qMax(0, swGatilho->findData(ce.switchId)));
        swGatilho->setEnabled(ce.trigger != core::CommonTrigger::None && !ce.advancedTrigger);
        areaLay->addWidget(new CommandListWidget(ed, ce.commands, areaCmds));
    };
    connect(m_lista, &QListWidget::currentRowChanged, this, mostrar);
    connect(nome, &QLineEdit::textEdited, this, [this, nome, selectedIndex] {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        ed.commonEvents[i].name = nome->text().left(128);
        ed.markDirty();
        if (QListWidgetItem* item = m_lista->currentItem()) {
            const QString prefix = ed.commonEvents[i].category.trimmed().isEmpty()
                ? QString() : QStringLiteral("[%1] ").arg(ed.commonEvents[i].category.trimmed());
            item->setText(QStringLiteral("%1 — %2%3").arg(ed.commonEvents[i].number).arg(prefix, ed.commonEvents[i].name));
        }
    });
    connect(categoria, &QLineEdit::editingFinished, this, [this, categoria, selectedIndex] {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        ed.commonEvents[i].category = categoria->text().trimmed().left(128);
        ed.markDirty();
        recarregar();
    });
    connect(descricao, &QPlainTextEdit::textChanged, this, [this, descricao, selectedIndex] {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        ed.commonEvents[i].description = descricao->toPlainText().left(4096);
        ed.markDirty();
    });
    connect(gatilho, &QComboBox::currentIndexChanged, this, [this, gatilho, swGatilho, selectedIndex](int) {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        ed.commonEvents[i].trigger = core::commonTriggerFromId(gatilho->currentData().toString());
        swGatilho->setEnabled(ed.commonEvents[i].trigger != core::CommonTrigger::None && !ed.commonEvents[i].advancedTrigger);
        ed.markDirty();
    });
    connect(swGatilho, &QComboBox::currentIndexChanged, this, [this, swGatilho, selectedIndex](int) {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        ed.commonEvents[i].switchId = swGatilho->currentData().toInt();
        ed.markDirty();
    });
    connect(assinatura, &QPushButton::clicked, this, [this, selectedIndex] {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        editCommonEventSignature(ed, ed.commonEvents[i], this);
    });
    connect(agendamento, &QPushButton::clicked, this, [this, selectedIndex, swGatilho] {
        const int i=selectedIndex();if(i<0||i>=ed.commonEvents.size())return;
        core::CommonEvent& ce=ed.commonEvents[i];
        QDialog d(this);d.setWindowTitle(tr("Gatilho e agendamento — %1").arg(ce.name));d.resize(720,460);
        auto* root=new QVBoxLayout(&d);auto* form=new QFormLayout;root->addLayout(form);
        auto* advanced=new QCheckBox(tr("Usar condição avançada (Value Resolver)"),&d);advanced->setChecked(ce.advancedTrigger);form->addRow(QString(),advanced);
        auto* type=new QComboBox(&d);for(core::CommonValueType t:{core::CommonValueType::Number,core::CommonValueType::Boolean,core::CommonValueType::Text})type->addItem(core::commonValueTypeLabel(t),core::commonValueTypeId(t));type->setCurrentIndex(qMax(0,type->findData(core::commonValueTypeId(ce.triggerValueType))));
        auto* leftHost=new QWidget(&d);auto* leftLay=new QVBoxLayout(leftHost);leftLay->setContentsMargins(0,0,0,0);
        auto* rightHost=new QWidget(&d);auto* rightLay=new QVBoxLayout(rightHost);rightLay->setContentsMargins(0,0,0,0);
        auto* op=new QComboBox(&d);auto* policy=new QComboBox(&d);for(core::CommonSchedulePolicy p:{core::CommonSchedulePolicy::WhileTrue,core::CommonSchedulePolicy::OnTrue,core::CommonSchedulePolicy::Interval})policy->addItem(core::commonSchedulePolicyLabel(p),core::commonSchedulePolicyId(p));policy->setCurrentIndex(qMax(0,policy->findData(core::commonSchedulePolicyId(ce.schedulePolicy))));
        auto* interval=new QSpinBox(&d);interval->setRange(1,360000);interval->setValue(qBound(1,ce.intervalFrames,360000));interval->setSuffix(tr(" quadros"));
        auto* priority=new QSpinBox(&d);priority->setRange(-1000,1000);priority->setValue(ce.priority);priority->setToolTip(tr("Maior prioridade vence quando mais de um Evento Comum automático está pronto."));
        form->addRow(tr("Tipo da condição"),type);form->addRow(tr("Valor esquerdo"),leftHost);form->addRow(tr("Operador"),op);form->addRow(tr("Valor direito"),rightHost);form->addRow(tr("Política"),policy);form->addRow(tr("Intervalo"),interval);form->addRow(tr("Prioridade"),priority);
        auto left=std::make_shared<std::shared_ptr<CommonSourceEditorState>>();auto right=std::make_shared<std::shared_ptr<CommonSourceEditorState>>();
        const QVariantMap savedLeft=ce.triggerLeft,savedRight=ce.triggerRight;const QString savedOp=ce.triggerOp;
        auto rebuild=[this,type,op,left,right,leftHost,rightHost,leftLay,rightLay,savedLeft,savedRight,savedOp]{
            const core::CommonValueType vt=core::commonValueTypeFromId(type->currentData().toString());
            QVariantMap l=*left?(*left)->value():savedLeft;QVariantMap r=*right?(*right)->value():savedRight;
            if(*left){leftLay->removeWidget((*left)->widget);(*left)->widget->deleteLater();}if(*right){rightLay->removeWidget((*right)->widget);(*right)->widget->deleteLater();}
            *left=makeCommonSourceEditor(leftHost,ed,vt,l,nullptr);*right=makeCommonSourceEditor(rightHost,ed,vt,r,nullptr);leftLay->addWidget((*left)->widget);rightLay->addWidget((*right)->widget);
            const QString keep=op->count()?op->currentData().toString():savedOp;QSignalBlocker b(op);op->clear();op->addItem(tr("Igual"),QStringLiteral("=="));op->addItem(tr("Diferente"),QStringLiteral("!="));
            if(vt==core::CommonValueType::Number){op->addItem(tr("Maior ou igual"),QStringLiteral(">="));op->addItem(tr("Menor ou igual"),QStringLiteral("<="));op->addItem(tr("Maior"),QStringLiteral(">"));op->addItem(tr("Menor"),QStringLiteral("<"));}
            if(vt==core::CommonValueType::Text){op->addItem(tr("Contém"),QStringLiteral("contains"));op->addItem(tr("Começa com"),QStringLiteral("startsWith"));op->addItem(tr("Termina com"),QStringLiteral("endsWith"));}
            int oi=op->findData(keep);if(oi<0)oi=0;op->setCurrentIndex(oi);
        };
        rebuild();connect(type,&QComboBox::currentIndexChanged,&d,[rebuild](int){rebuild();});
        auto sync=[=]{const bool enabled=advanced->isChecked();type->setEnabled(enabled);leftHost->setEnabled(enabled);rightHost->setEnabled(enabled);op->setEnabled(enabled);interval->setEnabled(policy->currentData().toString()==QLatin1String("interval"));};sync();connect(advanced,&QCheckBox::toggled,&d,[sync](bool){sync();});connect(policy,&QComboBox::currentIndexChanged,&d,[sync](int){sync();});
        auto* help=new QLabel(tr("Enquanto verdadeiro preserva o comportamento clássico. Ao ficar verdadeiro dispara uma vez por borda. Intervalo agenda execuções sem criar chamadas duplicadas; se o evento ainda estiver rodando, fica apenas uma execução pendente."),&d);help->setWordWrap(true);help->setStyleSheet(QStringLiteral("color:#777;font-size:11px"));root->addWidget(help);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;ce.advancedTrigger=advanced->isChecked();ce.triggerValueType=core::commonValueTypeFromId(type->currentData().toString());if(*left)ce.triggerLeft=(*left)->value();if(*right)ce.triggerRight=(*right)->value();ce.triggerOp=op->currentData().toString();ce.schedulePolicy=core::commonSchedulePolicyFromId(policy->currentData().toString());ce.intervalFrames=interval->value();ce.priority=priority->value();ed.markDirty();swGatilho->setEnabled(ce.trigger!=core::CommonTrigger::None&&!ce.advancedTrigger);
    });
    connect(novo, &QPushButton::clicked, this, [this] {
        int maior = 0;
        for (const core::CommonEvent& c : ed.commonEvents) maior = qMax(maior, c.number);
        core::CommonEvent ce;
        ce.number = maior + 1;
        ce.name = tr("Evento comum %1").arg(ce.number);
        ce.category = tr("Geral");
        ed.commonEvents.push_back(ce);
        ed.markDirty();
        { QSignalBlocker b1(m_busca), b2(m_categoriaFiltro); m_busca->clear(); m_categoriaFiltro->setCurrentIndex(0); }
        recarregar();
        for (int row = 0; row < m_lista->count(); ++row)
            if (m_lista->item(row)->data(Qt::UserRole).toString() == ce.id) { m_lista->setCurrentRow(row); break; }
    });
    connect(duplicar, &QPushButton::clicked, this, [this, selectedIndex] {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        int maior = 0;
        for (const core::CommonEvent& c : ed.commonEvents) maior = qMax(maior, c.number);
        core::CommonEvent copy = ed.commonEvents.at(i);
        copy.id = core::idGen();
        copy.number = maior + 1;
        copy.name = tr("%1 (cópia)").arg(copy.name).left(128);
        ed.commonEvents.push_back(copy);
        ed.markDirty();
        recarregar();
        for (int row = 0; row < m_lista->count(); ++row)
            if (m_lista->item(row)->data(Qt::UserRole).toString() == copy.id) { m_lista->setCurrentRow(row); break; }
    });
    connect(exportar, &QPushButton::clicked, this, [this, selectedIndex] {
        const int i=selectedIndex(); if(i<0||i>=ed.commonEvents.size()) return;
        const core::CommonEvent& ce=ed.commonEvents.at(i);
        QString suggested=ce.name.trimmed(); if(suggested.isEmpty()) suggested=QStringLiteral("evento-comum");
        suggested.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),QStringLiteral("-"));
        const QString path=QFileDialog::getSaveFileName(this,tr("Exportar Evento Comum portátil"),suggested+QStringLiteral(".ludocommon"),tr("Evento Comum LUDO (*.ludocommon)"));
        if(path.isEmpty()) return; QString err; core::CommonEventPackageSummary info;
        if(!core::exportCommonEventPackage(ed,ce.id,path,&err,&info)){QMessageBox::critical(this,tr("Exportar .ludocommon"),err);return;}
        QString msg=tr("Pacote criado. Conteúdo: Eventos Comuns: %1 · Switches: %2 · Variáveis: %3 · Textos: %4 · Bancos: %5 · Extensões: %6 · Mapas externos: %7 · Registros RPG: %8 · Tilesets: %9 · Superfícies de passos: %10 · Arquivos incorporados: %11.")
            .arg(info.commonEvents).arg(info.switches).arg(info.variables).arg(info.strings).arg(info.customDatabases).arg(info.plugins).arg(info.maps).arg(info.databaseRecords).arg(info.tilesets).arg(info.footstepSurfaces).arg(info.assets);
        if(!info.warnings.isEmpty()) msg+=QStringLiteral("\n\n")+tr("Avisos:\n• ")+info.warnings.join(QStringLiteral("\n• "));
        QMessageBox::information(this,tr("Exportar .ludocommon"),msg);
    });
    connect(importar, &QPushButton::clicked, this, [this] {
        const QString path=QFileDialog::getOpenFileName(this,tr("Importar Evento Comum portátil"),QString(),tr("Evento Comum LUDO (*.ludocommon)"));
        if(path.isEmpty()) return; QString err; core::CommonEventPackageImportResult info;
        if(!core::importCommonEventPackage(ed,path,&err,&info)){QMessageBox::critical(this,tr("Importar .ludocommon"),err);return;}
        {QSignalBlocker b1(m_busca),b2(m_categoriaFiltro);m_busca->clear();m_categoriaFiltro->setCurrentIndex(0);}
        recarregar(); for(int row=0;row<m_lista->count();++row)if(m_lista->item(row)->data(Qt::UserRole).toString()==info.rootCommonEventId){m_lista->setCurrentRow(row);break;}
        QString msg=tr("“%1” foi importado. Conteúdo novo: Eventos Comuns: %2 · Bancos: %3 · Extensões: %4 · Superfícies de passos: %5 · Arquivos: %6. Dependências verificadas: Mapas: %7 · Registros RPG: %8 · Tilesets: %9.")
            .arg(info.rootCommonEventName).arg(info.commonEvents).arg(info.customDatabases).arg(info.plugins).arg(info.footstepSurfaces).arg(info.assets).arg(info.maps).arg(info.databaseRecords).arg(info.tilesets);
        if(!info.warnings.isEmpty()) msg+=QStringLiteral("\n\n")+tr("Avisos:\n• ")+info.warnings.join(QStringLiteral("\n• "));
        QMessageBox::information(this,tr("Importar .ludocommon"),msg);
    });

    connect(apagar, &QPushButton::clicked, this, [this, mostrar, selectedIndex] {
        const int i = selectedIndex();
        if (i < 0 || i >= ed.commonEvents.size()) return;
        const core::CommonEvent& common=ed.commonEvents.at(i);
        const QString name = common.name;
        const int useCount=core::findProjectUses(ed,core::ReferenceSymbolKind::CommonEvent,common.id).size();
        QString question=tr("Excluir “%1”?").arg(name);
        if(useCount>0) question+=tr("\n\nEste Evento Comum ainda é usado no projeto. Usos encontrados: %1. Se você excluir agora, esses locais deixarão de funcionar. A Busca Global pode mostrar onde ele é usado.").arg(useCount);
        if (QMessageBox::question(this, tr("Excluir evento comum"), question,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        ed.commonEvents.remove(i);
        ed.markDirty();
        recarregar();
        mostrar(m_lista->currentRow());
    });
    connect(m_busca, &QLineEdit::textChanged, this, [this] { recarregar(); });
    connect(m_categoriaFiltro, &QComboBox::currentIndexChanged, this, [this](int) { recarregar(); });
    recarregar();
    int initialRow=-1;
    if(!initialCommonId.isEmpty()) for(int row=0;row<m_lista->count();++row)
        if(m_lista->item(row)->data(Qt::UserRole).toString()==initialCommonId){initialRow=row;break;}
    if(initialRow>=0) m_lista->setCurrentRow(initialRow);
    else if (m_lista->count() > 0) m_lista->setCurrentRow(0);
    else mostrar(-1);
}

void CommonEventsDialog::recarregar()
{
    const QString selectedId = m_lista->currentItem() ? m_lista->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString selectedCategory = m_categoriaFiltro ? m_categoriaFiltro->currentData().toString() : QString();
    if (m_categoriaFiltro) {
        QSignalBlocker blocker(m_categoriaFiltro);
        QSet<QString> categories;
        for (const core::CommonEvent& c : ed.commonEvents)
            if (!c.category.trimmed().isEmpty()) categories.insert(c.category.trimmed());
        QStringList ordered = categories.values();
        ordered.sort(Qt::CaseInsensitive);
        m_categoriaFiltro->clear();
        m_categoriaFiltro->addItem(tr("Todas as categorias"), QString());
        for (const QString& category : ordered) m_categoriaFiltro->addItem(category, category);
        m_categoriaFiltro->setCurrentIndex(qMax(0, m_categoriaFiltro->findData(selectedCategory)));
    }

    const QString query = m_busca ? m_busca->text().trimmed() : QString();
    const QString categoryFilter = m_categoriaFiltro ? m_categoriaFiltro->currentData().toString() : QString();
    m_lista->clear();
    int selectedRow = -1;
    for (const core::CommonEvent& c : ed.commonEvents) {
        if (!categoryFilter.isEmpty() && c.category.compare(categoryFilter, Qt::CaseInsensitive) != 0) continue;
        const QString haystack = QStringLiteral("%1 %2 %3 %4").arg(c.number).arg(c.name, c.category, c.description);
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        const QString prefix = c.category.trimmed().isEmpty() ? QString() : QStringLiteral("[%1] ").arg(c.category.trimmed());
        auto* item = new QListWidgetItem(QStringLiteral("%1 — %2%3").arg(c.number).arg(prefix, c.name), m_lista);
        item->setData(Qt::UserRole, c.id);
        item->setToolTip(c.description);
        if (c.id == selectedId) selectedRow = m_lista->count() - 1;
    }
    if (selectedRow < 0 && m_lista->count() > 0) selectedRow = 0;
    m_lista->setCurrentRow(selectedRow);
}

} // namespace ui

namespace ui {

// ============================================================================
//  MessagePreview / MessageCommandDialog — comando "mostrar mensagem"
// ============================================================================
namespace {

/// Fonte e medidas iguais às da janela do jogo, para o preview não mentir.
QFont mensagemFonte(const QWidget* w)
{
    QFont f = w ? w->font() : QFont();
    f.setPixelSize(16);
    return f;
}
constexpr int kMargem = 12, kPadding = 12, kLinhas = 4;

} // namespace

MessagePreview::MessagePreview(core::Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    setFixedSize(ed.gameResolution);
    m_clock = new QElapsedTimer;
    m_clock->start();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    timer->start(33);
}

QSize MessagePreview::sizeHint() const { return ed.gameResolution; }

void MessagePreview::setText(const QString& raw) { m_raw = raw; update(); }
void MessagePreview::setPositionId(const QString& id) { m_pos = id; update(); }
void MessagePreview::setSpeaker(const QString& speaker) { m_speaker = speaker; update(); }
void MessagePreview::setPage(int i) { m_page = qMax(0, i); update(); }
void MessagePreview::setOffset(int x, int y)
{
    m_offsetX = x;
    m_offsetY = y;
    update();
}

void MessagePreview::setBaseFontSize(int px)
{
    m_baseFontSize = px > 0 ? qBound(6, px, 96) : 0;
    update();
}

void MessagePreview::setTextEffects(const core::TextEffectStack& effects,
                                    const core::TextGradientSpec& gradient)
{
    m_effects = effects;
    m_gradient = gradient;
    if (m_clock) m_clock->restart();
    update();
}

void MessagePreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#14161c"));
    // "tela do jogo" de mentira, só para dar noção de onde a caixa fica
    p.setPen(QPen(QColor(70, 75, 90), 1, Qt::DashLine));
    p.drawRect(rect().adjusted(1, 1, -2, -2));

    QFont f = mensagemFonte(this);
    f.setPixelSize(qMax(6, m_baseFontSize > 0 ? m_baseFontSize : ed.gameUi.fontSize));
    p.setFont(f);
    const QFontMetricsF fm(f);
    const int uiPaddingX = qMax(0, ed.gameUi.paddingX);
    const int uiPaddingY = qMax(0, ed.gameUi.paddingY);
    const double innerW = qMax(40, width() - 2 * kMargem - 2 * uiPaddingX);
    // A prévia usa a MESMA folha de ícones do projeto: sem isto, \I[3]
    // aparecia como um quadradinho vazio aqui e como ícone no jogo.
    const core::IconSet* icones = &ed.iconSet;
    const QVector<game::TextPage> pages = game::layoutMessage(m_raw, f, innerW, kLinhas, {}, icones);
    m_pageCount = qMax(1, pages.size());
    const int idx = qBound(0, m_page, m_pageCount - 1);

    const int alturaCaixa = int(fm.height() * kLinhas) + uiPaddingY * 2;
    int y = height() - alturaCaixa - kMargem;
    if (m_pos == QLatin1String("top"))    y = kMargem;
    if (m_pos == QLatin1String("middle")) y = (height() - alturaCaixa) / 2;
    const QRect caixa(kMargem + m_offsetX, y + m_offsetY,
                      width() - kMargem * 2, alturaCaixa);

    const game::ui::UiTheme theme = game::ui::UiTheme::fromSettings(ed.gameUi);
    game::ui::UiDrawList uiList;
    if (theme.hasWindowSkin())
        uiList.addNineSlice(caixa, theme.windowSkin, theme.windowSkinSlices, theme.windowOpacity, false);
    else
        uiList.addPanel(caixa, theme.window, theme.windowOpacity);
    if (!m_speaker.trimmed().isEmpty()) {
        const int nameH = qMax(30, int(fm.height()) + uiPaddingY);
        const int nameW = qMin(caixa.width(), qMax(120, int(fm.horizontalAdvance(m_speaker)) + uiPaddingX * 2));
        QRect nameBox(caixa.left() + 10, caixa.top() - nameH + 5, nameW, nameH);
        if (nameBox.top() < 4) nameBox.moveTop(caixa.bottom() + 4);
        if (theme.hasWindowSkin()) uiList.addNineSlice(nameBox, theme.windowSkin, theme.windowSkinSlices, theme.windowOpacity, false);
        else uiList.addPanel(nameBox, theme.choiceWindow, theme.windowOpacity);
        uiList.addText(QRectF(nameBox).adjusted(uiPaddingX,0,-uiPaddingX,0), m_speaker, f, theme.selectedText, Qt::AlignVCenter|Qt::AlignLeft);
    }
    game::ui::UiPainterRenderer::render(p, uiList);

    if (pages.isEmpty()) {
        p.setPen(QColor("#8a8a8a"));
        p.drawText(caixa.adjusted(uiPaddingX, uiPaddingY, -uiPaddingX, -uiPaddingY),
                   Qt::AlignLeft | Qt::AlignTop, tr("(mensagem vazia)"));
        return;
    }
    {
        // Mesmo desenho do jogo (ícones, \FS, \FC, contorno e animações).
        game::TextDrawOpts o;
        o.color = theme.text;
        o.icons = icones;
        o.time = m_clock ? m_clock->elapsed() / 1000.0 : 0.0;
        o.effects = m_effects;
        o.gradient = m_gradient;
        game::drawTextPage(p, pages[idx], f,
                           QRectF(caixa.left() + uiPaddingX, caixa.top() + uiPaddingY,
                                  caixa.width() - uiPaddingX * 2, caixa.height() - uiPaddingY * 2),
                           o);
    }
    // seta de "continuar" (a mesma do jogo)
    p.setPen(Qt::NoPen);
    p.setBrush(theme.accent);
    const QPointF c(caixa.right() - 18, caixa.bottom() - 12);
    const QPointF tri[3] = { c + QPointF(-6, -4), c + QPointF(6, -4), c + QPointF(0, 4) };
    p.drawPolygon(tri, 3);
}

MessageCommandDialog::MessageCommandDialog(core::Editor& editorRef,
                                           core::EventCommand& cmd, QWidget* parent)
    : QDialog(parent), ed(editorRef), m_cmd(cmd)
{
    const core::DialogueContent existingDialogue=core::DialogueContent::fromVariantMap(cmd.params.value(QStringLiteral("dialogueContent")).toMap());
    setWindowTitle(tr("Mostrar mensagem"));
    // RC2.64: o preview vive em janela sob demanda; mantenha o formulário
    // compacto mesmo quando a resolução lógica do jogo for grande.
    resize(780, qMin(840, qMax(640, ed.gameResolution.height() + 70)));
    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);
    auto* leftScroll = new QScrollArea(this);
    leftScroll->setWidgetResizable(true);
    leftScroll->setMinimumWidth(540);
    leftScroll->setFrameShape(QFrame::NoFrame);
    auto* left = new QWidget(leftScroll);
    left->setMinimumWidth(500);
    auto* v = new QVBoxLayout(left);
    leftScroll->setWidget(left);
    body->addWidget(leftScroll);

    auto* hint = new QLabel(tr("O texto é quebrado em linhas e páginas automaticamente. "
                               "Use <b>Ver prévia</b> para abrir uma prévia na resolução exata do jogo."), left);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    auto* profileRow=new QHBoxLayout;profileRow->addWidget(new QLabel(tr("Personagem:"),left));m_speakerProfile=new QComboBox(left);m_speakerProfile->addItem(tr("Sem personagem definido"),QString());for(const auto& profile:ed.speakerDatabase.speakers)m_speakerProfile->addItem(profile.name,profile.id);m_speakerProfile->setCurrentIndex(qMax(0,m_speakerProfile->findData(existingDialogue.speakerId)));profileRow->addWidget(m_speakerProfile,1);auto* editProfile=new QPushButton(tr("Novo / editar…"),left);profileRow->addWidget(editProfile);v->addLayout(profileRow);
    connect(editProfile,&QPushButton::clicked,this,[this]{QDialog d(this);d.setWindowTitle(tr("Perfil de personagem"));d.resize(620,560);auto* root=new QVBoxLayout(&d);auto* form=new QFormLayout;root->addLayout(form);auto* id=new QLineEdit(&d);auto* name=new QLineEdit(&d);auto* font=new QLineEdit(&d);auto* portrait=new QLineEdit(&d);auto* voicePrefix=new QLineEdit(&d);auto* defaultExpression=new QLineEdit(&d);auto* portraitPosition=new QComboBox(&d);portraitPosition->addItem(tr("Esquerda"),QStringLiteral("left"));portraitPosition->addItem(tr("Direita"),QStringLiteral("right"));auto* expressions=new QPlainTextEdit(&d);expressions->setPlaceholderText(tr("feliz=Pictures/hero_feliz.png\ntriste=Pictures/hero_triste.png"));expressions->setMaximumHeight(120);auto* nameColor=new QLineEdit(QStringLiteral("#ffffffff"),&d);auto* textColor=new QLineEdit(QStringLiteral("#ffffffff"),&d);const QString selected=m_speakerProfile->currentData().toString();if(const auto* p=ed.speakerDatabase.findById(selected)){id->setText(p->id);name->setText(p->name);font->setText(p->font);portrait->setText(p->portrait);voicePrefix->setText(p->voicePrefix);defaultExpression->setText(p->defaultExpression);portraitPosition->setCurrentIndex(qMax(0,portraitPosition->findData(p->portraitPosition)));QStringList lines;for(auto it=p->expressionPortraits.cbegin();it!=p->expressionPortraits.cend();++it)lines<<it.key()+QStringLiteral("=")+it.value();expressions->setPlainText(lines.join(QLatin1Char('\n')));if(p->nameColor.isValid())nameColor->setText(p->nameColor.name(QColor::HexArgb));if(p->textColor.isValid())textColor->setText(p->textColor.name(QColor::HexArgb));}form->addRow(tr("ID estável:"),id);form->addRow(tr("Nome:"),name);form->addRow(tr("Fonte:"),font);form->addRow(tr("Retrato padrão:"),portrait);form->addRow(tr("Expressão padrão:"),defaultExpression);form->addRow(tr("Lado do retrato:"),portraitPosition);form->addRow(tr("Retratos por expressão:"),expressions);form->addRow(tr("Pasta/prefixo de voz:"),voicePrefix);form->addRow(tr("Cor do nome:"),nameColor);form->addRow(tr("Cor do texto:"),textColor);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&d,[&]{core::SpeakerProfile p;if(const auto* current=ed.speakerDatabase.findById(selected))p=*current;p.id=id->text().trimmed();p.name=name->text().trimmed();p.font=font->text().trimmed();p.portrait=portrait->text().trimmed();p.voicePrefix=voicePrefix->text().trimmed();p.defaultExpression=defaultExpression->text().trimmed();p.portraitPosition=portraitPosition->currentData().toString();p.expressionPortraits.clear();for(const QString& line:expressions->toPlainText().split(QLatin1Char('\n'))){const int equal=line.indexOf(QLatin1Char('='));if(equal>0){const QString key=line.left(equal).trimmed(),path=line.mid(equal+1).trimmed();if(!key.isEmpty()&&!path.isEmpty())p.expressionPortraits.insert(key,path);}}p.nameColor=QColor(nameColor->text());p.textColor=QColor(textColor->text());if(!ed.speakerDatabase.upsert(p)){QMessageBox::warning(&d,tr("Perfil inválido"),tr("Informe ao menos o nome do personagem."));return;}ed.markDirty();d.accept();});if(d.exec()==QDialog::Accepted){m_speakerProfile->clear();m_speakerProfile->addItem(tr("Sem personagem definido"),QString());for(const auto& p:ed.speakerDatabase.speakers)m_speakerProfile->addItem(p.name,p.id);m_speakerProfile->setCurrentIndex(qMax(0,m_speakerProfile->findData(id->text().trimmed())));refresh();}});

    auto* speakerRow = new QHBoxLayout;
    speakerRow->addWidget(new QLabel(tr("Nome exibido:"), left));
    m_speaker = new QLineEdit(cmd.params.value(QStringLiteral("speaker")).toString(), left);
    m_speaker->setPlaceholderText(tr("Opcional — deixe vazio para ocultar"));
    speakerRow->addWidget(m_speaker, 1);
    v->addLayout(speakerRow);

    m_edit = new QPlainTextEdit(left);
    m_edit->setPlainText(existingDialogue.text.isEmpty()?cmd.params.value(QStringLiteral("text")).toString():existingDialogue.text);
    m_edit->setMinimumHeight(110);
    v->addWidget(m_edit);

    auto* dialogueBox=new QGroupBox(tr("Personagem e fala"),left);auto* dialogueForm=new QFormLayout(dialogueBox);m_expression=new QLineEdit(existingDialogue.expression,dialogueBox);m_expression->setPlaceholderText(tr("Ex.: feliz, irritado, surpresa"));m_dialogueVoice=new QLineEdit(existingDialogue.voiceFile,dialogueBox);m_dialogueVoice->setPlaceholderText(tr("Arquivo relativo ao prefixo de voz do perfil"));m_effectPreset=new QComboBox(dialogueBox);m_effectPreset->addItem(tr("Efeitos configurados abaixo"),QString());for(const auto& preset:core::builtInTextEffectPresets())m_effectPreset->addItem(preset.name,preset.id);for(auto it=ed.textEffectPresets.cbegin();it!=ed.textEffectPresets.cend();++it)m_effectPreset->addItem(it.value().name,it.key());m_effectPreset->setCurrentIndex(qMax(0,m_effectPreset->findData(existingDialogue.textEffectPreset)));dialogueForm->addRow(tr("Expressão:"),m_expression);dialogueForm->addRow(tr("Voz:"),m_dialogueVoice);dialogueForm->addRow(tr("Efeito de texto:"),m_effectPreset);v->addWidget(dialogueBox);

    auto* localizationBox = new QGroupBox(tr("Localização (opcional)"), left);
    auto* localizationForm = new QFormLayout(localizationBox);
    m_localizationKey = new QLineEdit(core::normalizeLocalizationKey(cmd.params.value(QStringLiteral("localizationKey")).toString()), localizationBox);
    m_localizationKey->setPlaceholderText(tr("Ex.: map.town.welcome"));
    auto* textKeyRow = new QWidget(localizationBox); auto* textKeyLayout = new QHBoxLayout(textKeyRow); textKeyLayout->setContentsMargins(0,0,0,0);
    auto* createTextKey = new QPushButton(tr("Criar chave"), textKeyRow); textKeyLayout->addWidget(m_localizationKey,1); textKeyLayout->addWidget(createTextKey);
    m_speakerLocalizationKey = new QLineEdit(core::normalizeLocalizationKey(cmd.params.value(QStringLiteral("speakerLocalizationKey")).toString()), localizationBox);
    m_speakerLocalizationKey->setPlaceholderText(tr("Ex.: npc.maria.name"));
    localizationForm->addRow(tr("Texto:"), textKeyRow); localizationForm->addRow(tr("Nome do personagem:"), m_speakerLocalizationKey);
    auto* localizationHint = new QLabel(tr("Se houver uma tradução para esta chave, ela será usada no idioma atual. Caso contrário, o texto escrito acima continua valendo."), localizationBox);
    localizationHint->setWordWrap(true); localizationForm->addRow(localizationHint); v->addWidget(localizationBox);
    connect(createTextKey,&QPushButton::clicked,this,[this]{
        if(m_localizationKey->text().trimmed().isEmpty())m_localizationKey->setText(QStringLiteral("message.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
        const QString key=core::normalizeLocalizationKey(m_localizationKey->text());m_localizationKey->setText(key);
        if(!key.isEmpty()){ed.localization.ensureDefaults();ed.localization.texts[key][ed.localization.defaultLocale]=m_edit->toPlainText();ed.markDirty();}
    });

    // ---- botões de código ------------------------------------------------
    auto* codes = new QHBoxLayout;
    auto addCode = [&](const QString& rotulo, const QString& codigo, const QString& dica) {
        auto* b = new QPushButton(rotulo, left);
        b->setToolTip(dica);
        connect(b, &QPushButton::clicked, this, [this, codigo] {
            m_edit->insertPlainText(codigo);
            m_edit->setFocus();
        });
        codes->addWidget(b);
    };
    addCode(tr("Cor"), QStringLiteral("\\c[1]"), tr("\\c[n] muda a cor (0 = normal)"));
    addCode(tr("Pausa"), QStringLiteral("\\."), tr("\\. pausa curta · \\| pausa longa"));
    addCode(tr("Espera"), QStringLiteral("\\!"), tr("\\! espera o jogador apertar a tecla"));
    addCode(tr("Rápido"), QStringLiteral("\\>"), tr("\\> trecho instantâneo · \\< volta ao normal"));
    addCode(tr("Variável"), QStringLiteral("\\v[1]"), tr("\\v[n] insere o valor da variável n"));
    codes->addStretch(1);
    v->addLayout(codes);

    auto* fontRow = new QHBoxLayout;
    fontRow->addWidget(new QLabel(tr("Tamanho da letra:"), left));
    m_fontSize = new QSpinBox(left);
    m_fontSize->setRange(0, 96);
    m_fontSize->setSpecialValueText(tr("Padrão do jogo"));
    m_fontSize->setSuffix(tr(" px"));
    m_fontSize->setValue(cmd.params.value(QStringLiteral("fontSize"), 0).toInt());
    fontRow->addWidget(m_fontSize);
    fontRow->addSpacing(16);
    fontRow->addWidget(new QLabel(tr("Se o texto não couber:"), left));
    m_overflow = new QComboBox(left);
    m_overflow->addItem(tr("Continuar em páginas"), QStringLiteral("scroll"));
    m_overflow->addItem(tr("Reduzir fonte para caber"), QStringLiteral("shrink"));
    m_overflow->addItem(tr("Cortar após a primeira página"), QStringLiteral("truncate"));
    m_overflow->setCurrentIndex(qMax(0, m_overflow->findData(cmd.params.value(QStringLiteral("overflow"), QStringLiteral("scroll")))));
    fontRow->addWidget(m_overflow);
    fontRow->addStretch(1);
    v->addLayout(fontRow);

    // ---- posição + página do preview -------------------------------------
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Posição da caixa:"), left));
    m_pos = new QComboBox(left);
    m_pos->addItem(tr("Embaixo"), QStringLiteral("bottom"));
    m_pos->addItem(tr("No meio"), QStringLiteral("middle"));
    m_pos->addItem(tr("Em cima"), QStringLiteral("top"));
    const QString posAtual = cmd.params.value(QStringLiteral("position"),
                                              QStringLiteral("bottom")).toString();
    m_pos->setCurrentIndex(qMax(0, m_pos->findData(posAtual)));
    row->addWidget(m_pos);
    row->addSpacing(16);
    row->addWidget(new QLabel(tr("Ver página:"), left));
    m_pageSpin = new QSpinBox(left);
    m_pageSpin->setRange(1, 1);
    row->addWidget(m_pageSpin);
    row->addStretch(1);
    v->addLayout(row);

    auto* offsetRow = new QHBoxLayout;
    offsetRow->addWidget(new QLabel(tr("Ajuste horizontal:"), left));
    m_offsetX = new QSpinBox(left);
    m_offsetX->setRange(-4096, 4096);
    m_offsetX->setSuffix(QStringLiteral(" px"));
    m_offsetX->setValue(cmd.params.value(QStringLiteral("offsetX")).toInt());
    offsetRow->addWidget(m_offsetX);
    offsetRow->addWidget(new QLabel(tr("Ajuste vertical:"), left));
    m_offsetY = new QSpinBox(left);
    m_offsetY->setRange(-4096, 4096);
    m_offsetY->setSuffix(QStringLiteral(" px"));
    m_offsetY->setValue(cmd.params.value(QStringLiteral("offsetY")).toInt());
    offsetRow->addWidget(m_offsetY);
    offsetRow->addStretch(1);
    v->addLayout(offsetRow);

    m_textEffects = new TextEffectsEditorWidget(
        ed, core::TextEffectStack::fromVariantMap(cmd.params.value(QStringLiteral("textEffects")).toMap()),
        core::TextGradientSpec::fromVariantMap(cmd.params.value(QStringLiteral("textGradient")).toMap()), left);
    m_textEffects->setSampleText(m_edit->toPlainText());
    v->addWidget(m_textEffects);
    connect(m_textEffects, &TextEffectsEditorWidget::changed, this, [this] { refresh(); });
    v->addStretch(1);

    // RC2.64 / Bloco M: preview de comando não ocupa mais uma coluna fixa.
    // O mesmo widget continua sendo atualizado pelos controles, mas vive numa
    // janela sob demanda aberta pelo botão abaixo.
    auto* previewScroll = new QScrollArea(this);
    previewScroll->setWidgetResizable(false);
    m_preview = new MessagePreview(ed, previewScroll);
    previewScroll->setWidget(m_preview);
    m_info = new QLabel(this);
    m_info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    auto* previewDialog = new CommandPreviewDialog(
        tr("Prévia da mensagem · %1 × %2 px").arg(ed.gameResolution.width()).arg(ed.gameResolution.height()),
        previewScroll, this, m_info);
    auto* previewButton = new QPushButton(tr("Ver prévia"), left);
    previewButton->setToolTip(tr("Abre uma prévia em uma janela separada."));
    v->addWidget(previewButton);
    connect(previewButton, &QPushButton::clicked, previewDialog, &CommandPreviewDialog::present);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(box);
    m_box = box;
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_edit, &QPlainTextEdit::textChanged, this, &MessageCommandDialog::refresh);
    connect(m_speaker, &QLineEdit::textChanged, this, &MessageCommandDialog::refresh);
    connect(m_pos, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
    connect(m_offsetX, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(m_offsetY, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(m_pageSpin, &QSpinBox::valueChanged, this, [this](int i) {
        m_preview->setPage(i - 1);
    });
    connect(this, &QDialog::accepted, this, [this] {
        m_cmd.type = QStringLiteral("message");
        QVariantMap params = {{QStringLiteral("text"), m_edit->toPlainText()},
                              {QStringLiteral("localizationKey"), core::normalizeLocalizationKey(m_localizationKey->text())},
                              {QStringLiteral("speaker"), m_speaker->text().trimmed()},
                              {QStringLiteral("speakerLocalizationKey"), core::normalizeLocalizationKey(m_speakerLocalizationKey->text())},
                              {QStringLiteral("position"), m_pos->currentData().toString()},
                              {QStringLiteral("offsetX"), m_offsetX->value()},
                              {QStringLiteral("offsetY"), m_offsetY->value()},
                              {QStringLiteral("fontSize"), m_fontSize->value()},
                              {QStringLiteral("overflow"), m_overflow->currentData().toString()}};
        core::DialogueContent dialogue;dialogue.speakerId=m_speakerProfile->currentData().toString();dialogue.text=m_edit->toPlainText();dialogue.expression=m_expression->text().trimmed();dialogue.voiceFile=m_dialogueVoice->text().trimmed();dialogue.textEffectPreset=m_effectPreset->currentData().toString();if(!dialogue.speakerId.isEmpty()||!dialogue.expression.isEmpty()||!dialogue.voiceFile.isEmpty()||!dialogue.textEffectPreset.isEmpty())params[QStringLiteral("dialogueContent")]=dialogue.toVariantMap();
        const auto effects = m_textEffects->effects();
        const auto gradient = m_textEffects->gradient();
        if (effects.enabled()) params[QStringLiteral("textEffects")] = effects.toVariantMap();
        if (gradient.enabled()) params[QStringLiteral("textGradient")] = gradient.toVariantMap();
        m_cmd.params = params;
    });
    refresh();
}

void MessageCommandDialog::refresh()
{
    m_preview->setPositionId(m_pos->currentData().toString());
    m_preview->setSpeaker(m_speaker->text());
    m_preview->setOffset(m_offsetX->value(), m_offsetY->value());
    m_preview->setBaseFontSize(m_fontSize->value());
    m_preview->setTextEffects(m_textEffects->effects(), m_textEffects->gradient());
    m_preview->setText(m_edit->toPlainText());
    m_textEffects->setSampleText(m_edit->toPlainText());
    m_preview->repaint();
    const int n = qMax(1, m_preview->pageCount());
    {
        QSignalBlocker b(m_pageSpin);
        m_pageSpin->setRange(1, n);
        if (m_pageSpin->value() > n) m_pageSpin->setValue(n);
    }
    m_preview->setPage(m_pageSpin->value() - 1);
    m_info->setText(tr("Páginas: %n · caracteres escritos: %1", "", n)
                        .arg(m_edit->toPlainText().size()));
}


NoCodePluginManagerDialog::NoCodePluginManagerDialog(core::Editor& ed, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Extensões visuais"));
    resize(820, 560);
    auto working = std::make_shared<QVector<NoCodePlugin>>(ed.plugins);
    auto* root = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Extensões .ludoplugin adicionam comandos e formulários ao Editor usando recursos seguros da própria LUDO. Elas não executam DLLs, JavaScript ou código externo."), this);
    hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("color:#aaa"));root->addWidget(hint);
    auto* split = new QSplitter(Qt::Horizontal, this);
    auto* list = new QListWidget(split);
    auto* details = new QTextBrowser(split);
    split->addWidget(list);split->addWidget(details);split->setStretchFactor(1,2);root->addWidget(split,1);
    auto reload = std::make_shared<std::function<void()>>();
    *reload = [=] {
        QSignalBlocker blocker(list);list->clear();
        for (const NoCodePlugin& plugin : *working) {
            auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(plugin.name, plugin.version), list);
            item->setData(Qt::UserRole, plugin.id);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
            item->setCheckState(plugin.enabled?Qt::Checked:Qt::Unchecked);
        }
        if(list->count()>0)list->setCurrentRow(0);else details->clear();
    };
    connect(list,&QListWidget::currentRowChanged,this,[=](int row){
        if(row<0||row>=working->size()){details->clear();return;}
        const NoCodePlugin& plugin=working->at(row);QStringList commands;
        for(const PluginCommand& command:plugin.commands)commands.push_back(QStringLiteral("• %1 — %2").arg(command.category,command.name));
        const QString dependencies=plugin.dependencies.isEmpty()?tr("Nenhuma dependência"):tr("Depende de: %1").arg(plugin.dependencies.join(QStringLiteral(", ")));
        details->setHtml(tr("<h2>%1</h2><p><b>ID:</b> %2<br><b>Versão:</b> %3<br><b>Autor:</b> %4</p><p>%5</p><p>%6</p><p><b>Comandos:</b><br>%7</p>")
                             .arg(plugin.name,plugin.id,plugin.version,plugin.author,plugin.description,
                                  dependencies,commands.join(QStringLiteral("<br>"))));
    });
    connect(list,&QListWidget::itemChanged,this,[=](QListWidgetItem* item){
        const QString id=item->data(Qt::UserRole).toString();for(NoCodePlugin& plugin:*working)if(plugin.id==id){plugin.enabled=item->checkState()==Qt::Checked;break;}
    });
    auto* actions = new QHBoxLayout;
    auto* importButton = new QPushButton(tr("Instalar pacote…"), this);
    auto* newButton = new QPushButton(tr("Criar pacote visual…"), this);
    auto* composeButton = new QPushButton(tr("Novo / editar comando…"), this);
    auto* removeButton = new QPushButton(tr("Remover"), this);
    auto* exampleButton = new QPushButton(tr("Salvar exemplo para autores…"), this);
    actions->addWidget(importButton);actions->addWidget(newButton);actions->addWidget(composeButton);actions->addWidget(removeButton);actions->addStretch(1);actions->addWidget(exampleButton);root->addLayout(actions);
    connect(importButton,&QPushButton::clicked,this,[=]{
        const QString path=QFileDialog::getOpenFileName(this,tr("Instalar extensão visual"),QString(),tr("Extensão LUDO (*.ludoplugin *.json)"));if(path.isEmpty())return;
        QFile file(path);if(!file.open(QIODevice::ReadOnly)){QMessageBox::warning(this,tr("Extensão"),file.errorString());return;}
        QJsonParseError parse;const QJsonDocument document=QJsonDocument::fromJson(file.readAll(),&parse);
        if(parse.error!=QJsonParseError::NoError||!document.isObject()){QMessageBox::warning(this,tr("Extensão"),tr("JSON inválido: %1").arg(parse.errorString()));return;}
        NoCodePlugin plugin;QString error;if(!noCodePluginFromJson(document.object(),&plugin,&error)){QMessageBox::warning(this,tr("Extensão"),error);return;}
        QStringList missingDependencies;for(const QString& dependency:plugin.dependencies){bool found=false;for(const NoCodePlugin& installed:*working)if(installed.id==dependency&&installed.enabled){found=true;break;}if(!found)missingDependencies.push_back(dependency);}if(!missingDependencies.isEmpty()&&QMessageBox::question(this,tr("Dependências ausentes"),tr("Esta extensão depende das seguintes extensões ativas:\n\n%1\n\nInstalar mesmo assim? O teste do jogo ficará bloqueado até as dependências serem resolvidas.").arg(missingDependencies.join(QLatin1Char('\n'))),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        int duplicate=-1;for(int i=0;i<working->size();++i)if(working->at(i).id==plugin.id){duplicate=i;break;}
        if(duplicate>=0){if(QMessageBox::question(this,tr("Atualizar extensão"),tr("Substituir %1 pela versão %2?").arg(plugin.name,plugin.version))!=QMessageBox::Yes)return;(*working)[duplicate]=plugin;}else working->push_back(plugin);
        (*reload)();
    });
    connect(removeButton,&QPushButton::clicked,this,[=]{const int row=list->currentRow();if(row<0||row>=working->size())return;if(QMessageBox::question(this,tr("Remover extensão"),tr("Esta extensão pode estar sendo usada por eventos do projeto. Se você removê-la, esses comandos deixarão de funcionar. Remover mesmo assim?"))!=QMessageBox::Yes)return;working->remove(row);(*reload)();});
    connect(newButton,&QPushButton::clicked,this,[=]{
        bool ok=false;const QString id=QInputDialog::getText(this,tr("Novo pacote visual"),tr("ID estável (ex.: meu.jogo.dialogo):"),QLineEdit::Normal,QString(),&ok).trimmed();if(!ok||id.isEmpty())return;
        for(const NoCodePlugin& existing:*working)if(existing.id==id){QMessageBox::warning(this,tr("Novo pacote"),tr("Já existe um pacote com esse ID."));return;}
        const QString name=QInputDialog::getText(this,tr("Novo pacote visual"),tr("Nome mostrado no editor:"),QLineEdit::Normal,id,&ok).trimmed();if(!ok||name.isEmpty())return;
        NoCodePlugin plugin;plugin.id=id;plugin.name=name;plugin.author=tr("Projeto atual");plugin.description=tr("Extensão criada pelo compositor visual da LUDO.");
        PluginCommand seed;seed.id=QStringLiteral("novo_comando");seed.name=tr("Novo comando");seed.category=tr("Extensões visuais");seed.commands={{QStringLiteral("comment"),{{QStringLiteral("text"),tr("Edite o comportamento deste comando no compositor visual.")}}}};
        PluginCommandComposer composer(seed,this);if(composer.exec()!=QDialog::Accepted)return;plugin.commands.push_back(composer.command());working->push_back(plugin);(*reload)();list->setCurrentRow(working->size()-1);
    });
    connect(composeButton,&QPushButton::clicked,this,[=]{
        const int pluginRow=list->currentRow();if(pluginRow<0||pluginRow>=working->size())return;NoCodePlugin& plugin=(*working)[pluginRow];
        QStringList choices;for(const PluginCommand& command:plugin.commands)choices.push_back(command.name);choices.push_back(tr("+ Criar novo comando"));bool ok=false;const QString choice=QInputDialog::getItem(this,tr("Compositor visual"),tr("Comando:"),choices,0,false,&ok);if(!ok)return;const int commandRow=choices.indexOf(choice);
        PluginCommand initial;if(commandRow>=0&&commandRow<plugin.commands.size())initial=plugin.commands.at(commandRow);else{initial.id=QStringLiteral("comando_%1").arg(plugin.commands.size()+1);initial.name=tr("Novo comando");initial.category=plugin.name;initial.commands={{QStringLiteral("comment"),{{QStringLiteral("text"),tr("Comando criado visualmente")}}}};}
        PluginCommandComposer composer(initial,this);if(composer.exec()!=QDialog::Accepted)return;for(int i=0;i<plugin.commands.size();++i)if(i!=commandRow&&plugin.commands.at(i).id==composer.command().id){QMessageBox::warning(this,tr("Compositor"),tr("Outro comando deste pacote já usa esse ID."));return;}if(commandRow>=0&&commandRow<plugin.commands.size())plugin.commands[commandRow]=composer.command();else plugin.commands.push_back(composer.command());(*reload)();list->setCurrentRow(pluginRow);
    });
    connect(exampleButton,&QPushButton::clicked,this,[=]{
        NoCodePlugin plugin;plugin.id=QStringLiteral("exemplo.recompensa");plugin.name=tr("Exemplo de recompensa");plugin.author=tr("Autor do jogo");plugin.description=tr("Mostra como criar um comando visual seguro.");
        PluginCommand command;command.id=QStringLiteral("dar_ouro");command.name=tr("Dar ouro e avisar");command.category=tr("Recompensas");command.description=tr("Soma ouro e mostra uma mensagem.");
        command.fields={{QStringLiteral("quantidade"),tr("Quantidade"),QStringLiteral("integer"),100,0,999999999,{}},{QStringLiteral("mensagem"),tr("Mensagem"),QStringLiteral("text"),tr("Você recebeu ouro!"),0,0,{}}};
        command.commands={{QStringLiteral("party.gold"),{{QStringLiteral("operation"),QStringLiteral("add")},{QStringLiteral("amount"),QStringLiteral("${quantidade}")}}},{QStringLiteral("message"),{{QStringLiteral("text"),QStringLiteral("${mensagem}")},{QStringLiteral("position"),QStringLiteral("bottom")}}}};plugin.commands.push_back(command);
        QString path=QFileDialog::getSaveFileName(this,tr("Salvar extensão de exemplo"),QStringLiteral("exemplo-recompensa.ludoplugin"),tr("Extensão LUDO (*.ludoplugin)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".ludoplugin"),Qt::CaseInsensitive))path+=QStringLiteral(".ludoplugin");QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(QJsonDocument(noCodePluginToJson(plugin)).toJson(QJsonDocument::Indented))<0){QMessageBox::warning(this,tr("Extensão"),tr("Não foi possível salvar o exemplo."));return;}QMessageBox::information(this,tr("Extensão"),tr("Exemplo salvo. Você pode editar o arquivo e instalá-lo nesta mesma janela para testar."));
    });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);root->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,[&,working]{ed.plugins=*working;ed.markDirty();accept();});
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    (*reload)();
}

class DatabaseRelationButton final : public QPushButton
{
public:
    explicit DatabaseRelationButton(const QString& title,QWidget* parent=nullptr)
        : QPushButton(parent),m_title(title)
    {
        connect(this,&QPushButton::clicked,this,[this]{choose();});refreshLabel();
    }
    void setChoices(const QVector<QPair<QString,QString>>& choices){m_choices=choices;refreshLabel();}
    void setIds(const QStringList& ids){m_ids=ids;m_ids.removeDuplicates();refreshLabel();}
    QStringList ids() const{return m_ids;}
private:
    void choose(){
        QDialog dialog(this);dialog.setWindowTitle(m_title);dialog.resize(520,540);auto* root=new QVBoxLayout(&dialog);
        auto* help=new QLabel(tr("Marque os registros e confirme. Você não precisa digitar códigos ou IDs."),&dialog);help->setWordWrap(true);root->addWidget(help);
        auto* list=new QListWidget(&dialog);for(const auto& choice:m_choices){auto* item=new QListWidgetItem(choice.second,list);item->setData(Qt::UserRole,choice.first);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(m_ids.contains(choice.first)?Qt::Checked:Qt::Unchecked);}root->addWidget(list,1);
        auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);root->addWidget(box);connect(box,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;QStringList selected;for(int i=0;i<list->count();++i)if(list->item(i)->checkState()==Qt::Checked)selected.push_back(list->item(i)->data(Qt::UserRole).toString());setIds(selected);
    }
    void refreshLabel(){
        QStringList names;for(const auto& choice:m_choices)if(m_ids.contains(choice.first))names.push_back(choice.second);
        setText(names.isEmpty()?(m_ids.isEmpty()?tr("Escolher visualmente…"):tr("Selecionados: %1").arg(m_ids.size())):names.join(QStringLiteral(", ")));
        setToolTip(names.join(QLatin1Char('\n')));
    }
    QString m_title;QStringList m_ids;QVector<QPair<QString,QString>> m_choices;
};

DatabaseDialog::DatabaseDialog(core::Editor& ed,QWidget* parent):QDialog(parent)
{
    setWindowTitle(tr("Banco de Dados — Ludo Engine"));
    resize(1180, 760);
    auto working = std::make_shared<QHash<QString,QVector<DatabaseRecord>>>(ed.database);
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    auto* cats = new QListWidget(split);
    for (const QString& categoryId : databaseCategories()) {
        auto* item = new QListWidgetItem(databaseCategoryLabel(categoryId), cats);
        item->setData(Qt::UserRole, categoryId);
    }
    auto* records = new QListWidget(split);
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    auto* panel = new QWidget(scroll);
    auto* form = new QFormLayout(panel);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(panel);

    auto spin = [panel](int minimum, int maximum) {
        auto* field = new QSpinBox(panel);
        field->setRange(minimum, maximum);
        return field;
    };
    auto decimal = [panel](double minimum, double maximum, double step) {
        auto* field = new QDoubleSpinBox(panel);
        field->setRange(minimum, maximum);
        field->setSingleStep(step);
        field->setDecimals(2);
        return field;
    };
    auto* name = new QLineEdit(panel);
    auto* nameTextKey = new QLineEdit(panel); nameTextKey->setPlaceholderText(tr("Opcional — ex.: item.potion.name"));
    auto* descriptionTextKey = new QLineEdit(panel); descriptionTextKey->setPlaceholderText(tr("Opcional — ex.: item.potion.description"));
    auto* icon = new QPushButton(tr("Escolher no IconSet…"), panel);
    auto* description = new QPlainTextEdit(panel);
    description->setMaximumHeight(90);
    auto* classId = new QComboBox(panel);
    auto* initialParty = new QCheckBox(tr("Começa no grupo"), panel);
    auto* initialLevel = spin(1, 999);
    auto* maxLevel = spin(1, 999);
    auto* initialGold = spin(0, 99999999);
    auto* initialAmount = spin(0, 9999);
    auto* showInInventory = new QCheckBox(tr("Exibir automaticamente no Inventory Grid"), panel);
    showInInventory->setToolTip(tr("Quando o jogador possuir este registro, seu ícone e quantidade aparecem nas grades de inventário da UI."));
    auto* price = spin(0, 99999999);
    auto* hp = spin(0, 999999);
    auto* mp = spin(0, 999999);
    auto* attack = spin(0, 999999);
    auto* defense = spin(0, 999999);
    auto* agility = spin(0, 999999);
    auto* hpGrowth = spin(0, 99999);
    auto* mpGrowth = spin(0, 99999);
    auto* attackGrowth = spin(0, 99999);
    auto* defenseGrowth = spin(0, 99999);
    auto* agilityGrowth = spin(0, 99999);
    auto* power = spin(-999999, 999999);
    auto* mpCost = spin(0, 99999);
    auto* hitRate = spin(1,100);hitRate->setSuffix(QStringLiteral("%"));
    auto* evasion = spin(0,95);evasion->setSuffix(QStringLiteral("%"));
    auto* critical = spin(0,100);critical->setSuffix(QStringLiteral("%"));
    auto* useChance = spin(0,100);useChance->setSuffix(QStringLiteral("%"));
    auto* stateChance = spin(0,100);stateChance->setSuffix(QStringLiteral("%"));
    auto* element = new QComboBox(panel);
    element->addItem(tr("Sem elemento"),QString());
    const QVector<QPair<QString,QString>> elementChoices{{QStringLiteral("fire"),tr("Fogo")},{QStringLiteral("ice"),tr("Gelo")},{QStringLiteral("thunder"),tr("Trovão")},{QStringLiteral("water"),tr("Água")},{QStringLiteral("earth"),tr("Terra")},{QStringLiteral("wind"),tr("Vento")},{QStringLiteral("light"),tr("Luz")},{QStringLiteral("dark"),tr("Trevas")}};
    for(const auto& entry:elementChoices)element->addItem(entry.second,entry.first);
    auto* elementRatesWidget=new QWidget(panel);auto* elementRatesLayout=new QGridLayout(elementRatesWidget);elementRatesLayout->setContentsMargins(0,0,0,0);QHash<QString,QSpinBox*> elementRates;
    for(int i=0;i<elementChoices.size();++i){const auto& entry=elementChoices[i];auto* rate=spin(0,500);rate->setSuffix(QStringLiteral("%"));rate->setValue(100);elementRates[entry.first]=rate;const int row=i/2,col=(i%2)*2;elementRatesLayout->addWidget(new QLabel(entry.second,elementRatesWidget),row,col);elementRatesLayout->addWidget(rate,row,col+1);}
    auto* stateAddId=new QComboBox(panel);auto* stateRemoveId=new QComboBox(panel);auto* animationId=new QComboBox(panel);
    auto* healHp = spin(-999999, 999999);
    auto* healMp = spin(-999999, 999999);
    auto* consumable = new QCheckBox(tr("Consumível"), panel);
    auto* scope = new QComboBox(panel);
    scope->addItem(tr("Um aliado"), QStringLiteral("allyOne"));
    scope->addItem(tr("Todos os aliados"), QStringLiteral("allyAll"));
    scope->addItem(tr("Um inimigo"), QStringLiteral("enemyOne"));
    scope->addItem(tr("Todos os inimigos"), QStringLiteral("enemyAll"));
    scope->addItem(tr("O próprio usuário"), QStringLiteral("self"));
    auto* equipmentSlot = new QComboBox(panel);
    equipmentSlot->addItem(tr("Armadura"), QStringLiteral("armor"));
    equipmentSlot->addItem(tr("Acessório"), QStringLiteral("accessory"));
    auto* experience = spin(0, 99999999);
    auto* gold = spin(0, 99999999);
    auto* enemyGraphic=new QLineEdit(panel);enemyGraphic->setReadOnly(true);auto* enemyGraphicRow=new QWidget(panel);auto* enemyGraphicLayout=new QHBoxLayout(enemyGraphicRow);enemyGraphicLayout->setContentsMargins(0,0,0,0);auto* chooseEnemyGraphic=new QPushButton(tr("Escolher…"),enemyGraphicRow);auto* clearEnemyGraphic=new QPushButton(tr("Limpar"),enemyGraphicRow);enemyGraphicLayout->addWidget(enemyGraphic,1);enemyGraphicLayout->addWidget(chooseEnemyGraphic);enemyGraphicLayout->addWidget(clearEnemyGraphic);
    connect(chooseEnemyGraphic,&QPushButton::clicked,this,[&ed,enemyGraphic,this]{const QString path=AssetBrowserDialog::chooseImage(ed,this,QStringLiteral("Pictures"));if(!path.isEmpty())enemyGraphic->setText(ed.projectRelativePath(path));});connect(clearEnemyGraphic,&QPushButton::clicked,enemyGraphic,&QLineEdit::clear);
    auto* enemyIds = new DatabaseRelationButton(tr("Escolher inimigos da tropa"),panel);
    auto* skillIds = new DatabaseRelationButton(tr("Escolher habilidades"),panel);
    auto* lootId = new QComboBox(panel);
    auto* lootChance = spin(0, 100);
    lootChance->setSuffix(QStringLiteral("%"));
    auto* encounterWeight = spin(1, 9999);
    auto* stateDuration = spin(0, 9999);
    auto* attackRate = decimal(0.0, 10.0, 0.05);
    auto* defenseRate = decimal(0.0, 10.0, 0.05);
    auto* agilityRate = decimal(0.0,10.0,0.05);
    auto* hpDamageRate = spin(0,100);hpDamageRate->setSuffix(QStringLiteral("% do HP máximo"));
    auto* animationFrames = spin(1, 999);
    auto* animationEditor = new QPushButton(tr("Abrir Animation Editor…"), panel);
    auto* enemyAiEditor = new QPushButton(tr("Editar regras de IA…"), panel);
    auto* troopFormationEditor = new QPushButton(tr("Editar formação…"), panel);
    auto* troopEventsEditor = new QPushButton(tr("Editar Battle Events…"), panel);

    form->addRow(tr("Nome:"), name);
    form->addRow(tr("Chave do nome:"), nameTextKey);
    form->addRow(tr("Ícone:"), icon);
    form->addRow(tr("Descrição:"), description);
    form->addRow(tr("Chave da descrição:"), descriptionTextKey);
    form->addRow(tr("Classe:"), classId);
    form->addRow(initialParty);
    form->addRow(tr("Nível inicial:"), initialLevel);
    form->addRow(tr("Nível máximo:"), maxLevel);
    form->addRow(tr("Ouro inicial:"), initialGold);
    form->addRow(tr("Quantidade inicial:"), initialAmount);
    form->addRow(showInInventory);
    form->addRow(tr("Preço:"), price);
    form->addRow(tr("HP base / bônus:"), hp);
    form->addRow(tr("MP base / bônus:"), mp);
    form->addRow(tr("Ataque base / bônus:"), attack);
    form->addRow(tr("Defesa base / bônus:"), defense);
    form->addRow(tr("Agilidade base / bônus:"), agility);
    form->addRow(tr("HP por nível:"), hpGrowth);
    form->addRow(tr("MP por nível:"), mpGrowth);
    form->addRow(tr("Ataque por nível:"), attackGrowth);
    form->addRow(tr("Defesa por nível:"), defenseGrowth);
    form->addRow(tr("Agilidade por nível:"), agilityGrowth);
    form->addRow(tr("Poder:"), power);
    form->addRow(tr("Custo de MP:"), mpCost);
    form->addRow(tr("Chance de acerto:"),hitRate);
    form->addRow(tr("Evasão:"),evasion);
    form->addRow(tr("Chance de crítico:"),critical);
    form->addRow(tr("Chance de uso pela IA:"),useChance);
    form->addRow(tr("Elemento:"),element);
    form->addRow(tr("Dano elemental recebido:"),elementRatesWidget);
    form->addRow(tr("Aplicar estado:"),stateAddId);
    form->addRow(tr("Chance do estado:"),stateChance);
    form->addRow(tr("Remover estado:"),stateRemoveId);
    form->addRow(tr("Animação:"),animationId);
    form->addRow(tr("Recupera HP:"), healHp);
    form->addRow(tr("Recupera MP:"), healMp);
    form->addRow(consumable);
    form->addRow(tr("Alvo:"), scope);
    form->addRow(tr("Espaço:"), equipmentSlot);
    form->addRow(tr("EXP concedida:"), experience);
    form->addRow(tr("Ouro concedido:"), gold);
    form->addRow(tr("Gráfico na batalha:"),enemyGraphicRow);
    form->addRow(tr("Inimigos da tropa:"), enemyIds);
    form->addRow(tr("Habilidades:"), skillIds);
    form->addRow(tr("Recompensa de item:"), lootId);
    form->addRow(tr("Chance da recompensa:"), lootChance);
    form->addRow(tr("Peso em encontros:"), encounterWeight);
    form->addRow(tr("Duração em turnos:"), stateDuration);
    form->addRow(tr("Multiplicador de ataque:"), attackRate);
    form->addRow(tr("Multiplicador de defesa:"), defenseRate);
    form->addRow(tr("Multiplicador de agilidade:"),agilityRate);
    form->addRow(tr("Dano por turno:"),hpDamageRate);
    form->addRow(tr("Quadros da animação:"), animationFrames);
    form->addRow(tr("Editor de animação:"), animationEditor);
    form->addRow(tr("IA do inimigo:"), enemyAiEditor);
    form->addRow(tr("Formação da tropa:"), troopFormationEditor);
    form->addRow(tr("Eventos de batalha:"), troopEventsEditor);

    split->addWidget(cats);
    split->addWidget(records);
    split->addWidget(scroll);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 3);
    root->addWidget(split, 1);

    auto current = std::make_shared<int>(-1);
    auto category = std::make_shared<QString>();
    auto variantIds = [](const QVariant& value) {
        QStringList result = value.toStringList();
        if (!result.isEmpty()) return result;
        for (const QVariant& entry : value.toList()) {
            const QString id = entry.toString().trimmed();
            if (!id.isEmpty()) result.push_back(id);
        }
        return result;
    };
    auto setRowVisible = [form](QWidget* field, bool visible) {
        field->setVisible(visible);
        if (QWidget* label = form->labelForField(field)) label->setVisible(visible);
    };
    auto showFields = [=] {
        const QString c = *category;
        const bool actor = c == QLatin1String("actors");
        const bool klass = c == QLatin1String("classes");
        const bool skill = c == QLatin1String("skills");
        const bool item = c == QLatin1String("items");
        const bool weapon = c == QLatin1String("weapons");
        const bool armor = c == QLatin1String("armors");
        const bool state = c == QLatin1String("states");
        const bool enemy = c == QLatin1String("enemies");
        const bool troop = c == QLatin1String("troops");
        const bool animation = c == QLatin1String("animations");
        setRowVisible(classId, actor);
        setRowVisible(initialParty, actor);
        setRowVisible(initialLevel, actor);
        setRowVisible(maxLevel, actor || klass);
        setRowVisible(initialGold, actor);
        setRowVisible(initialAmount, item || weapon || armor);
        setRowVisible(showInInventory, item || weapon || armor);
        setRowVisible(price, item || weapon || armor || skill);
        setRowVisible(hp, actor || klass || weapon || armor || enemy);
        setRowVisible(mp, actor || klass || weapon || armor || enemy);
        setRowVisible(attack, actor || klass || weapon || armor || enemy);
        setRowVisible(defense, actor || klass || weapon || armor || enemy);
        setRowVisible(agility, actor || klass || weapon || armor || enemy);
        for (QSpinBox* field : {hpGrowth, mpGrowth, attackGrowth, defenseGrowth, agilityGrowth})
            setRowVisible(field, actor || klass);
        setRowVisible(power, skill || item);
        setRowVisible(mpCost, skill);
        setRowVisible(hitRate,actor||skill||enemy);
        setRowVisible(evasion,actor||enemy);
        setRowVisible(critical,actor||skill||enemy);
        setRowVisible(useChance,skill);
        setRowVisible(element,skill);
        setRowVisible(elementRatesWidget,actor||enemy);
        setRowVisible(stateAddId,skill||item);
        setRowVisible(stateChance,skill||item);
        setRowVisible(stateRemoveId,skill||item);
        setRowVisible(animationId,skill||item);
        setRowVisible(healHp, item || skill);
        setRowVisible(healMp, item || skill);
        setRowVisible(consumable, item);
        setRowVisible(scope, item || skill);
        setRowVisible(equipmentSlot, armor);
        setRowVisible(experience, enemy);
        setRowVisible(gold, enemy);
        setRowVisible(enemyGraphicRow,enemy);
        setRowVisible(enemyIds, troop);
        setRowVisible(skillIds, actor || klass || enemy);
        setRowVisible(lootId, enemy);
        setRowVisible(lootChance, enemy);
        setRowVisible(encounterWeight, troop);
        setRowVisible(stateDuration, state);
        setRowVisible(attackRate, state);
        setRowVisible(defenseRate, state);
        setRowVisible(agilityRate,state);
        setRowVisible(hpDamageRate,state);
        setRowVisible(animationFrames, animation);
        setRowVisible(animationEditor, animation);
        setRowVisible(enemyAiEditor, enemy);
        setRowVisible(troopFormationEditor, troop);
        setRowVisible(troopEventsEditor, troop);
        // Ataques normais também podem apontar para uma animação. Skills/Itens
        // já usavam o mesmo campo animationId.
        setRowVisible(animationId, skill || item || actor || weapon || enemy);
    };
    auto rebuildRelations = [=] {
        const QVariant oldClass = classId->currentData();
        classId->clear();
        classId->addItem(tr("(atributos do personagem)"), QString());
        for (const DatabaseRecord& record : working->value(QStringLiteral("classes")))
            classId->addItem(record.name, record.id);
        classId->setCurrentIndex(qMax(0, classId->findData(oldClass)));
        const QVariant oldLoot = lootId->currentData();
        lootId->clear();
        lootId->addItem(tr("(nenhuma)"), QString());
        for (const QString& cat : {QStringLiteral("items"), QStringLiteral("weapons"), QStringLiteral("armors")})
            for (const DatabaseRecord& record : working->value(cat))
                lootId->addItem(QStringLiteral("%1 — %2").arg(databaseCategoryLabel(cat), record.name), record.id);
        lootId->setCurrentIndex(qMax(0, lootId->findData(oldLoot)));
        auto fillRelation=[working](QComboBox* combo,const QString& category,const QString& none){const QVariant old=combo->currentData();combo->clear();combo->addItem(none,QString());for(const DatabaseRecord& record:working->value(category))combo->addItem(record.name,record.id);combo->setCurrentIndex(qMax(0,combo->findData(old)));};
        fillRelation(stateAddId,QStringLiteral("states"),tr("(nenhum)"));fillRelation(stateRemoveId,QStringLiteral("states"),tr("(nenhum)"));fillRelation(animationId,QStringLiteral("animations"),tr("(nenhuma)"));
        QVector<QPair<QString,QString>> enemies;for(const DatabaseRecord& record:working->value(QStringLiteral("enemies")))enemies.push_back({record.id,record.name});enemyIds->setChoices(enemies);
        QVector<QPair<QString,QString>> skills;for(const DatabaseRecord& record:working->value(QStringLiteral("skills")))skills.push_back({record.id,record.name});skillIds->setChoices(skills);
    };
    auto saveCurrent = [=] {
        if (category->isEmpty() || *current < 0 || *current >= (*working)[*category].size()) return;
        DatabaseRecord& record = (*working)[*category][*current];
        record.name = name->text().trimmed();
        record.description = description->toPlainText();
        record.data[QStringLiteral("nameTextKey")] = core::normalizeLocalizationKey(nameTextKey->text());
        record.data[QStringLiteral("descriptionTextKey")] = core::normalizeLocalizationKey(descriptionTextKey->text());
        record.data[QStringLiteral("classId")] = classId->currentData();
        record.data[QStringLiteral("initialParty")] = initialParty->isChecked();
        record.data[QStringLiteral("initialLevel")] = initialLevel->value();
        record.data[QStringLiteral("maxLevel")] = maxLevel->value();
        record.data[QStringLiteral("initialGold")] = initialGold->value();
        record.data[QStringLiteral("initialAmount")] = initialAmount->value();
        record.data[QStringLiteral("showInInventory")] = showInInventory->isChecked();
        record.data[QStringLiteral("price")] = price->value();
        record.data[QStringLiteral("hp")] = hp->value();
        record.data[QStringLiteral("mp")] = mp->value();
        record.data[QStringLiteral("attack")] = attack->value();
        record.data[QStringLiteral("defense")] = defense->value();
        record.data[QStringLiteral("agility")] = agility->value();
        record.data[QStringLiteral("hpGrowth")] = hpGrowth->value();
        record.data[QStringLiteral("mpGrowth")] = mpGrowth->value();
        record.data[QStringLiteral("attackGrowth")] = attackGrowth->value();
        record.data[QStringLiteral("defenseGrowth")] = defenseGrowth->value();
        record.data[QStringLiteral("agilityGrowth")] = agilityGrowth->value();
        record.data[QStringLiteral("power")] = power->value();
        record.data[QStringLiteral("mpCost")] = mpCost->value();
        record.data[QStringLiteral("hitRate")]=hitRate->value();
        record.data[QStringLiteral("evasion")]=evasion->value();
        record.data[QStringLiteral("critical")]=critical->value();
        record.data[QStringLiteral("useChance")]=useChance->value();
        record.data[QStringLiteral("element")]=element->currentData();
        QVariantMap savedElementRates;for(auto it=elementRates.cbegin();it!=elementRates.cend();++it)savedElementRates[it.key()]=it.value()->value()/100.0;record.data[QStringLiteral("elementRates")]=savedElementRates;
        record.data[QStringLiteral("stateAddId")]=stateAddId->currentData();
        record.data[QStringLiteral("stateChance")]=stateChance->value();
        record.data[QStringLiteral("stateRemoveId")]=stateRemoveId->currentData();
        record.data[QStringLiteral("animationId")]=animationId->currentData();
        record.data[QStringLiteral("healHp")] = healHp->value();
        record.data[QStringLiteral("healMp")] = healMp->value();
        record.data[QStringLiteral("consumable")] = consumable->isChecked();
        record.data[QStringLiteral("scope")] = scope->currentData();
        if (*category == QLatin1String("weapons"))
            record.data[QStringLiteral("slot")] = QStringLiteral("weapon");
        else if (*category == QLatin1String("armors"))
            record.data[QStringLiteral("slot")] = equipmentSlot->currentData();
        record.data[QStringLiteral("experience")] = experience->value();
        record.data[QStringLiteral("gold")] = gold->value();
        record.data[QStringLiteral("graphicPath")]=enemyGraphic->text();
        record.data[QStringLiteral("enemyIds")] = enemyIds->ids();
        record.data[QStringLiteral("skillIds")] = skillIds->ids();
        record.data[QStringLiteral("lootId")] = lootId->currentData();
        record.data[QStringLiteral("lootChance")] = lootChance->value();
        record.data[QStringLiteral("encounterWeight")] = encounterWeight->value();
        record.data[QStringLiteral("duration")] = stateDuration->value();
        record.data[QStringLiteral("attackRate")] = attackRate->value();
        record.data[QStringLiteral("defenseRate")] = defenseRate->value();
        record.data[QStringLiteral("agilityRate")]=agilityRate->value();
        record.data[QStringLiteral("hpDamageRate")]=hpDamageRate->value();
        record.data[QStringLiteral("frames")] = animationFrames->value();
    };
    auto loadCurrent = [=, &ed] {
        if (category->isEmpty() || *current < 0 || *current >= (*working)[*category].size()) {
            name->clear();
            nameTextKey->clear(); descriptionTextKey->clear();
            description->clear();
            icon->setText(tr("Escolher no IconSet…"));
            return;
        }
        const DatabaseRecord& record = (*working)[*category][*current];
        const QVariantMap& data = record.data;
        name->setText(record.name);
        nameTextKey->setText(data.value(QStringLiteral("nameTextKey")).toString());
        descriptionTextKey->setText(data.value(QStringLiteral("descriptionTextKey")).toString());
        description->setPlainText(record.description);
        icon->setText(record.icon >= 0 ? tr("I[%1]").arg(record.icon) : tr("Escolher no IconSet…"));
        icon->setIcon(inputIconPreview(ed, record.icon));
        classId->setCurrentIndex(qMax(0, classId->findData(data.value(QStringLiteral("classId")))));
        initialParty->setChecked(data.value(QStringLiteral("initialParty"), false).toBool());
        initialLevel->setValue(data.value(QStringLiteral("initialLevel"), 1).toInt());
        maxLevel->setValue(data.value(QStringLiteral("maxLevel"), 99).toInt());
        initialGold->setValue(data.value(QStringLiteral("initialGold"), 0).toInt());
        initialAmount->setValue(data.value(QStringLiteral("initialAmount"), 0).toInt());
        showInInventory->setChecked(data.value(QStringLiteral("showInInventory"), true).toBool());
        price->setValue(data.value(QStringLiteral("price"), 0).toInt());
        hp->setValue(data.value(QStringLiteral("hp"), 100).toInt());
        mp->setValue(data.value(QStringLiteral("mp"), 30).toInt());
        attack->setValue(data.value(QStringLiteral("attack"), 10).toInt());
        defense->setValue(data.value(QStringLiteral("defense"), 8).toInt());
        agility->setValue(data.value(QStringLiteral("agility"), 10).toInt());
        hpGrowth->setValue(data.value(QStringLiteral("hpGrowth"), 12).toInt());
        mpGrowth->setValue(data.value(QStringLiteral("mpGrowth"), 4).toInt());
        attackGrowth->setValue(data.value(QStringLiteral("attackGrowth"), 2).toInt());
        defenseGrowth->setValue(data.value(QStringLiteral("defenseGrowth"), 2).toInt());
        agilityGrowth->setValue(data.value(QStringLiteral("agilityGrowth"), 1).toInt());
        power->setValue(data.value(QStringLiteral("power"), 0).toInt());
        mpCost->setValue(data.value(QStringLiteral("mpCost"), 0).toInt());
        hitRate->setValue(data.value(QStringLiteral("hitRate"),95).toInt());
        evasion->setValue(data.value(QStringLiteral("evasion"),5).toInt());
        critical->setValue(data.value(QStringLiteral("critical"),5).toInt());
        useChance->setValue(data.value(QStringLiteral("useChance"),100).toInt());
        stateChance->setValue(data.value(QStringLiteral("stateChance"),100).toInt());
        element->setCurrentIndex(qMax(0,element->findData(data.value(QStringLiteral("element")))));
        const QVariantMap savedElementRates=data.value(QStringLiteral("elementRates")).toMap();for(auto it=elementRates.cbegin();it!=elementRates.cend();++it)it.value()->setValue(qBound(0,qRound(savedElementRates.value(it.key(),1.0).toDouble()*100.0),500));
        stateAddId->setCurrentIndex(qMax(0,stateAddId->findData(data.value(QStringLiteral("stateAddId")))));
        stateRemoveId->setCurrentIndex(qMax(0,stateRemoveId->findData(data.value(QStringLiteral("stateRemoveId")))));
        animationId->setCurrentIndex(qMax(0,animationId->findData(data.value(QStringLiteral("animationId")))));
        healHp->setValue(data.value(QStringLiteral("healHp"), 0).toInt());
        healMp->setValue(data.value(QStringLiteral("healMp"), 0).toInt());
        consumable->setChecked(data.value(QStringLiteral("consumable"), true).toBool());
        scope->setCurrentIndex(qMax(0, scope->findData(data.value(QStringLiteral("scope"), QStringLiteral("allyOne")))));
        equipmentSlot->setCurrentIndex(qMax(0, equipmentSlot->findData(data.value(QStringLiteral("slot"), QStringLiteral("armor")))));
        experience->setValue(data.value(QStringLiteral("experience"), 0).toInt());
        gold->setValue(data.value(QStringLiteral("gold"), 0).toInt());
        enemyGraphic->setText(data.value(QStringLiteral("graphicPath")).toString());
        enemyIds->setIds(variantIds(data.value(QStringLiteral("enemyIds"))));
        skillIds->setIds(variantIds(data.value(QStringLiteral("skillIds"))));
        lootId->setCurrentIndex(qMax(0, lootId->findData(data.value(QStringLiteral("lootId")))));
        lootChance->setValue(data.value(QStringLiteral("lootChance"), 0).toInt());
        encounterWeight->setValue(data.value(QStringLiteral("encounterWeight"), 10).toInt());
        stateDuration->setValue(data.value(QStringLiteral("duration"), 3).toInt());
        attackRate->setValue(data.value(QStringLiteral("attackRate"), 1.0).toDouble());
        defenseRate->setValue(data.value(QStringLiteral("defenseRate"), 1.0).toDouble());
        agilityRate->setValue(data.value(QStringLiteral("agilityRate"),1.0).toDouble());
        hpDamageRate->setValue(data.value(QStringLiteral("hpDamageRate"),0).toInt());
        animationFrames->setValue(data.value(QStringLiteral("frames"), 4).toInt());
    };
    auto reloadRecords = [=] {
        QSignalBlocker blocker(records);
        records->clear();
        const QVector<DatabaseRecord>& values = working->value(*category);
        for (const DatabaseRecord& record : values)
            records->addItem(QStringLiteral("%1: %2").arg(record.number, 3, 10, QLatin1Char('0'))
                                 .arg(record.name.isEmpty() ? tr("(sem nome)") : record.name));
        *current = values.isEmpty() ? -1 : qBound(0, *current, values.size() - 1);
        if (*current >= 0) records->setCurrentRow(*current);
        loadCurrent();
    };

    connect(cats, &QListWidget::currentRowChanged, this, [=](int row) {
        saveCurrent();
        if (row < 0) return;
        *category = cats->item(row)->data(Qt::UserRole).toString();
        *current = 0;
        rebuildRelations();
        showFields();
        reloadRecords();
    });
    connect(records, &QListWidget::currentRowChanged, this, [=](int row) {
        saveCurrent();
        *current = row;
        loadCurrent();
    });
    connect(icon, &QPushButton::clicked, this, [=, &ed] {
        if (*current < 0) return;
        DatabaseRecord& record = (*working)[*category][*current];
        record.icon = chooseInputIcon(ed, record.icon, this);
        loadCurrent();
    });
    connect(animationEditor, &QPushButton::clicked, this, [=, &ed] {
        if (*current < 0 || *category != QLatin1String("animations")) return;
        saveCurrent();
        DatabaseRecord& record = (*working)[*category][*current];
        RpgAnimationEditorDialog dialog(ed, record.data, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const QVariantMap data = dialog.animationData();
        for (auto it = data.cbegin(); it != data.cend(); ++it) record.data[it.key()] = it.value();
        loadCurrent();
    });
    connect(enemyAiEditor, &QPushButton::clicked, this, [=] {
        if (*current < 0 || *category != QLatin1String("enemies")) return;
        saveCurrent();
        DatabaseRecord& record = (*working)[*category][*current];
        EnemyAiEditorDialog dialog(working->value(QStringLiteral("skills")),
                                   working->value(QStringLiteral("states")),
                                   record.data.value(QStringLiteral("aiRules")).toList(), this);
        if (dialog.exec() != QDialog::Accepted) return;
        record.data[QStringLiteral("aiRules")] = dialog.rules();
    });
    connect(troopFormationEditor, &QPushButton::clicked, this, [=] {
        if (*current < 0 || *category != QLatin1String("troops")) return;
        saveCurrent();
        DatabaseRecord& record = (*working)[*category][*current];
        QVariantList formationData = record.data.value(QStringLiteral("enemySlots")).toList();
        if (formationData.isEmpty()) {
            const QStringList legacy = variantIds(record.data.value(QStringLiteral("enemyIds")));
            for (int i = 0; i < legacy.size(); ++i) {
                game::TroopEnemySlot slot; slot.enemyId = legacy[i];
                slot.x = legacy.size() <= 1 ? 0.5 : qreal(i + 1) / qreal(legacy.size() + 1); slot.y = 0.48;
                formationData.push_back(slot.toVariant());
            }
        }
        TroopFormationDialog dialog(working->value(QStringLiteral("enemies")), formationData, this);
        if (dialog.exec() != QDialog::Accepted) return;
        record.data[QStringLiteral("enemySlots")] = dialog.enemySlots();
        QStringList ids; for (const QVariant& value : dialog.enemySlots()) { const QString id=value.toMap().value(QStringLiteral("enemyId")).toString(); if(!id.isEmpty())ids.push_back(id); }
        record.data[QStringLiteral("enemyIds")] = ids;
        enemyIds->setIds(ids);
    });
    connect(troopEventsEditor, &QPushButton::clicked, this, [=, &ed] {
        if (*current < 0 || *category != QLatin1String("troops")) return;
        saveCurrent();
        DatabaseRecord& record = (*working)[*category][*current];
        TroopBattleEventsDialog dialog(ed, working->value(QStringLiteral("enemies")),
                                       working->value(QStringLiteral("actors")),
                                       working->value(QStringLiteral("states")),
                                       record.data.value(QStringLiteral("battleEvents")).toList(), this);
        if (dialog.exec() != QDialog::Accepted) return;
        record.data[QStringLiteral("battleEvents")] = dialog.events();
    });

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Adicionar"), this);
    auto* duplicate = new QPushButton(tr("Duplicar"), this);
    auto* remove = new QPushButton(tr("Excluir"), this);
    auto* logic = new QPushButton(tr("Interruptores e variáveis…"), this);
    auto* common = new QPushButton(tr("Eventos comuns…"), this);
    buttons->addWidget(add);
    buttons->addWidget(duplicate);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(logic);
    buttons->addWidget(common);
    root->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [=] {
        if (category->isEmpty()) return;
        saveCurrent();
        DatabaseRecord record;
        record.number = (*working)[*category].size() + 1;
        record.name = tr("Novo registro");
        record.data[QStringLiteral("maxLevel")] = 99;
        record.data[QStringLiteral("hp")] = 100;
        record.data[QStringLiteral("mp")] = 30;
        record.data[QStringLiteral("attack")] = 10;
        record.data[QStringLiteral("defense")] = 8;
        record.data[QStringLiteral("agility")] = 10;
        if (*category == QLatin1String("actors") && (*working)[*category].isEmpty())
            record.data[QStringLiteral("initialParty")] = true;
        (*working)[*category].push_back(record);
        *current = (*working)[*category].size() - 1;
        rebuildRelations();
        reloadRecords();
    });
    connect(duplicate, &QPushButton::clicked, this, [=] {
        if (*current < 0) return;
        saveCurrent();
        DatabaseRecord record = (*working)[*category][*current];
        record.id = idGen();
        record.number = (*working)[*category].size() + 1;
        record.name += tr(" (cópia)");
        (*working)[*category].push_back(record);
        *current = (*working)[*category].size() - 1;
        rebuildRelations();
        reloadRecords();
    });
    connect(remove, &QPushButton::clicked, this, [=] {
        if (*current < 0) return;
        (*working)[*category].remove(*current);
        for (int index = 0; index < (*working)[*category].size(); ++index)
            (*working)[*category][index].number = index + 1;
        *current = qMin(*current, (*working)[*category].size() - 1);
        rebuildRelations();
        reloadRecords();
    });
    connect(logic, &QPushButton::clicked, this, [&ed, this] { GameDataDialog(ed, this).exec(); });
    connect(common, &QPushButton::clicked, this, [&ed, this] { CommonEventsDialog(ed, this).exec(); });

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [=, &ed] {
        saveCurrent();
        ed.database = *working;
        ed.markDirty();
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    cats->setCurrentRow(0);
}


CustomDatabaseDialog::CustomDatabaseDialog(core::Editor& ed,QWidget* parent):QDialog(parent)
{
    setWindowTitle(tr("Banco de Dados Personalizado — LUDO"));
    resize(1120,720);
    auto working=std::make_shared<QVector<core::CustomDatabaseDefinition>>(ed.customDatabases);
    auto current=std::make_shared<int>(-1);

    auto* root=new QVBoxLayout(this);
    auto* intro=new QLabel(tr("Crie dados reutilizáveis para o seu jogo. Bancos <b>Somente leitura</b> mantêm os valores definidos no projeto; bancos <b>Editáveis durante o jogo</b> podem mudar por eventos e são salvos com a partida."),this);
    intro->setWordWrap(true);root->addWidget(intro);
    auto* split=new QSplitter(Qt::Horizontal,this);root->addWidget(split,1);

    auto* left=new QWidget(split);auto* leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,0,0);
    auto* databases=new QListWidget(left);leftLayout->addWidget(databases,1);
    auto* dbButtons=new QHBoxLayout;auto* addDb=new QPushButton(tr("Adicionar"),left);auto* removeDb=new QPushButton(tr("Excluir"),left);dbButtons->addWidget(addDb);dbButtons->addWidget(removeDb);leftLayout->addLayout(dbButtons);

    auto* right=new QWidget(split);auto* rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(0,0,0,0);
    auto* dbGroup=new QGroupBox(tr("Banco"),right);auto* dbForm=new QFormLayout(dbGroup);
    auto* dbName=new QLineEdit(dbGroup);auto* dbDescription=new QPlainTextEdit(dbGroup);dbDescription->setMaximumHeight(70);auto* dbMode=new QComboBox(dbGroup);dbMode->addItem(core::customDatabaseModeLabel(core::CustomDatabaseMode::ReadOnly),core::customDatabaseModeId(core::CustomDatabaseMode::ReadOnly));dbMode->addItem(core::customDatabaseModeLabel(core::CustomDatabaseMode::Runtime),core::customDatabaseModeId(core::CustomDatabaseMode::Runtime));
    auto* stableId=new QLabel(dbGroup);stableId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dbForm->addRow(tr("Nome"),dbName);dbForm->addRow(tr("Modo"),dbMode);dbForm->addRow(tr("Descrição"),dbDescription);dbForm->addRow(tr("ID estável"),stableId);rightLayout->addWidget(dbGroup);

    auto* tabs=new QTabWidget(right);rightLayout->addWidget(tabs,1);
    auto* fieldPage=new QWidget(tabs);auto* fieldLayout=new QVBoxLayout(fieldPage);auto* fields=new QTableWidget(fieldPage);fields->setColumnCount(4);fields->setHorizontalHeaderLabels({tr("Nome"),tr("Tipo"),tr("Referência"),tr("Padrão")});fields->horizontalHeader()->setStretchLastSection(true);fields->setSelectionBehavior(QAbstractItemView::SelectRows);fields->setSelectionMode(QAbstractItemView::SingleSelection);fields->setEditTriggers(QAbstractItemView::NoEditTriggers);fieldLayout->addWidget(fields,1);auto* fieldButtons=new QHBoxLayout;auto* addField=new QPushButton(tr("Adicionar campo…"),fieldPage);auto* editField=new QPushButton(tr("Editar…"),fieldPage);auto* removeField=new QPushButton(tr("Excluir"),fieldPage);fieldButtons->addWidget(addField);fieldButtons->addWidget(editField);fieldButtons->addWidget(removeField);fieldButtons->addStretch(1);fieldLayout->addLayout(fieldButtons);tabs->addTab(fieldPage,tr("Campos"));

    auto* recordPage=new QWidget(tabs);auto* recordLayout=new QVBoxLayout(recordPage);auto* records=new QListWidget(recordPage);recordLayout->addWidget(records,1);auto* recordButtons=new QHBoxLayout;auto* addRecord=new QPushButton(tr("Adicionar registro…"),recordPage);auto* editRecord=new QPushButton(tr("Editar valores…"),recordPage);auto* removeRecord=new QPushButton(tr("Excluir"),recordPage);recordButtons->addWidget(addRecord);recordButtons->addWidget(editRecord);recordButtons->addWidget(removeRecord);recordButtons->addStretch(1);recordLayout->addLayout(recordButtons);tabs->addTab(recordPage,tr("Registros"));
    split->setStretchFactor(0,1);split->setStretchFactor(1,3);

    const auto dbAt=[working,current]() -> core::CustomDatabaseDefinition* {return *current>=0&&*current<working->size()?&(*working)[*current]:nullptr;};
    auto saveMeta=[=]{if(auto* db=dbAt()){db->name=dbName->text();db->description=dbDescription->toPlainText();db->mode=core::customDatabaseModeFromId(dbMode->currentData().toString());core::normalizeCustomDatabaseDefinition(*db);}};
    auto databaseLabel=[](const core::CustomDatabaseDefinition& db){return QStringLiteral("%1 — %2 [%3]").arg(db.number).arg(db.name.isEmpty()?QObject::tr("(sem nome)"):db.name,core::customDatabaseModeLabel(db.mode));};
    auto reloadDbList=[=]{QSignalBlocker b(databases);databases->clear();for(const auto& db:*working)databases->addItem(databaseLabel(db));if(*current>=0&&*current<databases->count())databases->setCurrentRow(*current);};
    auto reloadFields=[=]{fields->setRowCount(0);const auto* db=dbAt();if(!db)return;for(const auto& field:db->fields){const int row=fields->rowCount();fields->insertRow(row);fields->setItem(row,0,new QTableWidgetItem(field.name));fields->setItem(row,1,new QTableWidgetItem(core::customDatabaseFieldTypeLabel(field.type)));QString ref;if(field.type==core::CustomDatabaseFieldType::RecordReference){if(const auto* target=core::customDatabaseById(*working,field.referenceDatabaseId))ref=target->name.isEmpty()?target->id:target->name;else ref=tr("(banco ausente)");}fields->setItem(row,2,new QTableWidgetItem(ref));QString def=field.type==core::CustomDatabaseFieldType::Boolean?(field.defaultValue.toBool()?tr("Ligado"):tr("Desligado")):field.defaultValue.toString();if(field.type==core::CustomDatabaseFieldType::RecordReference&&!def.isEmpty())if(const auto* target=core::customDatabaseById(*working,field.referenceDatabaseId))if(const auto* rr=core::customDatabaseRecordById(*target,def))def=rr->name.isEmpty()?rr->id:rr->name;fields->setItem(row,3,new QTableWidgetItem(def));}};
    auto reloadRecords=[=]{QSignalBlocker b(records);records->clear();const auto* db=dbAt();if(!db)return;for(const auto& record:db->records){auto* item=new QListWidgetItem(QStringLiteral("%1 — %2").arg(record.number).arg(record.name.isEmpty()?tr("(sem nome)"):record.name),records);item->setData(Qt::UserRole,record.id);}};
    auto loadCurrent=[=]{const auto* db=dbAt();const bool on=db;dbGroup->setEnabled(on);tabs->setEnabled(on);removeDb->setEnabled(on);if(!db){dbName->clear();dbDescription->clear();stableId->clear();fields->setRowCount(0);records->clear();return;}dbName->setText(db->name);dbDescription->setPlainText(db->description);dbMode->setCurrentIndex(qMax(0,dbMode->findData(core::customDatabaseModeId(db->mode))));stableId->setText(db->id);reloadFields();reloadRecords();};

    auto editFieldDialog=[=](int index){auto* db=dbAt();if(!db)return;if(index<-1||index>=db->fields.size())return;core::CustomDatabaseField field=index>=0?db->fields.at(index):core::CustomDatabaseField{};if(index<0)field.name=tr("Novo campo");QDialog dialog(this);dialog.setWindowTitle(index<0?tr("Adicionar campo"):tr("Editar campo"));auto* v=new QVBoxLayout(&dialog);auto* form=new QFormLayout;v->addLayout(form);auto* name=new QLineEdit(field.name,&dialog);auto* type=new QComboBox(&dialog);for(auto t:{core::CustomDatabaseFieldType::Number,core::CustomDatabaseFieldType::Boolean,core::CustomDatabaseFieldType::Text,core::CustomDatabaseFieldType::RecordReference})type->addItem(core::customDatabaseFieldTypeLabel(t),core::customDatabaseFieldTypeId(t));type->setCurrentIndex(qMax(0,type->findData(core::customDatabaseFieldTypeId(field.type))));auto* refDb=new QComboBox(&dialog);for(const auto& candidate:*working)refDb->addItem(candidate.name.isEmpty()?candidate.id:candidate.name,candidate.id);refDb->setCurrentIndex(qMax(0,refDb->findData(field.referenceDatabaseId)));auto* defaults=new QStackedWidget(&dialog);auto* number=new QSpinBox(defaults);number->setRange(-999999999,999999999);number->setValue(field.defaultValue.toInt());auto* boolean=new QComboBox(defaults);boolean->addItem(tr("Desligado"),false);boolean->addItem(tr("Ligado"),true);boolean->setCurrentIndex(field.defaultValue.toBool()?1:0);auto* text=new QLineEdit(field.defaultValue.toString(),defaults);auto* refRecord=new QComboBox(defaults);defaults->addWidget(number);defaults->addWidget(boolean);defaults->addWidget(text);defaults->addWidget(refRecord);form->addRow(tr("Nome"),name);form->addRow(tr("Tipo"),type);form->addRow(tr("Banco referenciado"),refDb);form->addRow(tr("Valor padrão"),defaults);auto rebuildRef=[=]{const QString targetId=refDb->currentData().toString();const QString keep=refRecord->currentData().toString().isEmpty()?field.defaultValue.toString():refRecord->currentData().toString();QSignalBlocker b(refRecord);refRecord->clear();refRecord->addItem(tr("(nenhum)"),QString());if(const auto* target=core::customDatabaseById(*working,targetId))for(const auto& r:target->records)refRecord->addItem(r.name.isEmpty()?tr("Registro %1").arg(r.number):r.name,r.id);const int i=refRecord->findData(keep);if(i>=0)refRecord->setCurrentIndex(i);};auto syncType=[=]{const auto t=core::customDatabaseFieldTypeFromId(type->currentData().toString());const int page=t==core::CustomDatabaseFieldType::Number?0:t==core::CustomDatabaseFieldType::Boolean?1:t==core::CustomDatabaseFieldType::Text?2:3;defaults->setCurrentIndex(page);refDb->setVisible(t==core::CustomDatabaseFieldType::RecordReference);if(t==core::CustomDatabaseFieldType::RecordReference)rebuildRef();};connect(type,&QComboBox::currentIndexChanged,&dialog,[=](int){syncType();});connect(refDb,&QComboBox::currentIndexChanged,&dialog,[=](int){rebuildRef();});syncType();auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);v->addWidget(box);connect(box,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;field.name=name->text().trimmed();field.type=core::customDatabaseFieldTypeFromId(type->currentData().toString());field.referenceDatabaseId=field.type==core::CustomDatabaseFieldType::RecordReference?refDb->currentData().toString():QString();if(field.type==core::CustomDatabaseFieldType::Number)field.defaultValue=number->value();else if(field.type==core::CustomDatabaseFieldType::Boolean)field.defaultValue=boolean->currentData().toBool();else if(field.type==core::CustomDatabaseFieldType::Text)field.defaultValue=text->text();else field.defaultValue=refRecord->currentData().toString();if(index<0)db->fields.push_back(field);else db->fields[index]=field;core::normalizeCustomDatabaseDefinition(*db);reloadFields();reloadRecords();};

    auto editRecordDialog=[=](int index){auto* db=dbAt();if(!db)return;if(index<-1||index>=db->records.size())return;core::CustomDatabaseRecord record=index>=0?db->records.at(index):core::CustomDatabaseRecord{};if(index<0){int nextNumber=1;for(const auto& existing:db->records)nextNumber=qMax(nextNumber,existing.number+1);record.number=nextNumber;record.name=tr("Novo registro");}QDialog dialog(this);dialog.setWindowTitle(index<0?tr("Adicionar registro"):tr("Editar registro"));dialog.resize(650,600);auto* v=new QVBoxLayout(&dialog);auto* topForm=new QFormLayout;auto* number=new QSpinBox(&dialog);number->setRange(1,999999);number->setValue(record.number);auto* name=new QLineEdit(record.name,&dialog);auto* desc=new QPlainTextEdit(record.description,&dialog);desc->setMaximumHeight(70);topForm->addRow(tr("Número"),number);topForm->addRow(tr("Nome"),name);topForm->addRow(tr("Descrição"),desc);v->addLayout(topForm);auto* scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);auto* content=new QWidget(scroll);auto* valuesForm=new QFormLayout(content);scroll->setWidget(content);v->addWidget(scroll,1);QVector<std::function<QVariant()>> getters;for(const auto& field:db->fields){const QVariant effective=record.values.contains(field.id)?record.values.value(field.id):field.defaultValue;if(field.type==core::CustomDatabaseFieldType::Number){auto* w=new QSpinBox(content);w->setRange(-999999999,999999999);w->setValue(effective.toInt());valuesForm->addRow(field.name,w);getters.push_back([w]{return QVariant(w->value());});}else if(field.type==core::CustomDatabaseFieldType::Boolean){auto* w=new QCheckBox(tr("Ligado"),content);w->setChecked(effective.toBool());valuesForm->addRow(field.name,w);getters.push_back([w]{return QVariant(w->isChecked());});}else if(field.type==core::CustomDatabaseFieldType::Text){auto* w=new QLineEdit(effective.toString(),content);valuesForm->addRow(field.name,w);getters.push_back([w]{return QVariant(w->text());});}else{auto* w=new QComboBox(content);w->addItem(tr("(nenhum)"),QString());if(const auto* target=core::customDatabaseById(*working,field.referenceDatabaseId))for(const auto& r:target->records)w->addItem(r.name.isEmpty()?tr("Registro %1").arg(r.number):r.name,r.id);const int ri=w->findData(effective.toString());if(ri>=0)w->setCurrentIndex(ri);valuesForm->addRow(field.name,w);getters.push_back([w]{return w->currentData();});}}if(db->fields.isEmpty())valuesForm->addRow(new QLabel(tr("Este banco ainda não possui campos."),content));auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);v->addWidget(box);connect(box,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;record.number=number->value();record.name=name->text().trimmed();record.description=desc->toPlainText();for(int i=0;i<db->fields.size()&&i<getters.size();++i)record.values[db->fields[i].id]=getters[i]();if(index<0)db->records.push_back(record);else db->records[index]=record;core::normalizeCustomDatabaseDefinition(*db);reloadRecords();reloadFields();};

    connect(databases,&QListWidget::currentRowChanged,this,[=](int row){saveMeta();*current=row;loadCurrent();});
    connect(dbName,&QLineEdit::textEdited,this,[=](const QString&){if(auto* db=dbAt()){db->name=dbName->text();reloadDbList();}});
    connect(dbMode,&QComboBox::currentIndexChanged,this,[=](int){if(auto* db=dbAt()){db->mode=core::customDatabaseModeFromId(dbMode->currentData().toString());reloadDbList();}});
    connect(addDb,&QPushButton::clicked,this,[=]{saveMeta();core::CustomDatabaseDefinition db;int nextNumber=1;for(const auto& existing:*working)nextNumber=qMax(nextNumber,existing.number+1);db.number=nextNumber;db.name=tr("Novo banco");working->push_back(db);*current=working->size()-1;reloadDbList();loadCurrent();});
    connect(removeDb,&QPushButton::clicked,this,[=,&ed]{
        if(!dbAt())return;const QString id=dbAt()->id;bool referenced=false;for(const auto& db:*working)for(const auto& field:db.fields)if(field.type==core::CustomDatabaseFieldType::RecordReference&&field.referenceDatabaseId==id){referenced=true;break;}
        const int semanticUses=core::findProjectUses(ed,core::ReferenceSymbolKind::CustomDatabase,id).size();
        QString warning;if(referenced)warning+=tr("Existem campos de referência apontando para este banco.\n");if(semanticUses>0)warning+=tr("A Busca Global encontrou usos deste banco no projeto: %1.\n").arg(semanticUses);
        if(!warning.isEmpty()&&QMessageBox::warning(this,tr("Excluir banco"),warning+tr("\nExcluir mesmo assim?"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        working->removeAt(*current);*current=qMin(*current,working->size()-1);reloadDbList();loadCurrent();
    });
    connect(addField,&QPushButton::clicked,this,[=]{editFieldDialog(-1);});connect(editField,&QPushButton::clicked,this,[=]{editFieldDialog(fields->currentRow());});connect(fields,&QTableWidget::cellDoubleClicked,this,[=](int row,int){editFieldDialog(row);});connect(removeField,&QPushButton::clicked,this,[=,&ed]{
        auto* db=dbAt();const int row=fields->currentRow();if(!db||row<0||row>=db->fields.size())return;const auto field=db->fields.at(row);const int uses=core::findProjectUses(ed,core::ReferenceSymbolKind::CustomField,field.id).size();
        if(uses>0&&QMessageBox::warning(this,tr("Excluir campo"),tr("O campo “%1” ainda é usado no projeto. Usos encontrados: %2. Excluir mesmo assim?").arg(field.name).arg(uses),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        db->fields.removeAt(row);core::normalizeCustomDatabaseDefinition(*db);reloadFields();reloadRecords();
    });
    connect(addRecord,&QPushButton::clicked,this,[=]{editRecordDialog(-1);});connect(editRecord,&QPushButton::clicked,this,[=]{editRecordDialog(records->currentRow());});connect(records,&QListWidget::itemDoubleClicked,this,[=](QListWidgetItem*){editRecordDialog(records->currentRow());});connect(removeRecord,&QPushButton::clicked,this,[=,&ed]{
        auto* db=dbAt();const int row=records->currentRow();if(!db||row<0||row>=db->records.size())return;const auto record=db->records.at(row);const QString removedId=record.id;const int uses=core::findProjectUses(ed,core::ReferenceSymbolKind::CustomRecord,removedId).size();
        if(uses>0&&QMessageBox::warning(this,tr("Excluir registro"),tr("O registro “%1” ainda é usado no projeto. Usos encontrados: %2. Excluir mesmo assim?").arg(record.name).arg(uses),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        db->records.removeAt(row);for(auto& sourceDb:*working)for(auto& rec:sourceDb.records)for(const auto& field:sourceDb.fields)if(field.type==core::CustomDatabaseFieldType::RecordReference&&field.referenceDatabaseId==db->id&&rec.values.value(field.id).toString()==removedId)rec.values[field.id]=QString();reloadRecords();reloadFields();
    });

    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);root->addWidget(box);connect(box,&QDialogButtonBox::accepted,this,[=,&ed]{saveMeta();for(auto& db:*working)core::normalizeCustomDatabaseDefinition(db);ed.customDatabases=*working;ed.markDirty();accept();});connect(box,&QDialogButtonBox::rejected,this,&QDialog::reject);
    reloadDbList();if(!working->isEmpty()){*current=0;databases->setCurrentRow(0);}loadCurrent();
}

LudoHelpDialog::LudoHelpDialog(QWidget* parent):QDialog(parent)
{
    setWindowTitle(tr("Como usar os Ludo Systems"));resize(820,620);auto* v=new QVBoxLayout(this);auto* tabs=new QTabWidget(this);
    struct Page{QString title,html,code;};const QVector<Page> pages={
      {tr("Camera"),tr("<h2>Ludo Camera System</h2><p>Use os comandos visuais ou escreva a tag em um <b>Comentário</b> comum do evento. Tags Ludo em comentários são detectadas automaticamente quando a página fica ativa, sem Autorun.</p><pre>&lt;LudoCamera alvo=player zoom=2 duracao=60 suavidade=6 zona=16&gt;</pre>"),QStringLiteral("<LudoCamera alvo=player zoom=2 duracao=60 suavidade=6 zona=16>")},
      {tr("Sprites"),tr("<h2>Ludo Sprite Effects</h2><p><b>visibleNear:</b> aparece perto. <b>visibleFar:</b> aparece longe.</p><pre>&lt;LudoFade alvo=self opacidade=0 duracao=60&gt;\n&lt;LudoPhantom modo=visibleNear perto=1 longe=8 minimo=24 maximo=255&gt;</pre>"),QStringLiteral("<LudoFade alvo=self opacidade=0 duracao=60>\n<LudoPhantom modo=visibleNear perto=1 longe=8 minimo=24 maximo=255>")},
      {tr("Cutscenes"),tr("<h2>Ludo Cutscene Skip</h2><p>Coloque Início no começo e Fim no ponto em que a região deixa de ser pulável. Ao pular, esperas e efeitos temporais são acelerados, mas switches, variáveis e demais efeitos lógicos dentro da região continuam executando. Choices, inputs e modais são barreiras e permanecem interativos. Regiões podem ser aninhadas; o primeiro skip fecha somente a mais interna.</p><pre>&lt;LudoCutscene inicio&gt;\n... cena ...\n&lt;LudoCutscene fim&gt;</pre>"),QStringLiteral("<LudoCutscene inicio>\n<LudoCutscene fim>")},
      {tr("Input"),tr("<h2>Ludo Input System</h2><p>Escolha no IconSet um ícone de teclado e um de controle genérico por ação. RC2.46 também expõe Ação de jogo 1–4, bordas Pressed/Released, duração, analógicos, gatilhos e Esperar ação pelo Value Resolver.</p><pre>\\KEY[confirm]\n\\KEY[action1]\n\\KEY[action2]\n\\KEY[skipCutscene]</pre>"),QStringLiteral("\\KEY[confirm]\n\\KEY[action1]\n\\KEY[action2]\n\\KEY[skipCutscene]")},
      {tr("Novidades"),tr("<h2>Notas de Atualização</h2><ul>"
          "<li><b>LUDO 4.0.0 RC2.85.4 — Scanlines + Gaussian Blur:</b> varredura branca nasce fora da tela e respeita Delay real com relógio por slot; Blur H/V/Completo e Tilt-Shift usam kernel Gaussiano real no runtime e no Preview.</li>"
          "<li><b>LUDO 4.0.0 RC2.85.3 — World Filter Composite Sampling Hotfix:</b> corrige linhas RGB periódicas da Aberração Cromática no mapa; Aberração, Blur e Tilt-Shift de World passam a amostrar a WorldScene já composta em vez de cada tile/UV do atlas, sem novo framebuffer/pass.</li>"
          "<li><b>LUDO 4.0.0 RC2.85.2 — Event Command Side Panel Correction:</b> a Lista do Evento fica sempre visível e a lateral direita passa a ter somente Inspector | Comandos; Comandos usa categoria em ComboBox, botões verticais, scrollbar e o mesmo catálogo para built-ins e plugins. Preview volta a ser sob demanda.</li>"
          "<li><b>LUDO 4.0.0 RC2.85.1 — Qt 6.8 QMap Reverse Iterator Build Hotfix:</b> corrige a compilação do Filter System no Qt 6.8.3 substituindo QMap::crbegin/crend por travessia reversa compatível, sem alterar slots, filtros ou formatos.</li>"
          "<li><b>LUDO 4.0.0 RC2.85 — Ludo Filter System v3 + Direct Map Startup:</b> slots 1–99 por filtro, Aberração Normal/Lente, Scanlines com Delay, Vignette, Blur e Tilt-Shift; Player abre direto no mapa com fade-in e o Event Editor inicia a reorganização lateral, consolidada corretamente na RC2.85.2.</li>"
          "<li><b>LUDO 4.0.0 RC2.84.1 — Filter Diagnostics Contract Hotfix:</b> sincroniza o CTest de Runtime Diagnostics com o contrato v2 do stack de filtros sem alterar runtime, shaders ou Autotile Tools.</li>"
          "<li><b>LUDO 4.0.0 RC2.84 — Ludo Filter System Bloco 2 + Autotile Tool Integration:</b> filtros passam a usar escopo World/Pictures/HUD (World por padrão), Aberração Cromática radial de lente, Noise e Scanlines de TV; seleção por botão direito e ferramentas de mapa passam a preservar Autotiles/Wang.</li>"
          "<li><b>LUDO 4.0.0 RC2.83.5 — Final CTest Regression Hotfix:</b> fecha os dois falsos negativos restantes do CTest (LudoPhantom automatic=false sem contexto self e comparação UTF-8 com QLatin1String) e endurece literais Unicode; filtros/runtime permanecem inalterados.</li>"
          "<li><b>LUDO 4.0.0 RC2.83.4 — Runtime Regression Hotfix:</b> corrige o detector de expressões, round-trip UTF-8 de presets, deduplicação semântica de ValueSpecs e atualiza regressões históricas/GUI da suíte crítica; o Ludo Filter System permanece inalterado.</li>"
          "<li><b>LUDO 4.0.0 RC2.83.3 — Critical Test Suite Build Hotfix:</b> corrige dois erros expostos pelo MinGW na suíte crítica: fechamento excedente em choice.show e colisão de namespace ::ui/game::ui, sem alterar runtime ou filtros.</li>"
          "<li><b>LUDO 4.0.0 RC2.83.2 — Plugin Contract Test Build Hotfix:</b> corrige três fechamentos excedentes em inicializadores agregados do teste crítico Plugin Contract 2.0 encontrados pelo MinGW, sem alterar runtime ou filtros.</li>"
          "<li><b>LUDO 4.0.0 RC2.83.1 — Windows Batch CRLF Hotfix:</b> normalização binária automática de scripts .bat antes dos gates de release, preservando conteúdo/encoding e corrigindo LF vindo de ZIP ou edição cruzada.</li>"
          "<li><b>LUDO 4.0.0 RC2.83 — Ludo Filter System / Bloco 1:</b> novo stack de pós-processamento com Aberração Cromática, transição, Save/Load, Preview/Debugger e caminho QRhi sem framebuffer ou passe extra.</li>"
          "<li><b>LUDO 4.0.0 RC2.82.1 — Preview Visibility Hotfix:</b> a aba Preview só aparece para comandos que realmente possuem provider visual; comandos lógicos não exibem mais fallback técnico.</li>"
          "<li><b>LUDO 4.0.0 RC2.82 — Event Command System 2.0 Final Audit:</b> fecha os Blocos 39–40 com rotas executáveis por schema, battle.start funcional, no-ops legados bloqueados e auditoria de parâmetros Editor/runtime.</li>"
          "<li><b>LUDO 4.0.0 RC2.81 — Plugin Contract 2.0 + Command Parity:</b> .ludoplugin v2 compatível com v1, argumentos/dependências resolvidos pelo mesmo contrato no Editor/Validator/runtime e paridade automática Registry/Catalog, incluindo repeat.break.</li>"
          "<li><b>LUDO 4.0.0 RC2.80.1 — Live Inspector Namespace Build Hotfix:</b> F8/F9 com cutscene/awaitables/paralelos, watches ao vivo, breakpoints condicionais e trace filtrável, exportável e reproduzível.</li>"
          "<li><b>LUDO 4.0.0 RC2.79.2 — Navigation Hotfix:</b> centraliza a navegação do Inspector no MainWindow e remove o fallback inválido de GlobalSearchDialog em Dialogs.cpp.</li>"
          "<li><b>LUDO 4.0.0 RC2.79.1 — Header Hotfix:</b> restaura a compilação independente de Dialogs.h ao declarar ProjectReferenceLocation antes do CommandListWidget.</li>"
          "<li><b>LUDO 4.0.0 RC2.79 — Command Inspector + Preview Framework 2.0:</b> Inspector lateral ligado a Registry/Validator/Reference Index, navegação F12/Ctrl+Click com histórico e previews comuns de Camera, Fog, Cutscene, Picture, Tela e Clima.</li>"
          "<li><b>LUDO 4.0.0 RC2.78 — Compositor No-Code + Índice de Referências 2.0:</b> criação visual de comandos declarativos com preview da expansão segura e Busca Global para Pictures, Speakers, regiões, tracks, templates, órfãos, não usados e ciclos.</li>"
          "<li><b>LUDO 4.0.0 RC2.77 — Favoritos, Presets + Fluxos reutilizáveis:</b> favoritos/recentes, presets JSON, templates parametrizados e extração estrutural para Evento Comum integrada ao ProjectIO/Validator/Reference Index.</li>"
          "<li><b>LUDO 4.0.0 RC2.76.2 — QVariant Boolean Build Hotfix:</b> corrige as leituras de collapse para a API válida do Qt 6 sem alterar gameplay ou formatos.</li>"
          "<li><b>LUDO 4.0.0 RC2.76.1 — QFontMetrics Build Hotfix:</b> corrige a compilação Qt 6/MinGW do overflow shrink de mensagens sem alterar gameplay ou formatos.</li>"
          "<li><b>LUDO 4.0.0 RC2.76 — Preview Narrativo + Editor estrutural:</b> preview animado de mensagens, legendas e escolhas; grupos persistentes, collapse, breadcrumb, minimap e drag-and-drop validado.</li>"
          "<li><b>LUDO 4.0.0 RC2.75 — Histórico + Diálogo localizado:</b> histórico persistente e filtrável, avanço rápido/skip seguro, localização inline, overflow configurável e grafemas Unicode.</li>"
          "<li><b>LUDO 4.0.0 RC2.74 — Speech Bubbles + Choices 2.0:</b> balões ancorados no mundo, notificações em tela e escolhas com condições reavaliadas, motivo de bloqueio, timer e opção automática.</li>"
          "<li><b>LUDO 4.0.0 RC2.73 — Portrait/Voice + Subtitle System 2.0:</b> expressões e retratos por personagem, fila FIFO de voz localizada e legendas com tracks, filas, estilos e duração automática; editor, player, Validator e persistência usam os mesmos contratos.</li>"
          "<li><b>LUDO 4.0.0 RC2.72 — Picture Animation + Dialogue Content:</b> timelines/keyframes e clique/toque de Pictures usam o runtime real; Speaker Database persistente aplica perfil, fonte, cores, expressão, voz e presets às mensagens.</li>"
          "<li><b>LUDO 4.0.0 RC2.71.1 — PictureManager Header Hotfix:</b> corrige o contrato de compilação de refreshBindings sem alterar gameplay ou formatos.</li>"
          "<li><b>LUDO 4.0.0 RC2.71 — Picture System 2.0 + Picture World:</b> nomes lógicos, grupos, valores dinâmicos e attachments a Player/Eventos/Pictures, com Save/Load e Validator integrados.</li>"
          "<li><b>LUDO 4.0.0 RC2.70 — Condition Builder + Expressões:</b> edição visual AND/OR com preview e limite de profundidade; expressões tipadas e Game Values universais no mesmo Value Resolver do runtime.</li><li><b>LUDO 4.0.0 RC2.69 — Validator e Diagnósticos 2.0:</b> valida contexto, recursão e referências de eventos; adiciona painel Problemas navegável com filtros, correções seguras, exportação e marcações no editor.</li><li><b>LUDO 4.0.0 RC2.68.3 — Picture Bilinear:</b> suavização de Pictures explicitamente Bilinear; desligada usa Nearest para pixel art. Sem mudança de formatos.</li><li><b>LUDO 4.0.0 RC2.68.2 — Negative Hotfix:</b> corrige Pictures e Picture Text com cores invertidas quando Negative estava desligado; sem mudança de formatos.</li><li><b>LUDO 4.0.0 RC2.68.1 — Build Hotfix:</b> corrige a leitura Qt 6 de halfTileVerticalOffset em ProjectIO; sem mudança de gameplay ou formatos.</li><li><b>LUDO 4.0.0 RC2.68 — Stabilization:</b> unifica Pictures ao Asset Picker, corrige smooth QRhi, reorganiza comandos, melhora retorno/half-tile de eventos, texto, Picture FX/Negative/Tonalidade, passos espaciais e fluxo de tilesets/Terrain.</li><li><b>LUDO 4.0.0 RC2.67.1 — Hotfix de Build Windows:</b> normaliza scripts .bat para CRLF e corrige rótulos do pipeline de shaders no cmd.exe, sem alterar formatos ou gameplay.</li><li><b>LUDO 4.0.0 RC2.67 — Bloco P / Consolidação e Paridade:</b> fecha o ciclo RC2.61–RC2.66 com teste nativo cruzado, paridade exata Editor/F5/F6/Player/Export, Save/Load, Asset Metadata, UI Theme, Event Codec e checklist clean-PC atualizado.</li>"
          "<li><b>LUDO 4.0.0 RC2.66 — Blocos N+O:</b> Picture Layers 0–9 usam âncoras reais CPU/QRhi; Screen Tone respeita affectedByTone; Interface In-Game ganha .ludotheme e refresh seguro; Map/Common compartilham clipboard/codec e Manter posição restaura o NPC.</li>"
          "<li><b>LUDO 4.0.0 RC2.64.1 — Sprite Selector:</b> Characters deixam de presumir um layout único; detecção com confiança, seleção visual e metadata spriteLayout por GUID preservam a configuração após mover/renomear o asset.</li>"
          "<li><b>LUDO 4.0.0 RC2.64 — Bloco M:</b> Choices ganham alinhamento Esquerda/Centro/Direita; outline/sombra usam o glyph real; previews de comandos abrem sob demanda pelo botão Ver prévia.</li>"
          "<li><b>LUDO 4.0.0 RC2.63 — Bloco L:</b> Sprite/Personagem e BGM/BGS/ME/SE/Voice usam o Universal Asset Picker; áudio referenciado entra no preload/cache real antes do gameplay.</li>"
          "<li><b>LUDO 4.0.0 RC2.62.1:</b> hotfix de compilação Windows do Universal Asset Picker, sem alteração de formato ou recurso.</li>"
          "<li><b>LUDO 4.0.0 RC2.62 — Bloco K:</b> Universal Asset Picker com categoria contextual, DropDown, pesquisa, subpastas, preview e seleção de assets existentes sem reimportação.</li>"
          "<li><b>LUDO 4.0.0 RC2.61 — Bloco J:</b> Asset Workflow Foundation padroniza pastas, categorias, Voice, descoberta de assets e sincronização com o Asset Database.</li>"
          "<li><b>LUDO 4.0.0 RC2.60 — Bloco I:</b> consolidação A–H com ProjectIO → Validator → Runtime Snapshot → Player/Export e Game State.</li>"
          "<li><b>LUDO 4.0.0 RC2.59 — Bloco H:</b> Map Workflow + Game State centralizam duplicação/reparenting de mapas, Novo Jogo/Load/Restart e políticas de transição.</li>"
          "<li><b>LUDO 4.0.0 RC2.54 — Event Editor 2.0 / Bloco C:</b> navegador lateral de Map Events usa Model/View direto no MapDoc e IDs estáveis; a escolha de comandos volta a uma lista única pesquisável, preservando Registry/Catalog/Validator/Interpreter.</li>"
          "<li><b>LUDO 4.0.0 RC2.53.1 — Qt 6.8 QRhi Build Contract Hotfix:</b> QShader usa a fronteira oficial rhi/GuiPrivate no alvo tes_game, corrigindo o build MinGW sem alterar o pipeline de preload.</li>"
          "<li><b>LUDO 4.0.0 RC2.53 — Playtest Preload Pipeline / Bloco B:</b> F5/F6 preparam o Runtime Snapshot com barra de trabalho real, assets referenciados, caches derivados de mapa e QShader; caches são transientes, validados por digest e invalidados no Hot Reload.</li>"
          "<li><b>LUDO 4.0.0 RC2.52 — Editor Stability Foundation / Bloco A:</b> árvore de Camadas transacional por IDs estáveis, seleção estrutural separada do alvo de pintura, ProjectIO/Undo-Redo/Validator cycle-safe e Janela de Evento redimensionável/restaurada dentro da área útil do monitor.</li>"
          "<li><b>LUDO 4.0.0 RC2.51.2 — Footstep Half-Step Cadence Hotfix:</b> dois movimentos de 0,5 tile compartilham o mesmo stride e geram um único footstep por tile percorrido, com persistência em Save/Load.</li>"
          "<li><b>LUDO 4.0.0 RC2.51.1 — Footstep Runtime Snapshot Hotfix:</b> superfícies/configurações de Footstep atravessam F5/F6/Hot Reload/Player e mantêm assetReferences em paridade.</li>"
          "<li><b>LUDO 4.0.0 RC2.51 — Footstep System:</b> superfícies por ID estável, resolução centralizada, Conexões do Autotile/fallback, jogador/eventos, Common Events, Asset Database, export e .ludocommon.</li>"
          "<li><b>LUDO 4.0.0 RC2.50 — Bloco O / Consolidação Final:</b> nenhuma feature nova; A–M foram auditados de ponta a ponta e o gate de release agora combina contratos estáticos, CTest RC2, paridade F5/F6/Player, migrações e CI GPU-first.</li>"
          "<li><b>LUDO 4.0.0 RC2.49.1 — Build Hotfix:</b> corrige as capturas do Editor nas ações Excluir banco/campo/registro do Custom Database para compatibilidade Qt 6.8.3/MinGW, preservando Busca Global e Encontrar Usos.</li>"
          "<li><b>LUDO 4.0.0 RC2.49 — Debugger 2.0 + Hot Reload + Fluxo Avançado:</b> Step Into/Over/Out, call stack por IDs estáveis, Hot Reload preservando GameState, Repetir N vezes, Chamar Map Event, Reservar Common Event no Scheduler e Return/Exit por escopo, todos integrados a Save/Load, Validator e Player/Export.</li>"
          "<li><b>LUDO 4.0.0 RC2.48 — Eventos Comuns portáteis + Busca Global:</b> .ludocommon inclui dependências transitivas e assets verificados; common.call usa ID estável; Encontrar Usos e refatoração segura protegem referências por ID.</li>"
          "<li>Tonalidade da Tela igual ao editores de RPG, agora com preview em tempo real.</li>"
          "<li>Pictures com spritesheet, frame específico, FPS, loop e flip horizontal/vertical.</li>"
          "<li>Tint Image, delay no Blink e Glow com pulso.</li>"
          "<li>Configurações de Exibição com camadas 0 a 9 e previews ampliados.</li>"
          "<li>Múltiplos panoramas/parallax com scroll independente, spritesheet e efeitos de Pictures.</li>"
          "<li>Autosave de recuperação e log diário do editor.</li>"
          "<li>Exportação com limpeza de assets e contêiner criptografado LUDOCRYPT2.</li>"
          "<li>Empacotamento do LudoPlayer reforçado para evitar erro de DLL Qt ausente.</li>"
          "<li>Refatoração incremental Fase 2: efeitos compartilhados entre Pictures e Panoramas, estado visual modular e previews centralizados.</li>"
          "<li>Refatoração incremental Fase 3: Command Registry, Interpreter modular, runtime CPU/GPU compartilhado e GameSession dividido.</li>"
          "<li>Refatoração incremental Fase 4: EditorSession separado, Resource Manager reativo, F5/F6 em memória e Undo do inspector.</li>"
          "<li>In-Game UI Fase 1: canvas próprio, mensagens/choices renderizadas pela engine e base de Window Skin 9-slice.</li>"
          "<li><b>LUDO 4.0.0 RC2.47 — Bloco I:</b> Runtime Map Management adiciona tiles/áreas/passagem/Terrain/tileset mutáveis com Save/Load, Value Resolver, render CPU/QRhi, colisão e Debugger integrados.</li><li><b>LUDO 4.0.0 RC2.46 — Blocos G+H:</b> Common Events ganham condição avançada pelo Value Resolver e scheduler Enquanto verdadeiro/Ao ficar verdadeiro/Intervalo; Input ganha ações livres, Held/Pressed/Released, duração, Esperar ação e valores analógicos/mouse.</li>"
          "<li><b>LUDO 4.0.0 RC2.45.1 — Build Hotfix E+F:</b> corrige a compilação das lambdas do editor de DB e limpa referências incrementais residuais do RC2.44 no Player, sem alterar formatos ou gameplay.</li>"
          "<li><b>LUDO 4.0.0 RC2.45 — Custom Database / Blocos E+F:</b> bancos No-Code tipados com IDs estáveis, modos Somente leitura/Runtime, consultas pelo Value Resolver, Save/Load, Validator, F8 Debugger e comandos de gerenciamento sem Type/Data/Field numéricos.</li>"
          "<li><b>LUDO 4.0.0 RC2.43.1 — Value System / Blocos B+C+D:</b> Strings Globais, Valores do Jogo, matemática avançada e Expressão Visual compartilham um único Value Resolver tipado com Save/Load, debugger, Validator e entrada de texto in-game.</li>"
          "<li><b>LUDO 4.0.0 RC2.42 — Common Events como Funções No-Code:</b> parâmetros tipados, variáveis locais, retorno, chamadas aninhadas e validação integrada formam a base do Bloco A.</li>"
          "<li><b>LUDO 4.0.0 RC2.41 — Arquitetura Clássica:</b> Common Events tornam-se o centro da lógica No-Code; UI Designer e .ludoui saem da engine, preservando as interfaces nativas.</li>"
          "<li><b>LUDO 4.0.0 RC2.40 — UI Designer 2.23:</b> última versão histórica do antigo designer visual.</li>"
          "<li><b>LUDO 4.0.0 RC2.39 — UI Designer 2.22:</b> modos Guiado/Avançado, bibliotecas e Inspector por abas, Timeline sob demanda e comportamentos No-Code rápidos preservam o mesmo documento e runtime.</li>"
          "<li><b>LUDO 4.0.0 RC2.38.1 — Localização:</b> novos textos recebem chaves curtas; Renomear chave atualiza todas as referências com segurança.</li>"
          "<li><b>LUDO 4.0.0 RC2.38 — Runtime GPU-first:</b> QRhi é o único runtime; D3D11/Vulkan têm fallback seguro, frame visual único, shaders validados e texturas ausentes usam placeholder.</li>"
          "<li><b>LUDO 4.0.0 RC2.37.1 — Hotfix GPU e sensores:</b> restaura jogador/eventos no QRhi e mostra no mapa a área real do Sensor Direcional.</li>"
          "<li><b>LUDO 4.0.0 RC2.37 — Eventos, Rotas, Áudio e Clima:</b> sensor de oito direções, visual inicial do evento, Lembrar Posição, áudio expandido e Weather otimizado.</li>"
          "<li><b>LUDO 4.0.0 RC2.36.2.2 — Ajuste de profundidade ★:</b> a troca atrás/à frente ocorre após um tile, preservando empate, meio passo, CPU e GPU.</li>"
          "<li><b>LUDO 4.0.0 RC2.36.2.1 — Build Hotfix:</b> corrige os segmentos QRhi para compilar editor/LudoPlayer e preserva a profundidade dinâmica dos tiles ★.</li>"
          "<li><b>LUDO 4.0.0 RC2.36.2 — Profundidade ★ estilo formato 4×4:</b> ★ usa faixa de prioridade de uma célula e alterna com jogador/eventos pelo Y dos pés; charsets grandes, meio tile, CPU e GPU seguem a mesma ordem.</li>"
          "<li><b>LUDO 4.0.0 RC2.36.1 — Hotfix ★:</b> restaura passagem superior estilo editores de RPG sobre jogador/eventos e preserva o cache de câmera com guarda/histerese.</li>"
          "<li><b>LUDO 4.0.0 RC2.36 — Bloco A / Renderização e Câmera:</b> cache QRhi ganha guarda e histerese contra rebuilds nas bordas.</li>"
          "<li><b>LUDO 4.0.0 RC2.35 — Fluxo Visual e Runtime:</b> comandos em janela tabulada; previews com panorama; Sprite Offset & Shake nativo; máscaras em cache; meio tile, áudio e Inventory Grid reativo revisados.</li>"
          "<li><b>LUDO 4.0.0 RC2.34 — Eventos, Câmera e Tileset:</b> comandos por cores com fonte maior e rotas expandidas; câmera ganha Apenas Zoom/Apenas Movimento; ★/✖ obedecem à toolbar; escolhas e modais paralelos recebem validação e identidade seguras.</li>"
          "<li><b>LUDO 4.0.0 RC2.33 — Runtime/Export/GPU Hotfix:</b> transições e legendas usam composição QRhi eficiente; rotas explícitas não têm pausas implícitas; exportação preserva filtro/renderer e valida áudio; localização cobre Pictures, modais e UI completa.</li>"
          "<li><b>LUDO 4.0.0 RC2.32 — Performance Matrix CPU/Runtime/QRhi:</b> benchmark oficial agora mede CPU, eventos, Fog/Weather, Pictures, Autotiles, A* e QRhi real; o resultado RC2.31.1 é preservado como baseline local antes da primeira matriz expandida.</li>"
          "<li><b>LUDO 4.0.0 RC2.31.1 — Hotfix / Benchmark Windows:</b> o benchmark por duplo clique agora mantém a janela aberta, encontra o CMake do Qt automaticamente e executa a medição mesmo sem Python; Python fica opcional para histórico/relatório avançado.</li>"
          "<li><b>LUDO 4.0.0 RC2.31 — O9 / Performance CI:</b> contratos estruturais seguem bloqueantes em todo PR; timing ganha workflow/histórico por commit, policy versionada e gate rígido apenas com baseline confiável do mesmo runnerTag, sem alterar gameplay, ProjectFormat ou SaveFormat.</li>"
          "<li><b>LUDO 4.0.0 RC2.30 — O8 / Áudio + Cache de Assets:</b> SEs reutilizam pool de 24 canais, WAV usa QSoundEffect de baixa latência, volumes/URLs ficam em cache e o inventário de Assets evita rescans até uma invalidação real, sem alterar Save/ProjectFormat.</li>"
          "<li><b>LUDO 4.0.0 RC2.29 — O7 / GPU Map Mesh Residente:</b> tiles estáticos do mapa ficam em buffers QRhi residentes em World Space; câmera/zoom/shake atualizam apenas um uniform, Autotiles reutilizam variantes do cache e F8 separa uploads estáticos/dinâmicos, sem alterar Save/ProjectFormat.</li>"
          "<li><b>LUDO 4.0.0 RC2.28 — O6 / Pathfinding Incremental:</b> buscas A* longas são repartidas em até 512 nós por tick, mantendo o mesmo caminho determinístico; caminhos curtos continuam imediatos, F8 mostra o trabalho do tick e o estado parcial nunca entra no Save.</li>"
          "<li><b>LUDO 4.0.0 RC2.27 — O5 / Undo por Diff + Jobs Assíncronos:</b> pinceladas locais usam histórico compacto; preview/import/hash de assets e exportação pesada rodam em workers; cancelamento preserva a exportação anterior e a UI deixa de depender de processEvents.</li>"
          "<li><b>LUDO 4.0.0 RC2.26 — O4 / CollisionGrid + EventSpatialIndex:</b> colisão estática passa a lookup O(1) por cache derivado e eventos usam buckets por célula; consultas locais deixam de varrer o mapa/lista inteira, mantendo half-step, Through, páginas e Save/Load sem mudança de formato.</li>"
          "<li><b>LUDO 4.0.0 RC2.25 — O3 / MapView Inteligente:</b> repaints locais usam dirty rects; Fog/markers/máscaras respeitam o clip visível, objetos fora da região são descartados e Panorama transformado usa cache, deixando mapas grandes mais leves sem mudar gameplay ou formatos.</li>"
          "<li><b>LUDO 4.0.0 RC2.24 — O2 / Hot Path e Alocações:</b> cache de chunks usa chave numérica, scratch buffers são reaproveitados, handles de textura são resolvidos antes do submit e o occlusion culling deixa de criar QString por célula; ordem CPU/GPU e formatos permanecem iguais.</li>"
          "<li><b>LUDO 4.0.0 RC2.23 — O1 / Performance Baseline:</b> F8 ganha P50/P95/P99, tempos por estágio e contadores de uploads/cache/state changes; benchmark sintético gera JSON para histórico e gate de regressão sem alterar gameplay ou formatos.</li>"
          "<li><b>LUDO 4.0.0 RC2.22 — Rota C / Editor e Preview:</b> rotas ganham edição em bloco, drag-and-drop, busca, presets por projeto, preview lateral executando o game::World real e debug F8 com caminho A*/estado sem criar runtime paralelo.</li>"
          "<li><b>LUDO 4.0.0 RC2.21.2 — Build Hotfix:</b> corrige a API const de consulta de eventos usada pelo preview da Rota B: Editor::findEvent agora possui overload const, sem remover const-correctness dos consumidores.</li>"
          "<li><b>LUDO 4.0.0 RC2.21.1 — Build Hotfix:</b> corrige a declaração de MapDoc usada pelo EventCommandValidator no Qt 6.8.3/MinGW por forward declaration explícita, sem alterar a Rota B.</li>"
          "<li><b>LUDO 4.0.0 RC2.21 — Rota B / Pathfinding e Alvos:</b> A* único e compartilhado para Jogador/Eventos; Ir até, Seguir, Fugir e Manter distância aceitam tile/Jogador/Eventos, com recálculo, half-step, limite de busca e validação preventiva.</li>"
          "<li><b>LUDO 4.0.0 RC2.20 — Rota A / Runtime de Movimento:</b> rotas usam estado explícito persistível, tickets, fila, pausa/retomada/cancelamento, políticas de bloqueio e executor único para Jogador/Eventos; autonomia e rota forçada ficam isoladas e falhas de configuração deixam de ser silenciosas.</li>"
          "<li><b>LUDO 4.0.0 RC2.19 — Bloco 5 100%:</b> Save/Load preserva fase de câmera, clima, Pictures e efeitos; F5/F6 validam o mesmo payload executável do LudoPlayer exportado; migração/defaults e matriz CPU/QRhi ganham contratos e testes dedicados.</li>"
          "<li><b>LUDO 4.0.0 RC2.18 — Bloco 4 100%:</b> Imagem em Sequência ganha pausa e frame fixo como estados runtime distintos, save/load v2 interno sem bump de formato e validação compartilhada que detecta spritesheets não divisíveis exatamente.</li>"
          "<li><b>LUDO 4.0.0 RC2.17 — Bloco 3 100%:</b> Pictures usam transform estrutural único para posição, âncora/pivot, escala, rotação, flip e transições em CPU/QRhi/editor; Zoom In e Zoom Out entram como comandos No-Code oficiais.</li>"
          "<li><b>LUDO 4.0.0 RC2.16 — Bloco 2 100%:</b> Clima usa um único WeatherState do editor ao runtime; CPU/QRhi consomem o mesmo RuntimeWeatherFrame em World Space, geração fica limitada à câmera/mapa e teleporte/troca de mapa preservam a regra correta.</li>"
          "<li><b>LUDO 4.0.0 RC2.15 — Bloco 1 100%:</b> World/Camera/Screen/Ui são espaços oficiais; World→Camera→Screen é a projeção única; CPU/QRhi usam um único snapshot por frame e a ordem visual passa a ser validada por estágio.</li>"
          "<li><b>LUDO 4.0.0 RC2.14.1 — Build Hotfix:</b> corrige a criação das pipelines QRhi no Qt 6.8.3/MinGW ao usar QRhiRenderPassDescriptor não-const, sem alterar o comportamento visual do Bloco 6.</li>"
          "<li><b>LUDO 4.0.0 RC2.14 — Câmera + Efeitos de Tela:</b> Tonalidade GPU passa a pós-processar a cena já composta como no CPU; Flash/Fade/Shake ganham estado runtime compartilhado e Screen/UI não herdam câmera, zoom ou tremor.</li>"
          "<li><b>LUDO 4.0.0 RC2.13 — Panorama/Parallax Runtime:</b> Fixar na tela usa Screen Space real sem câmera/zoom; CPU e QRhi compartilham a mesma geometria; a cobertura tiled clássica é preservada e o auto-scroll por Loop X/Y funciona também em camada fixa.</li>"
          "<li><b>LUDO 4.0.0 RC2.12 — Picture Sequence Runtime:</b> Imagem em Sequência usa frame/tempo próprios; mover, tint e flip não reiniciam; save/load preserva o instante; spritesheets inválidos têm fallback seguro.</li>"
          "<li><b>LUDO 4.0.0 RC2.10 — Weather World-Space Foundation:</b> Clima IN-GAME usa estado/geometria únicos em World Space; CPU/QRhi compartilham o mesmo campo e a preview de clima do editor foi removida.</li>"
          "<li><b>LUDO 4.0.0 RC2.9 — Visual Runtime Foundation:</b> World/Screen/Ui viram espaços oficiais; CPU/QRhi usam uma única projeção de câmera/zoom; Fog/Pictures declaram seu espaço e o diagnóstico exporta renderContract.</li>"
          "<li><b>LUDO 4.0.0 RC2.8 — Clima IN-GAME:</b> Testar Mapa/Testar Jogo/LudoPlayer ganham camada final dedicada de Chuva/Neve/Tempestade; CPU compõe após Screen Tone e QRhi usa screen-space lógico antes de Pictures/UI.</li>"
          "<li><b>LUDO 4.0.0 RC2.7 — Clima / Visibilidade:</b> MapView ganha preview animado, projetos antigos com Chuva/Neve/Tempestade são migrados corretamente e o runtime sincroniza o clima do mapa até um comando assumir o controle.</li>"
          "<li><b>LUDO 4.0.0 RC2.6 — Clima / Runtime:</b> chuva, neve e tempestade voltam a produzir partículas visíveis em CPU e GPU, presas ao mundo e recortadas ao mapa; o gate agora valida pixels CPU e quads QRhi.</li>"
          "<li><b>LUDO 4.0.0 RC2.5 — Velocidade de Autotile:</b> Autotiles Animados já criados, inclusive A1, podem ter FPS editado individualmente com preview, pausa e restauração para 6 FPS.</li>"
          "<li><b>LUDO 4.0.0 RC2.4 — Performance A1:</b> paleta compactada de A1/Autotiles usa cache e índice físico→visual O(1), removendo a lentidão do editor sem alterar IDs, atlas ou mapas.</li>"
          "<li><b>LUDO 4.0.0 RC2.3 — Charset XP / A1 / Clima / UX:</b> drag-and-drop indica acima/abaixo; Splash pós-projeto dura ao menos 3 s; Início do Jogador azul pode ser arrastado; Clima recupera intensidade e fica dentro do mapa; Charset XP 4×4 usa ciclo correto; Tilesets importam A1 16×12 e podem reduzir colunas/linhas com proteção de referências.</li>"
          "<li><b>LUDO 4.0.0 RC2.2 — Hotfix de editor/runtime:</b> Splash após Project Manager, Grid somente com linhas, Panorama/Fog recortados ao mapa, grade restaurada ao sair do Modo de Eventos, animação 0,5 contínua, tags Ludo automáticas em Comentários, paleta compacta de Autotile sem renumerar IDs, drag-and-drop de comandos e botão direito apagando Terreno.</li>"
          "<li><b>LUDO 4.0.0 RC2.1 — Correções de regressão / Release Candidate:</b> pixel-perfect CPU/GPU em zoom inteiro, recuperação de assets antigos, Panorama/Parallax, Autotiles/Tilesets, Simulador de UI, Character 4 frames, movimento 0,5 tile, Legendas, Pictures, Comando Ludo, Camera System, Accordion de eventos, Modo de Eventos, clima espacial e otimizações de stutter; ProjectFormat 2/SaveFormat 3 preservados.</li>"
          "<li><b>LUDO 4.0.0 RC1 — Feature Freeze / Release Candidate:</b> primeiro candidato da série 4.0; versionamento final, gate de regressão/compatibilidade, suite RC1 rotulada, preservação de ProjectFormat 2/SaveFormat 3 e checklist de distribuição em PC Windows limpo.</li>"
          "<li><b>LUDO 3.30.0 — Pre-4.0 Candidate / Animated Autotiles &amp; Stability:</b> autotiles animados integrados a Conexões do Autotile com FPS, Loop, Ping-Pong, sincronização global/fase por célula, preview à direita, edição no Gerenciador de Tilesets e runtime compartilhado CPU/QRhi; Validator, stress tests e release gate fecham o ciclo pré-4.0.</li>"
          "<li><b>LUDO 3.29.2 — Recommended Toolbar Palette:</b> toolbar refinada para a paleta recomendada (Azure/Aqua/Lavender/Coral/Amber + neutro) e regra única de preview de comandos à direita, incluindo painel lateral de Escolhas.</li>"
          "<li><b>LUDO 3.29.0 — Editor Workflow &amp; Event/Tileset UX:</b> páginas de evento em Ribbon, prioridade e olhar para o jogador, Esperar em 60 Hz lógico, preview de Escolhas, Gerenciador de Tilesets, Importar Autotile e toolbar/menus reorganizados.</li>"
          "<li><b>LUDO 3.28.2 — Hotfix de mensagens localizadas:</b> Testar Jogo/Testar Mapa agora preservam catálogo de localização e acessibilidade no Runtime Snapshot; mensagens com Text Key deixam de cair no texto original.</li>"
          "<li><b>LUDO 3.28.1 — Hotfix de localização em runtime:</b> troca de idioma agora atualiza imediatamente menus, loja e batalha já abertos, relocaliza a Tela de Título ao retornar do gameplay e centraliza Interpreter/UI/Banco de Dados no mesmo idioma do catálogo do jogo.</li>"
          "<li><b>LUDO 3.28.0 — Bloco F / Localization, Accessibility &amp; 4.0 RC:</b> localização No-Code com Text Keys, idiomas/fallback/CSV e integração no UI Designer 2.20, mensagens, título e Banco de Dados; acessibilidade ganha escala, movimento reduzido e foco reforçado, com stress/clean-PC preparando o feature freeze.</li>"
          "<li><b>LUDO 3.27.0 — Bloco E / Debug &amp; Production:</b> F8 ganha debugger visual com Pause/Step/Continue e breakpoint, inspeção de Switches/Variáveis/Common Events/UI/foco, Runtime Profiler e Diagnostic Bundle; exportação passa por validação obrigatória e game.assets usa LUDOASSET2 com hashes e validações defensivas.</li>"
          "<li><b>LUDO 3.26.1 — Hotfix de compilação do Bloco D:</b> corrige a colisão do identificador interno <code>slots</code> com a macro reservada do Qt no Project Validator e amplia a auditoria preventiva.</li>"
          "<li><b>LUDO 3.26.0 — Bloco D / RPG Animation &amp; Battle:</b> Animation Editor No-Code com Pictures/spritesheets, partículas, SE, flash, shake, Timeline e hit frame; Skills/Items/ataques usam a animação no runtime, Troops ganham formação e Battle Events e inimigos recebem AI Rules condicionais e ponderadas.</li>"
          "<li><b>LUDO 3.25.0 — Bloco C / UI Designer Behavior:</b> UI Designer 2.19 adiciona comportamento No-Code nativo para Checkbox/Radio/Toggle, Slider, Dropdown, Tab Bar, Text Input, Stepper, Scroll e Inventory Grid; Run UI, título e runtime compartilham as mesmas regras e dialogs antigos de gameplay saem do alvo runtime.</li>"
          "<li><b>LUDO 3.24.1 — Hotfix Asset Database:</b> corrige a compilação do exportador no Qt 6.8.3/MinGW ao incluir QJsonArray diretamente em GameExporter.cpp.</li>"
          "<li><b>LUDO 3.24.0 — Bloco B / Asset Database:</b> GUIDs estáveis para Assets, reparo de move/rename, localizador de ausentes, grafo owner→GUID e exportação por dependências explícitas.</li>"
          "<li><b>LUDO 3.23.0 — Bloco A / Stability &amp; Architecture:</b> CI Windows Qt 6.8.3 + MinGW 13.1 com GPU, smoke/E2E tests, Command Schema Registry, diagnóstico de comandos desconhecidos e primeira divisão estrutural do Project Validator.</li>"
          "<li><b>LUDO 3.22.1 — Properties Tab Bar:</b> o painel Propriedades troca o dropdown por abas horizontais e os campos numéricos ganham botões de aumentar/diminuir maiores em todo o Editor.</li>"
          "<li><b>LUDO 3.22.0 — Project Manager:</b> a Engine agora começa por um gerenciador de projetos sem templates, com criação vazia, recentes, busca, favoritos, localizar, duplicar, renomear e manutenção segura da lista.</li>"
          "<li><b>LUDO 3.21.1 — Testar Jogo GPU Hotfix:</b> corrige a inicialização do QRhi em Jogar &gt; Testar Jogo; a API GPU é escolhida antes de o renderer entrar na hierarquia do LudoPlayer, preservando a janela única.</li>"
          "<li><b>LUDO 3.21.0 — LUDO UI Screen / UI Designer 2.18:</b> o UI Designer ganha Carregar Tela e Exportar Tela em formato .ludoui, com remapeamento seguro de IDs, dependências de Pictures por ID/nome e preservação de States, Timeline, Events, Visual Logic, Bindings e Components.</li>"
          "<li><b>LUDO 3.20.8 — Hierarchy Layers Hotfix:</b> a Hierarquia do UI Designer agora funciona como camadas: o item mais acima fica na frente e arrastar na lista muda a ordem visual no Canvas e no runtime.</li>"
          "<li><b>LUDO 3.20.6 — Widget Pictures:</b> Usar Imagem/Picture no UI Designer abre a Biblioteca de Pictures da LUDO e referencia a Picture escolhida por ID, sem duplicar arquivo no Widget.</li>"
          "<li><b>LUDO 3.20.5 — Hotfix de compilação:</b> corrige a string HTML da seção Novidades e a captura do controle de divisões da grade no Menu Bar do UI Designer.</li>"
          "<li><b>LUDO 3.20.4 — UI / Player Polish:</b> Testar Mapa volta a uma janela própria para GPU/CPU; UI Designer 2.16 ganha Menu Bar, fundo base opcional, Timeline compacta com clips arrastáveis, maximização, Picture/Imagem em Assets/Pictures e ícone colorido.</li>"
          "<li><b>LUDO 3.20.3 — Player em janela única:</b> Tela de Título, gameplay CPU e gameplay QRhi/GPU agora permanecem no mesmo shell nativo do LudoPlayer, sem abrir uma segunda janela ao iniciar/continuar.</li>"
          "<li><b>LUDO 3.20.2 — Hotfix de compilação da Timeline:</b> corrige a referência de metadados usada pelas tracks no sync da Timeline para compilar corretamente no Qt 6.8/MinGW.</li>"
          "<li><b>LUDO 3.20.1 — Hotfix UI Designer:</b> corrige crash em telas vazias, restaura janelas/Window Skin no Run UI e troca a Timeline por tracks de Animation Clips no estilo editor de vídeo.</li>"
          "<li><b>LUDO 3.20.0 — UI Designer 2.15:</b> telas customizadas, Inspector contextual, Run UI WYSIWYG, Title/Save/Load editáveis, novos comandos de UI/Choices/Checkpoint, áudio com fades/crossfade e exportação com nome/ícone/portátil.</li>"
          "<li><b>LUDO 3.19.2 — Hotfix responsivo do UI Designer:</b> redimensionamento vertical liberado, Hierarchy e Timeline com rolagem própria e abertura ajustada à área útil do monitor, sem alterar a resolução da UI do jogo.</li>"
          "<li><b>LUDO 3.19.1 — Hotfix Block F:</b> compatibilidade Qt 6.8/MinGW corrigida no Add Gold do Simulator e no duplo clique do UI Validation, sem mudar o formato do projeto.</li>"
          "<li><b>LUDO 3.19.0 — UI Designer 2.14 / Block F:</b> UI Profiler + Validation, cache de imagens e cache da árvore ordenada de layout/hierarchy para reduzir trabalho repetido do renderer.</li>"
          "<li><b>LUDO 3.18.0 — UI Designer 2.13 / Block F:</b> Full UI Simulator no Canvas com Run UI, Simulation Data, mouse/teclado/controle virtual, Events/Conditions, Visual Logic, Screen States, Animation Clips, sons, scroll e tooltips.</li>"
          "<li><b>LUDO 3.17.1:</b> hotfix de compilação do template Save / Load do Block E no MinGW/Qt 6.8; o identificador interno não usa mais a macro reservada <code>slots</code>.</li>"
          "<li><b>LUDO 3.17.0 — UI Designer 2.12 / Block E:</b> Templates/UI Library com telas prontas editáveis, buscas nas bibliotecas/Hierarchy, agrupamento e atalhos de workflow, drag-and-drop de imagens e ações rápidas no Inspector.</li>"
          "<li><b>LUDO 3.15.1:</b> hotfix de compilação do Particle Emitter 2D no MinGW/Qt 6.8; o callback interno não usa mais o identificador reservado <code>emit</code>.</li>"
          "<li><b>LUDO 3.15.0 — UI Designer 2.10 / Block D:</b> Advanced Rendering 2D com rotação/scale/skew, blend modes e Materials (Glow, Blur, Pixelate, Glitch, CRT/VHS etc.), mais Particle Emitter 2D procedural com preview animado, tudo no mesmo UiDrawList CPU/QRhi.</li>"
          "<li><b>LUDO 3.13.1:</b> hotfix de compilação do Block C no MinGW/Qt 6.8; callbacks de criar/excluir Evento agora capturam corretamente a sincronização do Visual Logic.</li>"
          "<li><b>LUDO 3.13.0 — UI Designer 2.8 / Block C:</b> Visual Logic Graph No Code com nós de Evento, Condição/Branch, Ação, Delay e Sequence, conexões visuais, conversão de Eventos Simples e runtime não bloqueante.</li>"
          "<li><b>LUDO 3.12.0 — UI Designer 2.7 / Block B:</b> navegação No Code por teclado/gamepad com foco automático/manual e conexões visuais, mais Screen States com Visible/Enabled/Visual State/Animation Clip e Action de troca no runtime.</li>"
          "<li><b>LUDO 3.10.0 — UI Designer 2.6 / Block A:</b> Style System + Themes, Windowskin herdável nos Widgets, Style Classes por estado, 9-Slice customizável, Masks 2D e Clip Children.</li>"
          "<li><b>LUDO 3.8.0 — UI Designer 2.5:</b> Components/Prefabs reutilizáveis com biblioteca, drag-and-drop no Canvas, propriedades expostas, overrides e Apply/Revert.</li>"
          "<li><b>LUDO 3.7.0 — UI Designer 2.4:</b> Widget Framework 2D com paleta categorizada, Widgets customizados na Hierarchy/Canvas, Inspector próprio, Data Binding/States/Timeline/Events reutilizados e render pelo UiDrawList; Menu já aceita clique No Code em widgets interativos.</li>"
          "<li><b>LUDO 3.6.0 — UI Designer 2.3:</b> Data Binding No Code com Text/Value/Maximum/Visible/Enabled/Opacity/Image/Color/Progress, fontes de Party/Actor/Variáveis/Switches/Inventário, tokens de texto e Preview Data no Canvas.</li>"
          "<li><b>LUDO 3.5.0 — UI Designer 2.2:</b> Timeline horizontal no estilo de editor de vídeo com régua, trilhas, playhead, keyframes arrastáveis, timecode/zoom e Events + Conditions No Code com ações em sequência.</li>"
          "<li><b>LUDO 3.4.1 — UI Designer 2.1:</b> Estados Normal/Hover/Pressed/Focused/Selected/Disabled, Animation Clips, Timeline com keyframes, presets editáveis, Bounce/Elastic e preview No Code em tempo real.</li>"
          "<li><b>LUDO 3.4.0 — UI Designer 2.0:</b> Hierarchy pai/filho, anchors, pivot, containers Horizontal/Vertical/Grid, multi-seleção, snapping, alinhamento, Undo/Redo, previews de resolução e transições No Code com easing.</li>"
          "<li><b>LUDO 3.3.1:</b> corrigido o travamento/crash ao abrir ou interagir com o UI Designer — Layouts.</li>"
          "<li><b>LUDO 3.3.0 — In-Game UI Fase 4:</b> Battle HUD, seleção de alvos e Loja agora são in-game; novo UI Designer visual para layouts de Menu/Batalha/Loja.</li>"
          "<li><b>LUDO 3.2.0 — In-Game UI Fase 3:</b> Menu, Inventário, Status, Equipamento, Habilidades, Missões, Save/Load e Configurações agora são desenhados dentro do jogo.</li>"
          "<li><b>LUDO 3.1.1:</b> correção de compilação do Number Input no Qt 6.8/MinGW.</li>"
          "<li><b>LUDO 3.1.0 — In-Game UI Fase 2:</b> Name Box, entrada numérica, seleção de item e confirmações agora são in-game; tema por projeto com Window Skin, cursor, animações e sons.</li>"
          "</ul>"),QStringLiteral("LUDO 4.0.0 RC2.71 — Picture System 2.0 + Picture World: nomes, grupos, valores dinâmicos e attachments integrados. LUDO 4.0.0 RC2.70 — Condition Builder + Expressões: árvore AND/OR visual e Game Values universais pelo Value Resolver. LUDO 4.0.0 RC2.69 — Validator e Diagnósticos 2.0: validação contextual, referências, recursão e painel Problemas integrado. LUDO 4.0.0 RC2.68.3 — Picture Bilinear: suavização de Pictures usa Bilinear quando ativada e Nearest quando desativada. LUDO 4.0.0 RC2.68.2 — Negative Hotfix: Negative desligado volta a intensidade zero e preserva as cores originais de Pictures/Picture Text. LUDO 4.0.0 RC2.68.1 — Build Hotfix: compatibilidade de compilação Qt 6 em ProjectIO. LUDO 4.0.0 RC2.68 — Stabilization: Pictures, Eventos, Texto, Som de Passos, comandos e assets consolidados. RC2.67.1 — Hotfix de Build Windows: RC2.67 preservada; scripts .bat normalizados para CRLF e pipeline de shaders corrigido. Consolidação e Paridade: ciclo RC2.61–RC2.66 fechado com paridade Editor/F5/F6/Player/Export, Save/Load, Assets, UI, Pictures e Eventos; ProjectFormat 2 / SaveFormat 3 / runtime snapshot 4.")}
    };
    for(const Page&p:pages){auto* w=new QWidget(tabs);auto* pv=new QVBoxLayout(w);auto* b=new QTextBrowser(w);b->setHtml(p.html);pv->addWidget(b,1);auto* copy=new QPushButton(tr("Copiar exemplos"),w);pv->addWidget(copy,0,Qt::AlignRight);connect(copy,&QPushButton::clicked,w,[p]{QApplication::clipboard()->setText(p.code);});tabs->addTab(w,p.title);}v->addWidget(tabs,1);auto* box=new QDialogButtonBox(QDialogButtonBox::Close,this);v->addWidget(box);connect(box,&QDialogButtonBox::rejected,this,&QDialog::accept);
}

CutsceneSkipDialog::CutsceneSkipDialog(core::Editor& ed,QWidget* parent):QDialog(parent)
{
    setWindowTitle(tr("Customizar Ludo Cutscene Skip"));resize(760,650);auto* v=new QVBoxLayout(this);
    auto* hint=new QLabel(tr("Configure como o jogador pula cenas. A prévia usa a proporção da resolução atual (%1×%2).").arg(ed.gameResolution.width()).arg(ed.gameResolution.height()),this);hint->setWordWrap(true);v->addWidget(hint);
    auto* preview=new QLabel(this);preview->setMinimumHeight(300);preview->setAlignment(Qt::AlignCenter);preview->setStyleSheet(QStringLiteral("background:#111;border:1px solid #555"));v->addWidget(preview,1);
    auto* form=new QFormLayout;auto* enabled=new QCheckBox(tr("Permitir pular cutscenes"),this);enabled->setChecked(ed.cutsceneSkip.enabled);auto* mode=new QComboBox(this);mode->addItem(tr("Segurar o botão"),true);mode->addItem(tr("Apertar uma vez"),false);mode->setCurrentIndex(ed.cutsceneSkip.holdToSkip?0:1);auto* hold=new QSpinBox(this);hold->setRange(100,5000);hold->setSuffix(tr(" ms"));hold->setValue(ed.cutsceneSkip.holdMs);auto* fade=new QSpinBox(this);fade->setRange(0,180);fade->setSuffix(tr(" quadros"));fade->setValue(ed.cutsceneSkip.fadeFrames);auto* label=new QLineEdit(ed.cutsceneSkip.label,this);label->setToolTip(tr("Use {key} para inserir automaticamente a tecla configurada."));auto* corner=new QComboBox(this);corner->addItems({tr("Superior esquerdo"),tr("Superior direito"),tr("Inferior esquerdo"),tr("Inferior direito")});corner->setCurrentIndex(ed.cutsceneSkip.corner);auto* key=new QKeySequenceEdit(this);const QVector<int> keys=ed.inputMap.keysFor(GameAction::SkipCutscene);if(!keys.isEmpty())key->setKeySequence(QKeySequence(keys.first()));
    form->addRow(enabled);form->addRow(tr("Acionamento:"),mode);form->addRow(tr("Tempo segurando:"),hold);form->addRow(tr("Fade ao pular:"),fade);form->addRow(tr("Texto:"),label);form->addRow(tr("Posição:"),corner);form->addRow(tr("Botão:"),key);v->addLayout(form);
    auto refresh=[=,&ed]{QImage img(ed.gameResolution,QImage::Format_ARGB32_Premultiplied);img.fill(QColor("#263554"));QPainter p(&img);QLinearGradient sky(0,0,0,img.height());sky.setColorAt(0,QColor("#253c72"));sky.setColorAt(1,QColor("#79b7a0"));p.fillRect(img.rect(),sky);p.setBrush(QColor("#6c9251"));p.setPen(Qt::NoPen);p.drawRect(0,img.height()*2/3,img.width(),img.height()/3);p.setBrush(QColor("#f6cf67"));p.drawEllipse(QRectF(img.width()*.45,img.height()*.52,40,54));QString k=key->keySequence().toString(QKeySequence::NativeText);if(k.isEmpty())k=tr("C");QString text=label->text();text.replace(QStringLiteral("{key}"),k);QFont f=p.font();f.setBold(true);f.setPixelSize(qMax(12,img.height()/36));p.setFont(f);const int bw=p.fontMetrics().horizontalAdvance(text)+36,bh=qMax(38,img.height()/14),m=qMax(12,img.height()/35);int x=m,y=m;const int c=corner->currentIndex();if(c==1||c==3)x=img.width()-bw-m;if(c>=2)y=img.height()-bh-m;p.setBrush(QColor(15,18,30,210));p.drawRoundedRect(QRect(x,y,bw,bh),10,10);p.setPen(Qt::white);p.drawText(QRect(x+12,y,bw-24,bh-8),Qt::AlignCenter,text);p.setBrush(QColor("#67d6ff"));p.drawRoundedRect(QRectF(x+12,y+bh-8,(bw-24)*.62,4),2,2);p.end();preview->setPixmap(QPixmap::fromImage(img).scaled(preview->size()-QSize(8,8),Qt::KeepAspectRatio,Qt::SmoothTransformation));};
    connect(mode,&QComboBox::currentIndexChanged,this,[=](int){hold->setEnabled(mode->currentData().toBool());refresh();});connect(hold,&QSpinBox::valueChanged,this,[=](int){refresh();});connect(label,&QLineEdit::textChanged,this,[=](const QString&){refresh();});connect(corner,&QComboBox::currentIndexChanged,this,[=](int){refresh();});connect(key,&QKeySequenceEdit::keySequenceChanged,this,[=](const QKeySequence&){refresh();});QTimer::singleShot(0,this,refresh);
    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);v->addWidget(box);connect(box,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(box,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(this,&QDialog::accepted,this,[=,&ed]{ed.cutsceneSkip.enabled=enabled->isChecked();ed.cutsceneSkip.holdToSkip=mode->currentData().toBool();ed.cutsceneSkip.holdMs=hold->value();ed.cutsceneSkip.fadeFrames=fade->value();ed.cutsceneSkip.label=label->text();ed.cutsceneSkip.corner=corner->currentIndex();const QKeySequence seq=key->keySequence();if(!seq.isEmpty())ed.inputMap.setKeys(GameAction::SkipCutscene,{int(seq[0].key())});ed.markDirty();});
}

// ============================================================================
//  ControlsDialog — mapa de teclas do runtime
// ============================================================================
namespace {

/// Modal minúsculo que captura teclas: cada tecla apertada entra na lista.
/// Confirmar/cancelar é no MOUSE de propósito — se fosse no teclado, a tecla
/// do botão seria capturada junto.
class KeyCaptureDialog : public QDialog
{
public:
    KeyCaptureDialog(const QString& acao, QVector<int> atuais, QWidget* parent)
        : QDialog(parent), m_keys(atuais)
    {
        setWindowTitle(tr("Teclas de “%1”").arg(acao));
        resize(360, 220);
        auto* v = new QVBoxLayout(this);
        auto* lbl = new QLabel(tr("Aperte as teclas que quiser usar para <b>%1</b>.<br>"
                                  "Cada tecla apertada entra na lista.").arg(acao), this);
        lbl->setWordWrap(true);
        v->addWidget(lbl);
        m_list = new QListWidget(this);
        v->addWidget(m_list, 1);
        auto* row = new QHBoxLayout;
        auto* limpar = new QPushButton(tr("Limpar"), this);
        auto* ok = new QPushButton(tr("Pronto"), this);
        auto* cancel = new QPushButton(tr("Cancelar"), this);
        row->addWidget(limpar);
        row->addStretch(1);
        row->addWidget(cancel);
        row->addWidget(ok);
        v->addLayout(row);
        connect(limpar, &QPushButton::clicked, this, [this] { m_keys.clear(); refresh(); });
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        refresh();
    }
    QVector<int> keys() const { return m_keys; }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->isAutoRepeat()) return;
        const int k = e->key();
        if (k == 0 || k == Qt::Key_unknown) return;
        if (!m_keys.contains(k)) m_keys.push_back(k);
        refresh();
        e->accept();
    }

private:
    void refresh()
    {
        m_list->clear();
        for (int k : m_keys) m_list->addItem(core::InputMap::keyName(k));
        if (m_keys.isEmpty()) m_list->addItem(tr("(nenhuma tecla)"));
    }
    QVector<int> m_keys;
    QListWidget* m_list = nullptr;
};

} // namespace

ControlsDialog::ControlsDialog(core::Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef), m_map(editorRef.inputMap),m_system(editorRef.inputSystem)
{
    setWindowTitle(tr("Ludo Input System"));
    resize(1050, 720);
    auto* v = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Cada ação pode ter <b>várias teclas</b> — setas e WASD ao mesmo "
                               "tempo, por exemplo. O mapa é gravado no projeto."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    v->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({ tr("Ação"), tr("Teclas"), tr("Definir"),tr("Ícone teclado"),tr("Botão genérico"),tr("Ícone controle") });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for(int c=2;c<6;++c)m_table->horizontalHeader()->setSectionResizeMode(c,QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(m_table, 1);

    auto* aviso = new QLabel(this);
    aviso->setObjectName(QStringLiteral("avisoConflito"));
    aviso->setWordWrap(true);
    aviso->setStyleSheet(QStringLiteral("color:#ffd45e;font-size:11px"));
    v->addWidget(aviso);

    auto* advanced=new QGroupBox(tr("Ludo Input System"),this);auto* af=new QFormLayout(advanced);
    auto* adaptive=new QCheckBox(tr("Prompts adaptativos ao dispositivo ativo"),advanced);adaptive->setChecked(m_system.adaptivePrompts);
    auto* keyboard=new QCheckBox(tr("Permitir ícones de teclado do IconSet"),advanced);keyboard->setChecked(m_system.keyboardPrompts);
    auto* controller=new QCheckBox(tr("Permitir ícones de controle genérico do IconSet"),advanced);controller->setChecked(m_system.controllerPrompts);
    auto* analog=new QCheckBox(tr("Movimento analógico quando disponível"),advanced);analog->setChecked(m_system.analogMovement);
    auto* deadzone=new QDoubleSpinBox(advanced);deadzone->setRange(0,0.95);deadzone->setSingleStep(.05);deadzone->setValue(m_system.analogDeadzone);
    auto* forced=new QComboBox(advanced);forced->addItem(tr("Teclado / detecção automática"),QString());forced->addItem(tr("Controle genérico"),QStringLiteral("generic"));forced->setCurrentIndex(qMax(0,forced->findData(m_system.forcedController)));
    af->addRow(adaptive);af->addRow(keyboard);af->addRow(controller);af->addRow(analog);af->addRow(tr("Zona morta analógica:"),deadzone);af->addRow(tr("Dispositivo para prévia:"),forced);v->addWidget(advanced);

    auto* row = new QHBoxLayout;
    auto* restaurar = new QPushButton(tr("Restaurar padrão"), this);
    row->addWidget(restaurar);
    row->addStretch(1);
    v->addLayout(row);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(restaurar, &QPushButton::clicked, this, [this] {
        m_map = core::InputMap::defaults();m_system=core::InputSystemSettings();
        refresh();
    });
    connect(this, &QDialog::accepted, this, [this,adaptive,keyboard,controller,analog,deadzone,forced] {
        if (ed.inputMap != m_map) ed.inputMap = m_map;
        m_system.adaptivePrompts=adaptive->isChecked();m_system.keyboardPrompts=keyboard->isChecked();m_system.controllerPrompts=controller->isChecked();m_system.analogMovement=analog->isChecked();m_system.analogDeadzone=deadzone->value();m_system.forcedController=forced->currentData().toString();ed.inputSystem=m_system;ed.markDirty();
    });
    refresh();
}

static int chooseInputIcon(core::Editor& ed,int current,QWidget* parent)
{
    if(!ed.iconSet.isValid()){QMessageBox::information(parent,QObject::tr("IconSet"),QObject::tr("Adicione uma folha em Configurações do Jogo → Recursos → Folhas de ícones."));return current;}
    QDialog d(parent);d.setWindowTitle(QObject::tr("Escolher ícone do IconSet"));d.resize(720,520);auto* v=new QVBoxLayout(&d);auto* scroll=new QScrollArea(&d);auto* view=new IconSheetView(ed,scroll);scroll->setWidget(view);scroll->setWidgetResizable(false);v->addWidget(scroll,1);auto* info=new QLabel(current>=0?QObject::tr("Atual: \\I[%1]").arg(current):QObject::tr("Nenhum ícone"),&d);v->addWidget(info);int selected=current;QObject::connect(view,&IconSheetView::picked,&d,[&](int i){selected=i;info->setText(QObject::tr("Selecionado: \\I[%1]").arg(i));});auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);auto* clear=box->addButton(QObject::tr("Sem ícone"),QDialogButtonBox::ResetRole);v->addWidget(box);QObject::connect(clear,&QPushButton::clicked,&d,[&]{selected=-1;d.accept();});QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);return d.exec()==QDialog::Accepted?selected:current;
}
static QIcon inputIconPreview(const core::Editor& ed,int index)
{
    if(index<0||!ed.iconSet.isValid())return {};const QRect r=ed.iconSet.iconRect(index);if(r.isNull())return {};return QIcon(QPixmap::fromImage(ed.iconSet.image.copy(r)).scaled(24,24,Qt::KeepAspectRatio,Qt::FastTransformation));
}

void ControlsDialog::refresh()
{
    const QVector<core::GameAction> acoes = core::allGameActions();
    m_table->setRowCount(acoes.size());
    QStringList conflitos;
    for (int i = 0; i < acoes.size(); ++i) {
        const core::GameAction a = acoes[i];
        m_table->setItem(i, 0, new QTableWidgetItem(core::gameActionLabel(a)));
        m_table->setItem(i, 1, new QTableWidgetItem(m_map.describe(a)));
        auto* b = new QPushButton(tr("Definir…"), m_table);
        connect(b, &QPushButton::clicked, this, [this, a] {
            KeyCaptureDialog cap(core::gameActionLabel(a), m_map.keysFor(a), this);
            if (cap.exec() != QDialog::Accepted) return;
            m_map.setKeys(a, cap.keys());
            refresh();
        });
        m_table->setCellWidget(i, 2, b);
        const QString aid=core::gameActionId(a);
        auto makeIconButton=[this,aid](bool controller){const int idx=controller?m_system.controllerIcons.value(aid,-1):m_system.keyboardIcons.value(aid,-1);auto* ib=new QPushButton(idx>=0?tr("I[%1]").arg(idx):tr("Escolher…"),m_table);ib->setIcon(inputIconPreview(ed,idx));connect(ib,&QPushButton::clicked,this,[this,aid,controller]{const int old=controller?m_system.controllerIcons.value(aid,-1):m_system.keyboardIcons.value(aid,-1);const int n=chooseInputIcon(ed,old,this);if(controller)m_system.controllerIcons[aid]=n;else m_system.keyboardIcons[aid]=n;refresh();});return ib;};
        m_table->setCellWidget(i,3,makeIconButton(false));
        auto* buttonNo=new QComboBox(m_table);buttonNo->addItem(tr("Padrão automático"),-1);
        const QStringList controllerNames={QStringLiteral("A"),QStringLiteral("B"),QStringLiteral("X"),QStringLiteral("Y"),tr("Botão superior esquerdo (LB)"),tr("Botão superior direito (RB)"),tr("Selecionar / Voltar"),tr("Iniciar / Menu"),tr("Analógico esquerdo"),tr("Analógico direito"),tr("Direcional para cima"),tr("Direcional para baixo"),tr("Direcional para esquerda"),tr("Direcional para direita")};
        for(int button=0;button<controllerNames.size();++button)buttonNo->addItem(controllerNames[button],button);
        buttonNo->setCurrentIndex(qMax(0,buttonNo->findData(m_system.controllerButtons.value(aid,-1))));connect(buttonNo,&QComboBox::currentIndexChanged,this,[this,aid,buttonNo]{m_system.controllerButtons[aid]=buttonNo->currentData().toInt();});m_table->setCellWidget(i,4,buttonNo);
        m_table->setCellWidget(i,5,makeIconButton(true));

        for (int k : m_map.keysFor(a)) {
            core::GameAction outra;
            if (m_map.findConflict(a, k, &outra))
                conflitos << tr("%1 também é usada por “%2”")
                                 .arg(core::InputMap::keyName(k), core::gameActionLabel(outra));
        }
    }
    if (auto* aviso = findChild<QLabel*>(QStringLiteral("avisoConflito"))) {
        conflitos.removeDuplicates();
        // Repetir tecla é permitido de propósito (Esc = cancelar E sair), então
        // isto é um aviso, não um erro.
        aviso->setText(conflitos.isEmpty() ? QString()
                                           : tr("Teclas repetidas: %1.").arg(conflitos.join(QStringLiteral("; "))));
    }
}


// Implementações de Pictures foram movidas para PictureDialogs.cpp.

GameResolutionDialog::GameResolutionDialog(core::Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Resolução do jogo"));
    auto* raiz = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    auto* presets = new QComboBox(this);
    struct P { const char* nome; int w, h; };
    const QVector<P> lista = {
        { "320 × 240  (formato clássico de 256 cores)", 320, 240 },
        { "544 × 416  (formato 544×416)", 544, 416 },
        { "640 × 480", 640, 480 },
        { "800 × 600  (padrão)", 800, 600 },
        { "816 × 624  (formato 816×624)", 816, 624 },
        { "1280 × 720 (720p)", 1280, 720 },
    };
    presets->addItem(tr("Personalizada…"), 0);
    for (const P& p : lista)
        presets->addItem(QString::fromUtf8(p.nome), QSize(p.w, p.h));

    auto* larg = new QSpinBox(this);
    larg->setRange(64, 4096);
    larg->setSuffix(tr(" px"));
    larg->setValue(ed.gameResolution.width());
    auto* alt = new QSpinBox(this);
    alt->setRange(64, 4096);
    alt->setSuffix(tr(" px"));
    alt->setValue(ed.gameResolution.height());

    for (int i = 1; i < presets->count(); ++i)
        if (presets->itemData(i).toSize() == ed.gameResolution) presets->setCurrentIndex(i);

    form->addRow(tr("Tamanhos prontos:"), presets);
    form->addRow(tr("Largura:"), larg);
    form->addRow(tr("Altura:"), alt);
    raiz->addLayout(form);

    auto* nota = new QLabel(
        tr("Esta é a tela do jogo. Ao <b>maximizar</b> a janela, a imagem é ampliada "
           "por um fator <b>inteiro</b> (2×, 3×…) e centralizada, com barras pretas "
           "em volta — é o que mantém a pixel art nítida.<br><br>"
           "A pré-visualização das imagens de tela usa esta mesma resolução."), this);
    nota->setWordWrap(true);
    nota->setStyleSheet(QStringLiteral("color:#999;"));
    raiz->addWidget(nota);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    raiz->addWidget(bb);

    connect(presets, &QComboBox::currentIndexChanged, this, [presets, larg, alt](int i) {
        const QSize s = presets->itemData(i).toSize();
        if (s.isValid() && !s.isEmpty()) {
            larg->setValue(s.width());
            alt->setValue(s.height());
        }
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this, larg, alt] {
        const QSize nova(larg->value(), alt->value());
        if (nova != ed.gameResolution) {
            ed.gameResolution = nova;
            ed.markDirty();
        }
        accept();
    });
}

// ------------------------------------------------------------ folha de ícones
IconSheetView::IconSheetView(core::Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    setMinimumSize(200, 120);
}

QSize IconSheetView::sizeHint() const
{
    const core::IconSet& is = ed.iconSet;
    if (!is.isValid()) return QSize(320, 200);
    return QSize(is.image.width() * m_zoom, is.image.height() * m_zoom);
}

void IconSheetView::refresh()
{
    updateGeometry();
    update();
}

void IconSheetView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#1b1b1b"));
    const core::IconSet& is = ed.iconSet;
    if (!is.isValid()) {
        p.setPen(QColor("#888"));
        p.drawText(rect(), Qt::AlignCenter, tr("Nenhuma folha de ícones importada"));
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.scale(m_zoom, m_zoom);
    p.drawImage(0, 0, is.image);
    p.resetTransform();

    // Grade + número de cada ícone. É exatamente o que o autor precisa para
    // escrever \I[n] sem adivinhar.
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    const int cw = is.cellW * m_zoom, ch = is.cellH * m_zoom;
    for (int i = 0; i < is.count(); ++i) {
        const QRect src=is.iconRect(i);
        const int cx=src.x()*m_zoom,cy=src.y()*m_zoom;
        const QRect celula(cx, cy, cw, ch);
        p.setPen(QPen(QColor(255, 255, 255, 40)));
        p.drawRect(celula.adjusted(0, 0, -1, -1));
        const QString txt = QString::number(i);
        const QRect caixa(cx + 1, cy + 1, p.fontMetrics().horizontalAdvance(txt) + 4, 12);
        p.fillRect(caixa, QColor(0, 0, 0, 170));
        p.setPen(QColor("#ffd45e"));
        p.drawText(caixa, Qt::AlignCenter, txt);
        if (i == m_sel) {
            p.setPen(QPen(QColor("#7ec8ff"), 2));
            p.drawRect(celula.adjusted(1, 1, -2, -2));
        }
    }
}

void IconSheetView::mousePressEvent(QMouseEvent* e)
{
    const core::IconSet& is = ed.iconSet;
    if (!is.isValid()) return;
    const QPoint pt(int(e->position().x()/m_zoom),int(e->position().y()/m_zoom));
    int i=-1;for(int n=0;n<is.count();++n)if(is.iconRect(n).contains(pt)){i=n;break;}
    if (i < 0) return;
    m_sel = i;
    update();
    emit picked(i);
}

IconSetDialog::IconSetDialog(core::Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Folha de ícones"));
    resize(720, 560);
    auto* raiz = new QVBoxLayout(this);

    auto* linha = new QHBoxLayout;
    auto* bImp = new QPushButton(tr("Importar imagem…"), this);
    m_cellW = new QSpinBox(this);
    m_cellW->setRange(4, 256);
    m_cellW->setValue(ed.iconSet.cellW);
    m_cellW->setPrefix(tr("largura "));
    m_cellH = new QSpinBox(this);
    m_cellH->setRange(4, 256);
    m_cellH->setValue(ed.iconSet.cellH);
    m_cellH->setPrefix(tr("altura "));
    linha->addWidget(bImp);
    linha->addWidget(new QLabel(tr("Tamanho do ícone:"), this));
    linha->addWidget(m_cellW);
    linha->addWidget(m_cellH);
    linha->addStretch(1);
    raiz->addLayout(linha);

    auto* rol = new QScrollArea(this);
    rol->setWidgetResizable(false);
    m_grade = new IconSheetView(ed, rol);
    rol->setWidget(m_grade);
    raiz->addWidget(rol, 1);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#aaa;"));
    raiz->addWidget(m_info);
    m_blocks=new QListWidget(this);m_blocks->setMaximumHeight(110);m_blocks->setToolTip(tr("Origem e faixa de índices de cada bloco do atlas"));raiz->addWidget(m_blocks);
    auto* removeBlock=new QPushButton(tr("Remover última folha…"),this);removeBlock->setToolTip(tr("Somente a última folha pode ser removida, para preservar os índices anteriores."));raiz->addWidget(removeBlock,0,Qt::AlignLeft);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    raiz->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bImp, &QPushButton::clicked, this, &IconSetDialog::importar);
    connect(removeBlock,&QPushButton::clicked,this,&IconSetDialog::removerFolha);
    connect(m_cellW, &QSpinBox::valueChanged, this, [this](int v) {
        ed.iconSet.cellW = v;
        if(ed.iconSet.blocks.size()==1){ed.iconSet.blocks[0].cols=ed.iconSet.image.width()/v;ed.iconSet.blocks[0].startIndex=0;}
        ed.markDirty();
        atualizar();
    });
    connect(m_cellH, &QSpinBox::valueChanged, this, [this](int v) {
        ed.iconSet.cellH = v;
        if(ed.iconSet.blocks.size()==1){ed.iconSet.blocks[0].rows=ed.iconSet.image.height()/v;ed.iconSet.blocks[0].startIndex=0;}
        ed.markDirty();
        atualizar();
    });
    connect(m_grade, &IconSheetView::picked, this, [this](int i) {
        m_info->setText(tr("Ícone <b>%1</b> — escreva <code>\\I[%1]</code> no texto "
                           "(mensagem, legenda ou imagem de texto).").arg(i));
    });
    atualizar();
}

void IconSetDialog::importar()
{
    const QStringList files = AssetBrowserDialog::chooseImages(ed, this, QStringLiteral("Icons"));
    if (files.isEmpty()) return;
    int added=0;
    for(const QString& f:files){QImage img(f);if(img.isNull())continue;added+=ed.iconSet.appendImage(img,QFileInfo(f).completeBaseName(),ed.projectRelativePath(f));}
    if(added<=0){QMessageBox::warning(this,tr("Folhas de ícones"),tr("Nenhuma imagem pôde ser adicionada. Confira o tamanho das células."));return;}
    ed.markDirty();
    atualizar();
}

void IconSetDialog::removerFolha()
{
    if(ed.iconSet.blocks.isEmpty())return;
    const core::IconBlock last=ed.iconSet.blocks.last();
    if(QMessageBox::question(this,tr("Remover folha"),tr("Remover “%1” (índices %2–%3)?").arg(last.name).arg(last.startIndex).arg(last.startIndex+last.count()-1))!=QMessageBox::Yes)return;
    ed.iconSet.blocks.removeLast();
    if(ed.iconSet.blocks.isEmpty()){ed.iconSet.image=QImage();ed.iconSet.sourcePath.clear();}
    else{int width=0,height=0;for(const core::IconBlock& b:ed.iconSet.blocks){width=qMax(width,b.x+b.cols*ed.iconSet.cellW);height=qMax(height,b.y+b.rows*ed.iconSet.cellH);}ed.iconSet.image=ed.iconSet.image.copy(0,0,width,height);ed.iconSet.sourcePath=ed.iconSet.blocks.first().sourcePath;}
    ed.markDirty();atualizar();
}

void IconSetDialog::atualizar()
{
    m_grade->refresh();
    m_grade->resize(m_grade->sizeHint());
    const core::IconSet& is = ed.iconSet;
    m_blocks->clear();for(const core::IconBlock& b:is.blocks)m_blocks->addItem(tr("%1 · índices %2–%3 · %4").arg(b.name).arg(b.startIndex).arg(b.startIndex+b.count()-1).arg(b.sourcePath));
    const bool combined=is.blocks.size()>1; m_cellW->setEnabled(!combined); m_cellH->setEnabled(!combined);
    if (!is.isValid()) {
        m_info->setText(tr("Importe uma imagem com os ícones lado a lado, todos do "
                           "mesmo tamanho — como uma folha de tileset."));
        return;
    }
    m_info->setText(tr("%1 ícones · folhas combinadas: %2 · célula %3×%4. "
                       "Novas folhas são anexadas sem alterar os números antigos.")
                        .arg(is.count()).arg(qMax(1,is.blocks.size()))
                        .arg(is.cellW).arg(is.cellH));
}

FontManagerDialog::FontManagerDialog(core::Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Fontes do projeto")); resize(650,430);
    auto* v=new QVBoxLayout(this);
    auto* hint=new QLabel(tr("As fontes ficam em Assets/Fonts. A fonte principal é usada por mensagens, legendas, HUD e textos sem uma fonte específica."),this);hint->setWordWrap(true);v->addWidget(hint);
    m_list=new QListWidget(this);v->addWidget(m_list,1);
    auto* row=new QHBoxLayout;auto* add=new QPushButton(tr("Adicionar fontes…"),this);auto* remove=new QPushButton(tr("Remover da lista"),this);row->addWidget(add);row->addWidget(remove);row->addStretch(1);v->addLayout(row);
    auto* form=new QFormLayout;m_main=new QComboBox(this);form->addRow(tr("Fonte principal do jogo:"),m_main);v->addLayout(form);
    auto* box=new QDialogButtonBox(QDialogButtonBox::Close,this);v->addWidget(box);
    connect(add,&QPushButton::clicked,this,&FontManagerDialog::importar);
    connect(remove,&QPushButton::clicked,this,[this]{int i=m_list->currentRow();if(i<0||i>=ed.projectFonts.size())return;const QString fam=ed.projectFonts[i].family;ed.projectFonts.remove(i);if(ed.mainFontFamily==fam)ed.mainFontFamily.clear();ed.markDirty();atualizar();});
    connect(m_main,&QComboBox::currentIndexChanged,this,[this](int){ed.mainFontFamily=m_main->currentData().toString();ed.markDirty();});
    connect(box,&QDialogButtonBox::rejected,this,&QDialog::accept);atualizar();
}

void FontManagerDialog::importar()
{
    const QStringList files=AssetBrowserDialog::chooseFiles(ed,this,QStringLiteral("Fonts"));
    for(const QString& file:files){const QString ext=QFileInfo(file).suffix().toLower();if(ext!=QLatin1String("ttf")&&ext!=QLatin1String("otf"))continue;const int id=QFontDatabase::addApplicationFont(file);if(id<0)continue;const QStringList fams=QFontDatabase::applicationFontFamilies(id);for(const QString& family:fams){const QString rel=ed.projectRelativePath(file);bool exists=false;for(const core::ProjectFont& f:ed.projectFonts)if(f.sourcePath==rel&&f.family==family){exists=true;break;}if(!exists)ed.projectFonts.push_back({rel,family});}}
    ed.markDirty();atualizar();
}

void FontManagerDialog::atualizar()
{
    m_list->clear();for(const core::ProjectFont& f:ed.projectFonts)m_list->addItem(QStringLiteral("%1 — %2").arg(f.family,f.sourcePath));
    QSignalBlocker b(m_main);m_main->clear();m_main->addItem(tr("(fonte padrão do sistema)"),QString());for(const core::ProjectFont& f:ed.projectFonts)if(m_main->findData(f.family)<0)m_main->addItem(f.family,f.family);m_main->setCurrentIndex(qMax(0,m_main->findData(ed.mainFontFamily)));
}

// ------------------------------------------------------- texto rico (picture)

void MessageCommandDialog::esconderBotoes() { if (m_box) m_box->hide(); }
QString MessageCommandDialog::texto() const { return m_edit->toPlainText(); }
void MessageCommandDialog::setTexto(const QString& t) { m_edit->setPlainText(t); }

void SubtitleCommandDialog::esconderBotoes() { if (m_box) m_box->hide(); }
QString SubtitleCommandDialog::texto() const { return m_edit->toPlainText(); }
void SubtitleCommandDialog::setTexto(const QString& t) { m_edit->setPlainText(t); }

// ============================================================================
//  Janela única de texto: caixa de mensagem OU legenda
// ============================================================================
//  Antes eram dois comandos com duas janelas, e o autor tinha de saber de
//  antemão qual queria. Agora é uma janela só com um seletor no topo — e o
//  comando gravado continua sendo `message` ou `subtitle.show`, então nenhum
//  projeto antigo precisa ser convertido.
// ----------------------------------------------------------------------------
TextCommandDialog::TextCommandDialog(core::Editor& editorRef, core::EventCommand& cmd,
                                     QWidget* parent)
    : QDialog(parent), ed(editorRef), m_cmd(cmd)
{
    setWindowTitle(tr("Mostrar texto"));
    resize(qMin(1700, ed.gameResolution.width() + 700),
           qMin(980, qMax(760, ed.gameResolution.height() + 150)));
    auto* raiz = new QVBoxLayout(this);

    auto* topo = new QHBoxLayout;
    topo->addWidget(new QLabel(tr("Como mostrar:"), this));
    m_modo = new QComboBox(this);
    m_modo->addItem(tr("Caixa de mensagem — trava o jogo e espera o botão"),
                    QStringLiteral("message"));
    m_modo->addItem(tr("Legenda — não trava, tem duração e pode seguir alguém"),
                    QStringLiteral("subtitle.show"));
    m_modo->setCurrentIndex(cmd.type == QLatin1String("subtitle.show") ? 1 : 0);
    topo->addWidget(m_modo, 1);
    raiz->addLayout(topo);

    // Os dois formulários que já existiam viram PÁGINAS desta janela: nada de
    // reescrever a prévia e os botões de código, que já estavam prontos.
    auto* pilha = new QStackedWidget(this);
    auto* dm = new MessageCommandDialog(ed, m_cmd, pilha);
    dm->setWindowFlags(Qt::Widget);
    dm->esconderBotoes();
    auto* ds = new SubtitleCommandDialog(ed, m_cmd, pilha);
    ds->setWindowFlags(Qt::Widget);
    ds->esconderBotoes();
    pilha->addWidget(dm);
    pilha->addWidget(ds);
    pilha->setCurrentIndex(m_modo->currentIndex());
    raiz->addWidget(pilha, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    raiz->addWidget(bb);

    connect(m_modo, &QComboBox::currentIndexChanged, this, [pilha, dm, ds](int i) {
        // O texto acompanha a troca de modo: quem escreveu a fala não deve
        // perdê-la só porque mudou de ideia sobre como ela aparece.
        if (i == 1) ds->setTexto(dm->texto());
        else        dm->setTexto(ds->texto());
        pilha->setCurrentIndex(i);
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this, pilha, dm, ds] {
        // Só o formulário ativo grava (cada um emite accepted e escreve no
        // comando; deixar os dois gravarem embaralharia os parâmetros).
        if (m_modo->currentIndex() == 1) ds->accept();
        else                             dm->accept();
        accept();
    });
}

void TextCommandDialog::trocarModo() {}
void TextCommandDialog::refresh() {}
void TextCommandDialog::gravar() {}

} // namespace ui
