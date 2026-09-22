#include "CharacterGraphicDialog.h"

#include "AssetBrowser.h"
#include "Icons.h"
#include "core/TilesetOps.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>

namespace ui {

class SpriteSheetView : public QWidget
{
public:
    explicit SpriteSheetView(QWidget* parent = nullptr) : QWidget(parent)
    { setMinimumSize(460, 360); setMouseTracking(true); }

    core::EventGraphic* graphic = nullptr;
    std::function<void()> changed;

protected:
    QRectF imageRect() const
    {
        if (!graphic || graphic->charset.isNull()) return {};
        // Pixel art: nunca use escala fracionária no preview. Se a folha não
        // couber em 1:1, ela permanece 1:1 e o QWidget apenas recorta as bordas.
        // Assim um pixel da charset nunca vira 1,37 ou 0,82 pixels de tela.
        const QSize src = graphic->charset.size();
        const int availW = qMax(1, width() - 16);
        const int availH = qMax(1, height() - 16);
        const int sx = availW / qMax(1, src.width());
        const int sy = availH / qMax(1, src.height());
        const int scale = qMax(1, qMin(sx, sy));
        const QSize dst(src.width() * scale, src.height() * scale);
        const int x = (width() - dst.width()) / 2;
        const int y = (height() - dst.height()) / 2;
        return QRectF(x, y, dst.width(), dst.height());
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#252a30"));
        if (!graphic || graphic->charset.isNull()) {
            p.setPen(QColor("#aeb7c2"));
            p.drawText(rect(), Qt::AlignCenter, tr("Escolha uma spritesheet em Assets"));
            return;
        }
        const QRectF dst = imageRect();
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(dst, graphic->charset);
        const int cols = graphic->totalCols(), rows = graphic->totalRows();
        const double cw = dst.width() / cols, ch = dst.height() / rows;
        p.setPen(QPen(QColor(255, 255, 255, 65), 1));
        for (int x = 1; x < cols; ++x) p.drawLine(QPointF(dst.left()+x*cw,dst.top()), QPointF(dst.left()+x*cw,dst.bottom()));
        for (int y = 1; y < rows; ++y) p.drawLine(QPointF(dst.left(),dst.top()+y*ch), QPointF(dst.right(),dst.top()+y*ch));

        const int bx = graphic->characterIndex % qMax(1, graphic->characterCols);
        const int by = graphic->characterIndex / qMax(1, graphic->characterCols);
        const int col = bx * graphic->charsetCols + graphic->frame;
        const int row = by * graphic->charsetRows + graphic->dir;
        p.setBrush(QColor(70, 150, 255, 55));
        p.setPen(QPen(QColor("#70d6ff"), 3));
        p.drawRect(QRectF(dst.left()+col*cw, dst.top()+row*ch, cw, ch).adjusted(1,1,-1,-1));
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (!graphic || graphic->charset.isNull() || e->button() != Qt::LeftButton) return;
        const QRectF dst = imageRect();
        if (!dst.contains(e->position())) return;
        const int cols = graphic->totalCols(), rows = graphic->totalRows();
        const int col = qBound(0, int((e->position().x()-dst.left()) / dst.width()*cols), cols-1);
        const int row = qBound(0, int((e->position().y()-dst.top()) / dst.height()*rows), rows-1);
        const int charX = col / qMax(1, graphic->charsetCols);
        const int charY = row / qMax(1, graphic->charsetRows);
        graphic->characterIndex = charY * qMax(1, graphic->characterCols) + charX;
        graphic->frame = col % qMax(1, graphic->charsetCols);
        graphic->dir = row % qMax(1, graphic->charsetRows);
        update();
        if (changed) changed();
    }
};

CharacterGraphicDialog::CharacterGraphicDialog(core::Editor& editor,
                                               const core::EventGraphic& initial,
                                               QWidget* parent)
    : QDialog(parent), m_ed(editor), m_graphic(initial)
{
    m_graphic.kind = core::EventGraphic::Charset;
    setWindowTitle(tr("Selecionar personagem"));
    resize(860, 570);
    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);

