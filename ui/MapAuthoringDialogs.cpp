#include "MapAuthoringDialogs.h"

#include "AssetBrowser.h"
#include "Icons.h"
#include "core/ProjectIO.h"
#include "core/AutoTileTables.h"
#include "core/TilesetCatalog.h"
#include "core/TilesetOps.h"
#include "core/Wang.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

using namespace core;

namespace ui {
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
CombinedTilesetDialog::CombinedTilesetDialog(Editor& editorRef, QWidget* parent,
                                                   Mode mode, int fixedTileWidth, int fixedTileHeight)
    : QDialog(parent), ed(editorRef), m_mode(mode)
{
    setWindowTitle(m_mode == Mode::CreateTileset ? tr("Novo Tileset") : tr("Adicionar imagem ao Tileset"));
    resize(600, 680);
    auto* v = new QVBoxLayout(this);

    auto* hint = new QLabel(m_mode == Mode::CreateTileset
        ? tr("Crie um Tileset a partir de uma ou mais imagens. Se ultrapassar o limite de textura, o conteúdo será organizado em páginas automaticamente.")
        : tr("Escolha uma ou mais imagens para adicionar ao Tileset selecionado. Você decidirá depois se elas entram em uma nova página ou em uma página existente."), this);
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
    m_tilesetCategory = new QComboBox(this);
    m_tilesetCategory->setEditable(true);
    m_tilesetCategory->addItem(QString());
    QStringList categories;
    for (const Tileset& ts : ed.tilesets)
        if (!ts.category.isEmpty() && !categories.contains(ts.category)) categories << ts.category;
    categories.sort(Qt::CaseInsensitive);
    m_tilesetCategory->addItems(categories);
    form->addRow(tr("Categoria"), m_tilesetCategory);
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
    if (m_mode == Mode::AddImage) {
        m_name->setText(tr("Imagem"));
        form->setRowVisible(m_name, false);
        form->setRowVisible(m_tilesetCategory, false);
        if (fixedTileWidth > 0) { m_tw->setValue(fixedTileWidth); m_tw->setEnabled(false); }
        if (fixedTileHeight > 0) { m_th->setValue(fixedTileHeight); m_th->setEnabled(false); }
    }
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
    box->button(QDialogButtonBox::Ok)->setText(m_mode == Mode::CreateTileset ? tr("Criar Tileset") : tr("Continuar"));
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
        m_result.category = m_mode == Mode::CreateTileset
            ? m_tilesetCategory->currentText().trimmed().left(128) : QString();
        const auto parts = splitTilesetForTextureLimit(m_result, 4096);
        if (parts.size() > 1 && QMessageBox::question(this, tr("Organizar em páginas"),
                tr("A imagem de %1 × %2 px precisa de %3 páginas de até 4096 × 4096 px. Todo o conteúdo será preservado. Continuar?")
                    .arg(m_result.image.width()).arg(m_result.image.height()).arg(parts.size())) != QMessageBox::Yes)
            return;
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
        m_images.push_back(NamedImage{ img, QFileInfo(p).completeBaseName(), ed.projectRelativePath(p) });
    }
    m_list->clear();
    for (const NamedImage& ni : m_images)
        m_list->addItem(tr("%1 — %2×%3 px").arg(ni.name).arg(ni.img.width()).arg(ni.img.height()));
    if (m_mode == Mode::AddImage && !m_images.isEmpty()) m_name->setText(m_images.first().name);
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
    controlsScroll->setMinimumWidth(360);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
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
    animationForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    animationForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_animated = new QCheckBox(tr("A imagem tem vários quadros"), animationBox);
    m_frameCount = new QSpinBox(animationBox); m_frameCount->setRange(2, 32); m_frameCount->setValue(3);
    m_frameAxis = new QComboBox(animationBox);
    m_frameAxis->addItem(tr("Horizontal (lado a lado)"), QStringLiteral("horizontal"));
    m_frameAxis->addItem(tr("Vertical (um abaixo do outro)"), QStringLiteral("vertical"));
    m_animationFps = new QDoubleSpinBox(animationBox); m_animationFps->setRange(0.1, 60.0); m_animationFps->setDecimals(1); m_animationFps->setValue(6.0); m_animationFps->setSuffix(tr(" quadros/s"));
    m_animationLoop = new QCheckBox(tr("Repetir"), animationBox); m_animationLoop->setChecked(true);
    m_animationPingPong = new QCheckBox(tr("Ida e volta"), animationBox);
    m_animationSync = new QComboBox(animationBox);
    m_animationSync->addItem(tr("Sincronizado (água e lava)"), true);
    m_animationSync->addItem(tr("Cada célula começa em um momento diferente"), false);
    animationForm->addRow(m_animated);
    animationForm->addRow(tr("Quadros:"), m_frameCount);
    animationForm->addRow(tr("Organização da imagem:"), m_frameAxis);
    animationForm->addRow(tr("Velocidade:"), m_animationFps);
    auto* behaviorRow = new QHBoxLayout; behaviorRow->addWidget(m_animationLoop); behaviorRow->addWidget(m_animationPingPong); behaviorRow->addStretch(1);
    animationForm->addRow(tr("Reprodução:"), behaviorRow);
    animationForm->addRow(tr("Como a animação começa:"), m_animationSync);
    auto* animationHint = new QLabel(tr("O primeiro quadro ensina ao editor como o Autotile se conecta. Os outros quadros mudam apenas a aparência durante a animação."), animationBox);
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
            ? tr("A largura da imagem (%1 px) precisa ser divisível igualmente entre os %2 quadros da animação.").arg(m_sourceImage.width()).arg(count)
            : tr("A altura da imagem (%1 px) precisa ser divisível igualmente entre os %2 quadros da animação.").arg(m_sourceImage.height()).arg(count);
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
    const int normalTileset = ed.session.activeTilesetIdx;
    const TilesetSelection normalSelection = ed.session.tsSel;
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
        ed.session.activeTilesetIdx = normalTileset;
        ed.session.tsSel = normalSelection;
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
    ed.session.activeTilesetIdx = normalTileset;
    ed.session.tsSel = normalSelection;
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
                          "Confira no prévia antes de inserir."));
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
    const int normalTileset = ed.session.activeTilesetIdx;
    const TilesetSelection normalSelection = ed.session.tsSel;
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
        ed.session.activeTilesetIdx = normalTileset;
        ed.session.tsSel = normalSelection;
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
    ed.session.activeTilesetIdx = normalTileset;
    ed.session.tsSel = normalSelection;
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
} // namespace ui