    auto* left = new QWidget(this);
    auto* lv = new QVBoxLayout(left);
    auto* choose = new QPushButton(icons::get(QStringLiteral("open")), tr("Escolher sprite…"), left);
    auto* detect = new QPushButton(tr("Detectar layout"), left);
    lv->addWidget(choose);
    lv->addWidget(detect);
    auto* form = new QFormLayout;
    m_frames = new QSpinBox(left); m_frames->setRange(1, 32);
    m_dirs = new QSpinBox(left); m_dirs->setRange(1, 16);
    m_charsX = new QSpinBox(left); m_charsX->setRange(1, 16);
    m_charsY = new QSpinBox(left); m_charsY->setRange(1, 16);
    form->addRow(tr("Frames por direção:"), m_frames);
    form->addRow(tr("Direções por personagem:"), m_dirs);
    form->addRow(tr("Personagens na horizontal:"), m_charsX);
    form->addRow(tr("Personagens na vertical:"), m_charsY);
    lv->addLayout(form);
    m_info = new QLabel(left);
    m_info->setWordWrap(true);
    m_info->setProperty("uiRole", QStringLiteral("hint"));
    lv->addWidget(m_info);
    lv->addStretch(1);
    body->addWidget(left);

    m_view = new SpriteSheetView(this);
    m_view->graphic = &m_graphic;
    m_view->changed = [this] { updateInfo(); };
    body->addWidget(m_view, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(choose, &QPushButton::clicked, this, [this] { chooseAsset(); });
    connect(detect, &QPushButton::clicked, this, [this] { autoDetect(); });
    for (QSpinBox* s : {m_frames,m_dirs,m_charsX,m_charsY})
        connect(s, &QSpinBox::valueChanged, this, [this](int) { syncFromControls(); });

    syncControls();
    updateInfo();
}

void CharacterGraphicDialog::chooseAsset()
{
    const QString path = AssetBrowserDialog::chooseImage(m_ed, this, QStringLiteral("Characters"));
    if (path.isEmpty()) return;
    QImage img(path);
    if (img.isNull()) return;
    m_graphic.charset = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_graphic.sourcePath = m_ed.projectRelativePath(path);
    m_graphic.characterIndex = 0;
    m_graphic.dir = 0;
    m_graphic.frame = 1;
    autoDetect();
}

QVector<int> CharacterGraphicDialog::detectedLayout(const QSize& size)
{
    const int w = size.width(), h = size.height();
    if (w <= 0 || h <= 0 || h % 4) return {};

    // Formatos clássicos de folhas com vários personagens continuam tendo
    // prioridade, para não reinterpretar um charset antigo como XP por acaso.
    if (w * 2 == h * 3 && w % 12 == 0 && h % 8 == 0) return {3, 4, 4, 2};
    if (w == h && w % 12 == 0 && w > 256) return {3, 4, 4, 3};

    // formato 4×4: um personagem = 4 quadros x 4 direções, sem precisar de
    // linhas transparentes entre as células. Ex.: 160x240 -> 40x60 por frame.
    // Só decidimos automaticamente quando 4 é inequívoco; se também couber em
    // 3 quadros, autoDetect() pergunta ao criador em vez de chutar.
    const bool canFour = (w % 4 == 0);
    const bool canThree = (w % 3 == 0);
    if (canFour && !canThree) return {4, 4, 1, 1};
    if (canThree && !canFour) return {3, 4, 1, 1};
    if (w == h && w <= 256 && canFour) return {4, 4, 1, 1};
    return {};
}

void CharacterGraphicDialog::autoDetect()
{
    if (m_graphic.charset.isNull()) return;

    QVector<int> l;
    const QSize detected = core::detectCharsetGrid(m_graphic.charset);
    if (detected.isValid()) {
        // Folhas 4-frame normalmente têm 4/8/16... colunas totais e 4
        // direções por personagem. Se o número de colunas também é múltiplo
        // de 3 (ex.: 12x8, o formato clássico de 3 frames), mantemos 3 frames
        // para não quebrar folhas antigas. Grades inequívocas como 4x4,
        // 8x4 e 16x8 passam a ser reconhecidas automaticamente como 4-frame.
        const bool canFourFrame = (detected.width() % 4 == 0) &&
                                  (detected.height() % 4 == 0);
        const bool canThreeFrame = (detected.width() % 3 == 0) &&
                                   (detected.height() % 4 == 0);
        int frames = canFourFrame && !canThreeFrame ? 4 : 3;
        if (canFourFrame && canThreeFrame) {
            // Ex.: 12×4 células pode significar 4 personagens de 3 frames OU
            // 3 personagens de 4 frames. A imagem sozinha não contém informação
            // suficiente para decidir; em vez de sempre errar para 3, o Detectar
            // Layout reconhece as duas possibilidades e pergunta ao criador.
            QMessageBox choice(this);
            choice.setWindowTitle(tr("Detectar layout"));
            choice.setText(tr("Esta folha pode usar 3 ou 4 frames por personagem."));
            choice.setInformativeText(tr("Quantos frames de animação cada personagem possui?"));
            QPushButton* four = choice.addButton(tr("4 frames"), QMessageBox::AcceptRole);
            QPushButton* three = choice.addButton(tr("3 frames"), QMessageBox::AcceptRole);
            choice.setDefaultButton(m_graphic.charsetCols == 4 ? four : three);
            choice.exec();
            frames = choice.clickedButton() == four ? 4 : 3;
        }
        if (detected.width() % frames == 0 && detected.height() % 4 == 0) {
            l = { frames, 4,
                  qMax(1, detected.width() / frames),
                  qMax(1, detected.height() / 4) };
        }
    }
    if (l.isEmpty()) l = detectedLayout(m_graphic.charset.size());
    if (l.isEmpty()) {
        const int w = m_graphic.charset.width();
        const int h = m_graphic.charset.height();
        const bool canFour = h % 4 == 0 && w % 4 == 0;
        const bool canThree = h % 4 == 0 && w % 3 == 0;
        if (canFour && canThree) {
            QMessageBox choice(this);
            choice.setWindowTitle(tr("Detectar layout"));
            choice.setText(tr("Esta folha sem separadores pode usar 3 ou 4 frames por direção."));
            choice.setInformativeText(tr("Escolha 4 frames para charsets no padrão formato 4×4."));
            QPushButton* four = choice.addButton(tr("4 frames (formato 4×4)"), QMessageBox::AcceptRole);
            QPushButton* three = choice.addButton(tr("3 frames"), QMessageBox::AcceptRole);
            choice.setDefaultButton(m_graphic.charsetCols == 4 ? four : three);
            choice.exec();
            l = choice.clickedButton() == four ? QVector<int>{4,4,1,1}
                                               : QVector<int>{3,4,1,1};
        }
    }
    if (l.isEmpty()) l = {3,4,1,1};
    const int oldCols = m_graphic.charsetCols;
    m_graphic.charsetCols=l[0];m_graphic.charsetRows=l[1];m_graphic.characterCols=l[2];m_graphic.characterRows=l[3];
    m_graphic.characterIndex = qBound(0, m_graphic.characterIndex, m_graphic.characterCount()-1);
    // O padrão XP usa o primeiro quadro como repouso e percorre 0-1-2-3.
    // Em uma detecção recém-feita, não herdamos o antigo centro de 3 frames.
    if (m_graphic.charsetCols == 4 && oldCols != 4) m_graphic.frame = 0;
    m_graphic.frame = qBound(0, m_graphic.frame, m_graphic.charsetCols-1);
    m_graphic.dir = qBound(0, m_graphic.dir, m_graphic.charsetRows-1);
    syncControls();
    m_view->update();
    updateInfo();
}

void CharacterGraphicDialog::syncFromControls()
{
    m_graphic.charsetCols = m_frames->value();
    m_graphic.charsetRows = m_dirs->value();
    m_graphic.characterCols = m_charsX->value();
    m_graphic.characterRows = m_charsY->value();
    m_graphic.characterIndex = qBound(0, m_graphic.characterIndex, m_graphic.characterCount()-1);
    m_graphic.frame = qBound(0, m_graphic.frame, m_graphic.charsetCols-1);
    m_graphic.dir = qBound(0, m_graphic.dir, m_graphic.charsetRows-1);
    m_view->update();
    updateInfo();
}

void CharacterGraphicDialog::syncControls()
{
    QSignalBlocker a(m_frames), b(m_dirs), c(m_charsX), d(m_charsY);
    m_frames->setValue(m_graphic.charsetCols);
    m_dirs->setValue(m_graphic.charsetRows);
    m_charsX->setValue(m_graphic.characterCols);
    m_charsY->setValue(m_graphic.characterRows);
}

void CharacterGraphicDialog::updateInfo()
{
    if (m_graphic.charset.isNull()) {
        m_info->setText(tr("Importe ou escolha uma spritesheet da pasta Assets/Characters."));
        return;
    }
    const QRect r = m_graphic.charsetFrameRect();
    m_info->setText(tr("%1×%2 px · %3 personagem(ns) · selecionado #%4 · direção %5 · frame %6 · quadro %7×%8")
                        .arg(m_graphic.charset.width()).arg(m_graphic.charset.height())
                        .arg(m_graphic.characterCount()).arg(m_graphic.characterIndex + 1)
                        .arg(m_graphic.dir + 1).arg(m_graphic.frame + 1)
                        .arg(r.width()).arg(r.height()));
}

} // namespace ui
