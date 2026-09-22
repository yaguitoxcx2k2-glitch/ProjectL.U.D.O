#include "PictureDialogs.h"
#include "core/ResourceManager.h"

#include "AssetBrowser.h"
#include "VisualEffectsPanel.h"
#include "PicturePreviewController.h"
#include "PreviewClock.h"
#include "TextEffectsEditorWidget.h"
#include "CommandPreviewDialog.h"
#include "core/PictureState.h"

#include "core/Renderer.h"
#include "game/GameWorld.h"
#include "game/RuntimePictureTransform.h"
#include "game/RuntimeRenderState.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QJsonDocument>
#include <QJsonArray>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>

using namespace core;

namespace ui {

// ============================================================================
//  Imagens de tela (pictures)
// ============================================================================
//  A biblioteca guarda as imagens DENTRO do projeto (data URI, como o charset
//  e os tilesets). Foi uma decisão consciente: o autor pode mover a pasta do
//  jogo, mandar o .json por e-mail e nada quebra — em troca, o arquivo cresce.
// ----------------------------------------------------------------------------

PictureLibraryDialog::PictureLibraryDialog(core::Editor& editorRef, QWidget* parent,
                                           bool selectionMode, const QString& initialPictureId)
    : QDialog(parent), ed(editorRef), m_selectionMode(selectionMode),
      m_initialPictureId(initialPictureId)
{
    setWindowTitle(selectionMode ? tr("Escolher imagem") : tr("Biblioteca de imagens"));
    resize(680, 460);

    auto* raiz = new QVBoxLayout(this);
    auto* topo = new QHBoxLayout;

    m_lista = new QListWidget(this);
    m_lista->setMinimumWidth(220);
    topo->addWidget(m_lista, 1);

    auto* dir = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(320, 280);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral(
        "background:#1b1b1b;border:1px solid #444;color:#888;"));
    m_preview->setText(tr("(nenhuma imagem)"));
    m_info = new QLabel(this);
    m_info->setStyleSheet(QStringLiteral("color:#aaa;"));
    dir->addWidget(m_preview, 1);
    dir->addWidget(m_info);
    topo->addLayout(dir, 1);
    raiz->addLayout(topo, 1);

    auto* botoes = new QHBoxLayout;
    auto* bImp = new QPushButton(tr("Adicionar folhas…"), this);
    auto* bRen = new QPushButton(tr("Renomear…"), this);
    auto* bRem = new QPushButton(tr("Remover"), this);
    botoes->addWidget(bImp);
    botoes->addWidget(bRen);
    botoes->addWidget(bRem);
    botoes->addStretch(1);
    raiz->addLayout(botoes);

    auto* dica = new QLabel(
        m_selectionMode
            ? tr("Escolha uma imagem da biblioteca para usar neste elemento. Você também pode importar, renomear ou remover imagens aqui.")
            : tr("As imagens ficam guardadas no projeto e podem ser usadas diretamente pelos comandos de imagem dos eventos."),
        this);
    dica->setWordWrap(true);
    dica->setStyleSheet(QStringLiteral("color:#888;"));
    raiz->addWidget(dica);

    auto* bb = new QDialogButtonBox(this);
    if (m_selectionMode) {
        m_useSelected = bb->addButton(tr("Usar imagem selecionada"), QDialogButtonBox::AcceptRole);
        bb->addButton(QDialogButtonBox::Cancel);
        connect(m_useSelected, &QPushButton::clicked, this, [this]{
            if (!selectedPictureId().isEmpty()) accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_lista, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
            if (!selectedPictureId().isEmpty()) accept();
        });
    } else {
        bb->addButton(QDialogButtonBox::Close);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    }
    raiz->addWidget(bb);

    connect(bImp, &QPushButton::clicked, this, &PictureLibraryDialog::importar);
    connect(bRem, &QPushButton::clicked, this, &PictureLibraryDialog::remover);
    connect(bRen, &QPushButton::clicked, this, &PictureLibraryDialog::renomear);
    connect(m_lista, &QListWidget::currentRowChanged, this,
            [this](int) {
                atualizarPreview();
                if (m_useSelected) m_useSelected->setEnabled(!selectedPictureId().isEmpty());
            });
    recarregar();
}

QString PictureLibraryDialog::selectedPictureId() const
{
    const int row = m_lista ? m_lista->currentRow() : -1;
    return (row >= 0 && row < ed.pictures.size()) ? ed.pictures[row].id : QString();
}

QString PictureLibraryDialog::choosePicture(core::Editor& ed, QWidget* parent, const QString& currentPictureId)
{
    PictureLibraryDialog dialog(ed, parent, true, currentPictureId);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedPictureId() : QString();
}

void PictureLibraryDialog::recarregar()
{
    int sel = m_lista->currentRow();
    m_lista->clear();
    for (const core::PictureAsset& a : ed.pictures)
        m_lista->addItem(QStringLiteral("%1  (%2×%3)")
                             .arg(a.name).arg(a.image.width()).arg(a.image.height()));
    if (ed.pictures.isEmpty()) {
        m_lista->addItem(tr("(biblioteca vazia)"));
        m_lista->setCurrentRow(0);
    } else {
        if (sel < 0 && !m_initialPictureId.isEmpty()) sel = ed.pictureIndexById(m_initialPictureId);
        if (sel < 0) sel = 0;
        m_lista->setCurrentRow(qBound(0, sel, ed.pictures.size() - 1));
    }
    atualizarPreview();
    if (m_useSelected) m_useSelected->setEnabled(!selectedPictureId().isEmpty());
}

void PictureLibraryDialog::atualizarPreview()
{
    const int i = m_lista->currentRow();
    if (i < 0 || i >= ed.pictures.size()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("(nenhuma imagem)"));
        m_info->clear();
        return;
    }
    const core::PictureAsset& a = ed.pictures[i];
    QPixmap pm = QPixmap::fromImage(a.image);
    if (pm.width() > m_preview->width() || pm.height() > m_preview->height())
        pm = pm.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_preview->setPixmap(pm);
    m_info->setText(tr("%1 · %2×%3 px · %4")
                        .arg(a.name).arg(a.image.width()).arg(a.image.height())
                        .arg(a.image.hasAlphaChannel() ? tr("com transparência")
                                                       : tr("sem transparência")));
}

void PictureLibraryDialog::importar()
{
    const QStringList arquivos = AssetBrowserDialog::chooseImages(ed, this, QStringLiteral("Pictures"));
    int ok = 0;
    for (const QString& f : arquivos) {
        QImage img(f);
        if (img.isNull()) continue;
        core::PictureAsset a;
        a.name = QFileInfo(f).completeBaseName();
        a.sourcePath = ed.projectRelativePath(f);
        // Formato premultiplicado: é o que o QPainter desenha mais rápido, e
        // evita reconverter a imagem a cada quadro do jogo.
        a.image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        ed.pictures.push_back(a);
        ++ok;
    }
    if (ok > 0) {
        ed.markDirty();
        emit ed.picturesChanged();
        ed.resources().notifyPictureLibraryChanged();
        recarregar();
        m_lista->setCurrentRow(ed.pictures.size() - 1);
    } else if (!arquivos.isEmpty()) {
        QMessageBox::warning(this, tr("Importar imagens"),
                             tr("Nenhuma das imagens escolhidas pôde ser lida."));
    }
}

void PictureLibraryDialog::remover()
{
    const int i = m_lista->currentRow();
    if (i < 0 || i >= ed.pictures.size()) return;
    const QString nome = ed.pictures[i].name;
    if (QMessageBox::question(this, tr("Remover imagem"),
                              tr("Remover “%1” da biblioteca?\n\nOs comandos que já usam "
                                 "esta imagem deixarão de mostrar alguma coisa.").arg(nome))
        != QMessageBox::Yes)
        return;
    ed.pictures.remove(i);
    ed.markDirty();
    emit ed.picturesChanged();
    ed.resources().notifyPictureLibraryChanged();
    recarregar();
}

void PictureLibraryDialog::renomear()
{
    const int i = m_lista->currentRow();
    if (i < 0 || i >= ed.pictures.size()) return;
    QDialog d(this);
    d.setWindowTitle(tr("Renomear imagem"));
    auto* v = new QVBoxLayout(&d);
    auto* le = new QLineEdit(ed.pictures[i].name, &d);
    v->addWidget(new QLabel(tr("Novo nome:"), &d));
    v->addWidget(le);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted || le->text().trimmed().isEmpty()) return;
    ed.pictures[i].name = le->text().trimmed();
    ed.markDirty();
    emit ed.picturesChanged();
    ed.resources().notifyPictureLibraryChanged();
    recarregar();
}

namespace {
/// Tamanho inicial seguro para notebook: usa a área disponível do sistema
/// operacional (que exclui barra de tarefas/dock), sem impedir o usuário de
/// redimensionar para cima ou para baixo depois.
void fitPictureDialogToScreen(QDialog* dialog, const QSize& preferred)
{
    QScreen* screen = dialog->screen();
    if (!screen) screen = QApplication::primaryScreen();
    if (!screen) {
        dialog->resize(preferred);
        dialog->setSizeGripEnabled(true);
        return;
    }
    const QRect available = screen->availableGeometry();
    const QSize cap(qMax(480, available.width() - 32),
                    qMax(360, available.height() - 32));
    dialog->setMinimumSize(qMin(720, cap.width()), qMin(460, cap.height()));
    dialog->resize(preferred.boundedTo(cap));
    dialog->move(available.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
    dialog->setSizeGripEnabled(true);
}
} // namespace

// ---------------------------------------------------------------- palco
PictureStageView::PictureStageView(core::Editor& editorRef, core::PictureDef& def,
                                   QWidget* parent)
    : QWidget(parent), ed(editorRef), m_def(def)
{
    setMinimumSize(420, 320);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    m_clock = new PreviewClock(this);
    connect(m_clock, &PreviewClock::frame, this, [this](double) { update(); });
}

void PictureStageView::playTransitions(bool on)
{
    m_singleTransition = 0;
    m_playTrans = on;
    m_ensaio.clearAll();
    if (on) {
        setAnimated(true);           // o ensaio precisa de quadros correndo
        core::PictureDef d = m_def;
        d.number = 1;
        m_ensaio.show(d);
        m_clock->restart();
    }
    update();
}

void PictureStageView::previewTransition(bool entering)
{
    m_playTrans=false;m_singleTransition=entering?1:2;m_ensaio.clearAll();setAnimated(true);m_clock->restart();update();
}

void PictureStageView::setReferencePictures(const QVector<core::PictureDef>& pictures)
{
    m_references = pictures;
    update();
}

void PictureStageView::setSnapEnabled(bool on)
{
    m_snapEnabled = on;
    if (!on) m_guideX = m_guideY = false;
    update();
}

void PictureStageView::setInteractive(bool on)
{
    m_interactive = on;
    m_grab = Grab::None;
    m_guideX = m_guideY = false;
    setCursor(on ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void PictureStageView::setShowPlayerReference(bool on)
{
    m_showPlayerReference = on;
    update();
}

void PictureStageView::setAnimated(bool on)
{
    if (on && !m_clock->running()) {
        m_clock->restart();
        m_clock->start(33);
    } else if (!on && m_clock->running()) {
        m_clock->stop();
        update();
    }
}

const core::PictureAsset* PictureStageView::asset() const
{
    return ed.pictureFor(m_def.assetId, m_def.assetName);
}

double PictureStageView::fator() const
{
    // A tela do jogo é a resolução do PROJETO: mudou a resolução, o palco
    // muda junto (é o que faz a posição arrastada aqui valer no jogo).
    const QSize r = ed.gameResolution;
    return qMin(width() / double(qMax(1, r.width())), height() / double(qMax(1, r.height())));
}

QPointF PictureStageView::paraTela(const QPointF& jogo) const
{
    const double f = fator();
    const double ox = (width() - ed.gameResolution.width() * f) / 2.0;
    const double oy = (height() - ed.gameResolution.height() * f) / 2.0;
    return QPointF(ox + jogo.x() * f, oy + jogo.y() * f);
}

QPointF PictureStageView::paraJogo(const QPointF& tela) const
{
    const double f = qMax(0.0001, fator());
    const double ox = (width() - ed.gameResolution.width() * f) / 2.0;
    const double oy = (height() - ed.gameResolution.height() * f) / 2.0;
    return QPointF((tela.x() - ox) / f, (tela.y() - oy) / f);
}

QTransform PictureStageView::transformParaPalco(const QTransform& logicalScreen) const
{
    // Converte uma matriz em pixels lógicos do jogo para o widget do preview
    // sem refazer a matemática de rotação/escala/flip. Construir a matriz a
    // partir da origem e dos vetores-base evita depender da ordem de
    // multiplicação de QTransform.
    const QPointF o = paraTela(logicalScreen.map(QPointF(0.0, 0.0)));
    const QPointF ex = paraTela(logicalScreen.map(QPointF(1.0, 0.0)));
    const QPointF ey = paraTela(logicalScreen.map(QPointF(0.0, 1.0)));
    return QTransform(ex.x() - o.x(), ex.y() - o.y(),
                      ey.x() - o.x(), ey.y() - o.y(),
                      o.x(), o.y());
}

QTransform PictureStageView::transformDaImagem() const
{
    const core::PictureAsset* a = asset();
    const QSizeF base = !m_tamBase.isEmpty()
                            ? m_tamBase
                            : (m_def.rich.enabled ? QSizeF(1, 1)
                                                  : (a ? QSizeF(a->image.size()) : QSizeF()));
    if (base.isEmpty()) return QTransform();

    game::LivePicture live;
    live.def = m_def;
    if (m_clock->running()) live.age = m_clock->elapsedSeconds();

    game::PictureFrame frame;
    frame.baseSize = base;
    frame.image = QImage(qMax(1, qRound(base.width())), qMax(1, qRound(base.height())),
                         QImage::Format_ARGB32_Premultiplied);
    frame.valid = true;

    return transformDaImagem(live, frame);
}

QTransform PictureStageView::transformDaImagem(const core::PictureDef& def,
                                                const QSizeF& base) const
{
    if (base.isEmpty()) return QTransform();
    game::LivePicture live;
    live.def = def;
    game::PictureFrame frame;
    frame.baseSize = base;
    frame.image = QImage(qMax(1, qRound(base.width())), qMax(1, qRound(base.height())),
                         QImage::Format_ARGB32_Premultiplied);
    frame.valid = true;
    return transformDaImagem(live, frame);
}

QTransform PictureStageView::transformDaImagem(const game::LivePicture& picture,
                                                const game::PictureFrame& frame) const
{
    if (!frame.valid || frame.baseSize.isEmpty() || frame.image.isNull()) return QTransform();
    const QSize gameSize = ed.gameResolution.isEmpty() ? QSize(1, 1) : ed.gameResolution;
    const game::RuntimeRenderState render = game::makeRuntimeRenderState(
        QPointF(0.0, 0.0), gameSize, QSizeF(gameSize), 1.0);
    const game::RuntimePictureTransform transform =
        game::makeRuntimePictureTransform(render, picture, frame);
    return transform.valid ? transformParaPalco(transform.screenTransform) : QTransform();
}

QPointF PictureStageView::ancoraEmTela() const
{
    return paraTela(QPointF(m_def.x, m_def.y));
}

void PictureStageView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#101010"));

    const double f = fator();
    const QRectF palco(paraTela(QPointF(0, 0)),
                       QSizeF(ed.gameResolution.width() * f, ed.gameResolution.height() * f));

    // Fundo: o mapa como o jogo mostraria, para dar noção de escala.
    p.save();
    p.setClipRect(palco);
    p.fillRect(palco, ed.mapInfo().background);
    p.translate(palco.topLeft());
    // Zoom 2× é o padrão do runtime; multiplicado pelo fator do palco.
    p.scale(2.0 * f, 2.0 * f);
    core::RenderOptions opt;
    opt.drawObjectFrames = false;
    core::drawLayerTree(p, ed, ed.layers(), 1.0, opt);
    p.restore();

    // Moldura da tela do jogo.
    p.setPen(QPen(QColor("#4a4a4a"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(palco);

    // No ensaio de movimento, o herói ajuda a avaliar escala e sobreposição.
    // Ele é apenas uma referência visual: não altera o comando nem o mapa.
    if (m_showPlayerReference) {
        const QPointF feet = paraTela(QPointF(ed.gameResolution.width() / 2.0,
                                              ed.gameResolution.height() / 2.0));
        const double spriteScale = 2.0 * f;
        p.save();
        p.setClipRect(palco);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 90));
        p.drawEllipse(QRectF(feet.x() - 12.0 * f, feet.y() - 5.0 * f,
                             24.0 * f, 9.0 * f));
        if (ed.player.hasCharset()) {
            const QSize fs = ed.player.frameSize();
            const game::CharsetCell cell = game::charsetCellFor(game::Dir::Down, ed.player);
            const int frame = qBound(0, ed.player.effectiveIdleFrame(),
                                     qMax(0, ed.player.framesPerDirection() - 1));
            const QRect src((cell.colOffset + frame) * fs.width(), cell.row * fs.height(),
                            fs.width(), fs.height());
            const QRectF dst(feet.x() - fs.width() * spriteScale / 2.0,
                             feet.y() - fs.height() * spriteScale,
                             fs.width() * spriteScale, fs.height() * spriteScale);
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.drawImage(dst, ed.player.charset, src);
        } else {
            p.setPen(QPen(QColor("#f5f7ff"), qMax(1.0, f)));
            p.setBrush(QColor("#4f7cff"));
            p.drawEllipse(feet + QPointF(0, -28.0 * f), 8.0 * f, 8.0 * f);
            p.drawRoundedRect(QRectF(feet.x() - 9.0 * f, feet.y() - 22.0 * f,
                                     18.0 * f, 22.0 * f), 4.0 * f, 4.0 * f);
        }
        p.setPen(QColor(255, 255, 255, 190));
        QFont playerLabelFont = p.font();
        playerLabelFont.setPixelSize(qMax(9, qRound(11.0 * f)));
        p.setFont(playerLabelFont);
        p.drawText(QRectF(feet.x() - 55.0 * f, feet.y() + 3.0 * f,
                          110.0 * f, 18.0 * f), Qt::AlignHCenter | Qt::AlignTop,
                   tr("Jogador (referência)"));
        p.restore();
    }

    // Mesa de luz: pictures já criadas podem ser ligadas como referência.
    // São desenhadas antes da picture atual, sem entrar no comando salvo.
    for (const core::PictureDef& ref : std::as_const(m_references)) {
        game::LivePicture live;
        live.def = ref;
        const game::PictureFrame frame = m_fx.build(live, ed);
        if (!frame.valid) continue;
        const QTransform rt = transformDaImagem(live, frame);
        p.save();
        p.setClipRect(palco);
        p.setTransform(rt, true);
        p.setOpacity(qBound(0.0, frame.opacity / 255.0 * 0.55, 0.75));
        p.setRenderHint(QPainter::SmoothPixmapTransform, ref.smooth);
        p.drawImage(QPointF(0, 0), frame.image);
        p.restore();

        const QPolygonF outline = transformDaImagem(ref, frame.baseSize)
            .map(QPolygonF(QRectF(QPointF(0, 0), frame.baseSize)));
        p.setPen(QPen(QColor(80, 220, 255, 190), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(outline);
        const QPointF rp = paraTela(QPointF(ref.x, ref.y));
        p.setBrush(QColor(80, 220, 255));
        p.drawEllipse(rp, 3, 3);
    }

    const core::PictureAsset* a = asset();
    if (!a && !m_def.rich.enabled) {
        p.setPen(QColor("#888"));
        p.drawText(palco, Qt::AlignCenter, tr("Escolha uma imagem da biblioteca"));
        return;
    }

    // A imagem, com a mesma matemática do runtime — inclusive os efeitos.
    game::LivePicture lp;
    lp.def = m_def;
    lp.age = m_clock->running() ? m_clock->elapsedSeconds() : 0.0;
    if (m_playTrans) {
        // Ensaio das transições: um gerenciador de verdade roda o ciclo
        // entrar → segurar → sair → recomeçar, com o mesmo código do jogo.
        static const double kSegurar = 1.2;
        m_ensaio.update(1.0 / 30.0);
        game::LivePicture* viva = m_ensaio.at(1);
        if (!viva) {                                   // saiu de cena: recomeça
            core::PictureDef d = m_def;
            d.number = 1;
            m_ensaio.show(d);
            viva = m_ensaio.at(1);
        } else if (!viva->inTransition() &&
                   viva->age > (m_def.fx.transitionInFrames / 60.0) + kSegurar) {
            m_ensaio.startTransitionOut(1, m_def.fx.transitionOut,
                                        m_def.fx.transitionOutFrames);
        }
        if (viva) lp = *viva;
    } else if (m_singleTransition != 0) {
        const bool entering=m_singleTransition==1;
        const int frames=entering?m_def.fx.transitionInFrames:m_def.fx.transitionOutFrames;
        const core::PictureTransition type=entering?m_def.fx.transitionIn:m_def.fx.transitionOut;
        lp.phase=entering?game::LivePicture::In:game::LivePicture::Out;lp.phaseType=type;lp.phaseDuration=qMax(1,frames)/60.0;lp.phaseTime=qMin(lp.phaseDuration,m_clock->elapsedSeconds());
        if(lp.phaseTime>=lp.phaseDuration&&entering)lp.phase=game::LivePicture::Normal;
    }
    const game::PictureFrame quadro = m_fx.build(lp, ed);
    if (!quadro.valid) return;
    m_tamBase = quadro.baseSize;      // as alças moldam ESTA imagem

    p.save();
    p.setClipRect(palco);
    const QTransform tq = transformDaImagem(lp, quadro);
    p.setTransform(tq, true);
    p.setOpacity(qBound(0.0, quadro.opacity / 255.0, 1.0));
    switch (m_def.blend) {
    case core::PictureBlend::Add:      p.setCompositionMode(QPainter::CompositionMode_Plus); break;
    case core::PictureBlend::Multiply: p.setCompositionMode(QPainter::CompositionMode_Multiply); break;
    case core::PictureBlend::Screen:   p.setCompositionMode(QPainter::CompositionMode_Screen); break;
    case core::PictureBlend::Normal:   break;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_def.smooth);
    p.drawImage(QPointF(0, 0), quadro.image);
    p.restore();

    // Alças: contorno, quadrado de escala (canto inferior direito) e círculo
    // de rotação (acima do topo).
    const QTransform t = transformDaImagem();
    const QPolygonF quad = t.map(QPolygonF(QRectF(QPointF(0, 0), quadro.baseSize)));
    p.setPen(QPen(QColor("#ffd45e"), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(quad);

    if (!m_interactive) return;

    p.setPen(QPen(QColor("#111"), 1));
    p.setBrush(QColor("#ffd45e"));
    const QPointF cantoBR = quad.size() > 2 ? quad[2] : QPointF();
    p.drawRect(QRectF(cantoBR - QPointF(5, 5), QSizeF(10, 10)));
    const QPointF meioTopo = quad.size() > 1 ? (quad[0] + quad[1]) / 2.0 : QPointF();
    const QPointF meioBase = quad.size() > 3 ? (quad[3] + quad[2]) / 2.0 : QPointF();
    QPointF dirCima = meioTopo - meioBase;
    const double len = std::hypot(dirCima.x(), dirCima.y());
    if (len > 0.001) dirCima /= len;
    const QPointF alcaRot = meioTopo + dirCima * 22.0;
    p.setPen(QPen(QColor("#ffd45e"), 1));
    p.drawLine(meioTopo, alcaRot);
    p.setBrush(QColor("#7ec8ff"));
    p.drawEllipse(alcaRot, 6, 6);

    // Cruz da âncora: mostra em torno de que ponto a imagem gira.
    const QPointF anc = ancoraEmTela();
    p.setPen(QPen(QColor("#ff6b6b"), 1));
    p.drawLine(anc + QPointF(-6, 0), anc + QPointF(6, 0));
    p.drawLine(anc + QPointF(0, -6), anc + QPointF(0, 6));

    // Guias aparecem somente enquanto o ímã está prendendo um eixo.
    p.setPen(QPen(QColor(72, 220, 255, 220), 1, Qt::DashLine));
    if (m_guideX) {
        const double x = paraTela(QPointF(m_guideXValue, 0)).x();
        p.drawLine(QPointF(x, palco.top()), QPointF(x, palco.bottom()));
    }
    if (m_guideY) {
        const double y = paraTela(QPointF(0, m_guideYValue)).y();
        p.drawLine(QPointF(palco.left(), y), QPointF(palco.right(), y));
    }
}

void PictureStageView::mousePressEvent(QMouseEvent* e)
{
    if (!m_interactive) return;
    const core::PictureAsset* a = asset();
    if (!a && !m_def.rich.enabled) return;
    const QSizeF base = !m_tamBase.isEmpty() ? m_tamBase
                                               : (m_def.rich.enabled ? QSizeF() : QSizeF(a->image.size()));
    if (base.isEmpty()) return;
    const QTransform t = transformDaImagem();
    const QPolygonF quad = t.map(QPolygonF(QRectF(QPointF(0, 0), base)));
    const QPointF pos = e->position();

    auto perto = [&](const QPointF& p) {
        return std::hypot(pos.x() - p.x(), pos.y() - p.y()) <= 9.0;
    };
    const QPointF cantoBR = quad.size() > 2 ? quad[2] : QPointF();
    const QPointF meioTopo = quad.size() > 1 ? (quad[0] + quad[1]) / 2.0 : QPointF();
    const QPointF meioBase = quad.size() > 3 ? (quad[3] + quad[2]) / 2.0 : QPointF();
    QPointF dirCima = meioTopo - meioBase;
    const double len = std::hypot(dirCima.x(), dirCima.y());
    if (len > 0.001) dirCima /= len;
    const QPointF alcaRot = meioTopo + dirCima * 22.0;

    m_grabIni = paraJogo(pos);
    m_iniX = m_def.x; m_iniY = m_def.y;
    m_iniSX = m_def.scaleX; m_iniSY = m_def.scaleY;
    m_iniAng = m_def.angle;

    if (perto(alcaRot))        m_grab = Grab::Rotate;
    else if (perto(cantoBR))   m_grab = Grab::Scale;
    else if (quad.containsPoint(pos, Qt::OddEvenFill)) m_grab = Grab::Move;
    else m_grab = Grab::None;

    if (m_grab != Grab::None) setCursor(Qt::ClosedHandCursor);
}

void PictureStageView::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_interactive) return;
    if (m_grab == Grab::None) return;
    const core::PictureAsset* a = asset();
    if (!a && !m_def.rich.enabled) return;
    const QSizeF baseArrasto = !m_tamBase.isEmpty() ? m_tamBase
                                                      : (m_def.rich.enabled ? QSizeF() : QSizeF(a->image.size()));
    const QPointF agora = paraJogo(e->position());
    const QPointF d = agora - m_grabIni;

    if (m_grab == Grab::Move) {
        double x = m_iniX + d.x();
        double y = m_iniY + d.y();
        m_guideX = m_guideY = false;
        if (m_snapEnabled && !(e->modifiers() & Qt::AltModifier)) {
            QVector<double> xs{0.0, ed.gameResolution.width() / 2.0,
                               double(ed.gameResolution.width())};
            QVector<double> ys{0.0, ed.gameResolution.height() / 2.0,
                               double(ed.gameResolution.height())};
            for (const core::PictureDef& ref : std::as_const(m_references)) {
                xs.push_back(ref.x);
                ys.push_back(ref.y);
            }
            const double limite = 8.0;
            double melhorX = limite + 1.0, melhorY = limite + 1.0;
            for (double alvo : std::as_const(xs)) {
                const double distancia = qAbs(x - alvo);
                if (distancia <= limite && distancia < melhorX) {
                    melhorX = distancia; x = alvo;
                    m_guideX = true; m_guideXValue = alvo;
                }
            }
            for (double alvo : std::as_const(ys)) {
                const double distancia = qAbs(y - alvo);
                if (distancia <= limite && distancia < melhorY) {
                    melhorY = distancia; y = alvo;
                    m_guideY = true; m_guideYValue = alvo;
                }
            }
        }
        m_def.x = qRound(x);        // pixel inteiro: pixel art
        m_def.y = qRound(y);
    } else if (m_grab == Grab::Scale) {
        // O ponto do mouse volta para o espaço LOCAL da imagem (desfazendo a
        // rotação em torno da âncora) e a escala sai direto do tamanho que o
        // canto deveria ter. Medir nos eixos da TELA parece funcionar até a
        // imagem estar girada: a 72° a distância horizontal até a âncora é
        // quase zero, e qualquer arrasto virava 2000% de escala.
        const QPointF anc(m_def.x, m_def.y);
        QTransform desgira;
        desgira.rotate(-m_def.angle);
        QPointF local = desgira.map(agora - anc);
        // O RuntimePictureTransform representa flip como escala assinada.
        // Durante o drag mantemos scaleX/scaleY positivos e desfazemos o
        // sinal visual aqui, para o handle continuar coerente com o mesmo
        // pivot/âncora usado no runtime.
        if (m_def.flipH) local.setX(-local.x());
        if (m_def.flipV) local.setY(-local.y());
        // Distância da âncora até o canto inferior direito, em pixels da
        // imagem sem escala. Se a âncora estiver EM cima do canto, aquele eixo
        // não dá para escalar (divisão por zero) e fica como estava.
        const QPointF ancF = core::pictureAnchorFactor(m_def.anchor, m_def.anchorX, m_def.anchorY);
        const double baseX = (1.0 - ancF.x()) * baseArrasto.width();
        const double baseY = (1.0 - ancF.y()) * baseArrasto.height();
        double sx = m_iniSX, sy = m_iniSY;
        if (baseX > 0.5) sx = qBound(1.0, local.x() / baseX * 100.0, 2000.0);
        if (baseY > 0.5) sy = qBound(1.0, local.y() / baseY * 100.0, 2000.0);
        if (e->modifiers() & Qt::ShiftModifier) {   // Shift = proporcional
            const double f = (sx / qMax(1.0, m_iniSX) + sy / qMax(1.0, m_iniSY)) / 2.0;
            sx = m_iniSX * f;
            sy = m_iniSY * f;
        }
        m_def.scaleX = qRound(sx);
        m_def.scaleY = qRound(sy);
    } else if (m_grab == Grab::Rotate) {
        const QPointF anc(m_def.x, m_def.y);
        const double a0 = std::atan2(m_grabIni.y() - anc.y(), m_grabIni.x() - anc.x());
        const double a1 = std::atan2(agora.y() - anc.y(), agora.x() - anc.x());
        double ang = m_iniAng + (a1 - a0) * 180.0 / M_PI;
        if (e->modifiers() & Qt::ShiftModifier) ang = qRound(ang / 15.0) * 15.0;
        m_def.angle = qRound(ang * 10.0) / 10.0;
    }
    emit defChanged();
    update();
}

void PictureStageView::mouseReleaseEvent(QMouseEvent*)
{
    if (!m_interactive) return;
    m_grab = Grab::None;
    m_guideX = m_guideY = false;
    setCursor(Qt::OpenHandCursor);
    update();
}

// ------------------------------------------------------- comandos de imagem
bool editPictureWorldCommand(core::Editor& editor, core::EventCommand& command, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Imagem"));
    auto* root=new QVBoxLayout(&dialog);auto* form=new QFormLayout;root->addLayout(form);
    const QString type=command.type;const QVariantMap old=command.params;
    QSpinBox* number=nullptr;QCheckBox* useName=nullptr;QLineEdit* logicalName=nullptr;
    const bool needsPicture=type!=QLatin1String("picture.eraseGroup")&&type!=QLatin1String("picture.moveGroup")&&type!=QLatin1String("picture.timeline.define");
    if(needsPicture){
        number=new QSpinBox(&dialog);number->setRange(1,100);number->setValue(old.value(QStringLiteral("number"),1).toInt());
        useName=new QCheckBox(QObject::tr("Identificar a imagem por nome"),&dialog);
        logicalName=new QLineEdit(old.value(QStringLiteral("logicalName")).toString(),&dialog);
        logicalName->setPlaceholderText(QObject::tr("Ex.: retrato-heroi"));
        useName->setChecked(!logicalName->text().isEmpty()&&!old.contains(QStringLiteral("number")));
        form->addRow(QObject::tr("Número da imagem:"),number);form->addRow(useName);form->addRow(QObject::tr("Nome da imagem:"),logicalName);
        QObject::connect(useName,&QCheckBox::toggled,number,&QWidget::setDisabled);
        number->setDisabled(useName->isChecked());
    }
    QLineEdit* group=nullptr;
    if(type==QLatin1String("picture.setGroup")||type==QLatin1String("picture.eraseGroup")||type==QLatin1String("picture.moveGroup")){
        group=new QLineEdit(old.value(QStringLiteral("group")).toString(),&dialog);group->setPlaceholderText(QObject::tr("Ex.: hud"));form->addRow(QObject::tr("Grupo:"),group);
    }
    QLineEdit* target=nullptr;QComboBox* axis=nullptr;QDoubleSpinBox* offsetX=nullptr;QDoubleSpinBox* offsetY=nullptr;
    QCheckBox* inheritRotation=nullptr;QCheckBox* inheritScale=nullptr;QCheckBox* inheritOpacity=nullptr;
    if(type==QLatin1String("picture.attach")){
        dialog.setWindowTitle(QObject::tr("Fazer imagem seguir um alvo"));
        target=new QLineEdit(old.value(QStringLiteral("target"),QStringLiteral("player")).toString(),&dialog);
        target->setPlaceholderText(QObject::tr("player, event:ID ou picture:nome"));form->addRow(QObject::tr("Alvo:"),target);
        axis=new QComboBox(&dialog);axis->addItem(QObject::tr("X e Y"),QStringLiteral("both"));axis->addItem(QObject::tr("Somente X"),QStringLiteral("x"));axis->addItem(QObject::tr("Somente Y"),QStringLiteral("y"));axis->setCurrentIndex(qMax(0,axis->findData(old.value(QStringLiteral("followAxis"),QStringLiteral("both")))));form->addRow(QObject::tr("Acompanhar movimento:"),axis);
        auto offset=[&](const QString& key){auto* spin=new QDoubleSpinBox(&dialog);spin->setRange(-4000,4000);spin->setDecimals(1);spin->setSuffix(QStringLiteral(" px"));spin->setValue(old.value(key).toDouble());return spin;};
        offsetX=offset(QStringLiteral("offsetX"));offsetY=offset(QStringLiteral("offsetY"));form->addRow(QObject::tr("Deslocamento X:"),offsetX);form->addRow(QObject::tr("Deslocamento Y:"),offsetY);
        inheritRotation=new QCheckBox(QObject::tr("Herdar rotação"),&dialog);inheritScale=new QCheckBox(QObject::tr("Herdar escala"),&dialog);inheritOpacity=new QCheckBox(QObject::tr("Herdar opacidade"),&dialog);
        inheritRotation->setChecked(old.value(QStringLiteral("inheritRotation")).toBool());inheritScale->setChecked(old.value(QStringLiteral("inheritScale")).toBool());inheritOpacity->setChecked(old.value(QStringLiteral("inheritOpacity")).toBool());form->addRow(inheritRotation);form->addRow(inheritScale);form->addRow(inheritOpacity);
    }
    QLineEdit* timelineName=nullptr;QSpinBox* timelineDuration=nullptr;QCheckBox* timelineLoop=nullptr,*timelineWait=nullptr;QTableWidget* keyframes=nullptr;
    if(type.startsWith(QLatin1String("picture.timeline."))){timelineName=new QLineEdit(old.value(QStringLiteral("name")).toString(),&dialog);timelineName->setPlaceholderText(QObject::tr("Ex.: retrato-entrada"));form->addRow(QObject::tr("Animação:"),timelineName);
        if(type==QLatin1String("picture.timeline.define")){dialog.setWindowTitle(QObject::tr("Criar animação da imagem"));timelineDuration=new QSpinBox(&dialog);timelineDuration->setRange(1,36000);timelineDuration->setSuffix(QObject::tr(" quadros"));timelineDuration->setValue(old.value(QStringLiteral("duration"),60).toInt());form->addRow(QObject::tr("Duração:"),timelineDuration);timelineLoop=new QCheckBox(QObject::tr("Repetir continuamente"),&dialog);timelineLoop->setChecked(old.value(QStringLiteral("loop")).toBool());form->addRow(timelineLoop);keyframes=new QTableWidget(&dialog);keyframes->setColumnCount(8);keyframes->setHorizontalHeaderLabels({QObject::tr("Tempo"),QStringLiteral("X"),QStringLiteral("Y"),QObject::tr("Opacidade"),QObject::tr("Escala X"),QObject::tr("Escala Y"),QObject::tr("Rotação"),QStringLiteral("Tint")});keyframes->horizontalHeader()->setStretchLastSection(true);keyframes->setMinimumHeight(210);QVariantList saved=old.value(QStringLiteral("keyframes")).toList();if(saved.isEmpty()){const QJsonDocument legacy=QJsonDocument::fromJson(old.value(QStringLiteral("keyframes_json")).toString().toUtf8());if(legacy.isArray())saved=legacy.array().toVariantList();}auto addFrame=[&](const QVariantMap& frame){const int row=keyframes->rowCount();keyframes->insertRow(row);const QStringList keys{QStringLiteral("time"),QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("opacity"),QStringLiteral("scaleX"),QStringLiteral("scaleY"),QStringLiteral("rotation"),QStringLiteral("tint")};for(int col=0;col<keys.size();++col){const QString text=frame.contains(keys[col])?frame.value(keys[col]).toString():QString();keyframes->setItem(row,col,new QTableWidgetItem(text));}};for(const QVariant& value:saved)addFrame(value.toMap());if(keyframes->rowCount()==0){addFrame({{QStringLiteral("time"),0},{QStringLiteral("x"),0},{QStringLiteral("y"),0},{QStringLiteral("opacity"),255}});addFrame({{QStringLiteral("time"),timelineDuration->value()},{QStringLiteral("x"),300},{QStringLiteral("y"),0},{QStringLiteral("opacity"),255}});}auto* frameBox=new QWidget(&dialog);auto* frameLayout=new QVBoxLayout(frameBox);frameLayout->setContentsMargins(0,0,0,0);frameLayout->addWidget(keyframes);auto* frameButtons=new QHBoxLayout;auto* add=new QPushButton(QObject::tr("Adicionar quadro-chave"),frameBox);auto* remove=new QPushButton(QObject::tr("Remover selecionado"),frameBox);frameButtons->addWidget(add);frameButtons->addWidget(remove);frameButtons->addStretch();frameLayout->addLayout(frameButtons);QObject::connect(add,&QPushButton::clicked,&dialog,[&,addFrame]{addFrame({{QStringLiteral("time"),timelineDuration->value()}});});QObject::connect(remove,&QPushButton::clicked,&dialog,[&]{const int row=keyframes->currentRow();if(row>=0)keyframes->removeRow(row);});form->addRow(QObject::tr("Quadros-chave:"),frameBox);}else if(type==QLatin1String("picture.timeline.play")){dialog.setWindowTitle(QObject::tr("Reproduzir animação"));timelineWait=new QCheckBox(QObject::tr("Esperar a animação terminar"),&dialog);timelineWait->setChecked(old.value(QStringLiteral("wait"),true).toBool());form->addRow(timelineWait);}else dialog.setWindowTitle(QObject::tr("Parar animação"));}
    QComboBox* commonEvent=nullptr;
    if(type==QLatin1String("picture.onClick")||type==QLatin1String("picture.onTouch")){dialog.setWindowTitle(type.endsWith(QLatin1String("onClick"))?QObject::tr("Ao clicar na imagem"):QObject::tr("Ao tocar na imagem"));commonEvent=new QComboBox(&dialog);commonEvent->addItem(QObject::tr("Nenhum (remover vínculo)"),QString());for(const auto& event:editor.commonEvents)commonEvent->addItem(event.name,event.id);commonEvent->setCurrentIndex(qMax(0,commonEvent->findData(old.value(QStringLiteral("commonEventId")))));form->addRow(QObject::tr("Evento Comum:"),commonEvent);}
    QVector<QCheckBox*> enabled;QVector<QDoubleSpinBox*> values;QVector<QString> keys;
    QSpinBox* duration=nullptr;QComboBox* ease=nullptr;QCheckBox* wait=nullptr;
    if(type==QLatin1String("picture.moveGroup")){
        dialog.setWindowTitle(QObject::tr("Mover grupo de imagens"));
        const struct Field{const char* key;const char* label;double min,max,def;}fields[]={{"x","X",-4000,4000,0},{"y","Y",-4000,4000,0},{"scaleX","Escala X",1,2000,100},{"scaleY","Escala Y",1,2000,100},{"opacity","Opacidade",0,255,255},{"angle","Ângulo",-3600,3600,0}};
        for(const Field& field:fields){auto* row=new QWidget(&dialog);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);auto* check=new QCheckBox(QObject::tr("Alterar"),row);auto* spin=new QDoubleSpinBox(row);spin->setRange(field.min,field.max);spin->setValue(old.value(QString::fromLatin1(field.key),field.def).toDouble());check->setChecked(old.contains(QString::fromLatin1(field.key)));spin->setEnabled(check->isChecked());QObject::connect(check,&QCheckBox::toggled,spin,&QWidget::setEnabled);layout->addWidget(check);layout->addWidget(spin,1);form->addRow(QObject::tr(field.label),row);enabled.push_back(check);values.push_back(spin);keys.push_back(QString::fromLatin1(field.key));}
        duration=new QSpinBox(&dialog);duration->setRange(0,3600);duration->setSuffix(QObject::tr(" quadros"));duration->setValue(old.value(QStringLiteral("duration"),30).toInt());form->addRow(QObject::tr("Duração:"),duration);
        ease=new QComboBox(&dialog);for(int i=0;i<=int(core::PictureEase::Elastic);++i){const auto value=core::PictureEase(i);ease->addItem(core::pictureEaseLabel(value),core::pictureEaseId(value));}ease->setCurrentIndex(qMax(0,ease->findData(old.value(QStringLiteral("ease"),QStringLiteral("linear")))));form->addRow(QObject::tr("Curva:"),ease);
        wait=new QCheckBox(QObject::tr("Esperar o grupo terminar"),&dialog);wait->setChecked(old.value(QStringLiteral("wait"),false).toBool());form->addRow(wait);
    }
    if(type==QLatin1String("picture.setGroup"))dialog.setWindowTitle(QObject::tr("Definir grupo da imagem"));
    else if(type==QLatin1String("picture.eraseGroup"))dialog.setWindowTitle(QObject::tr("Apagar grupo de imagens"));
    else if(type==QLatin1String("picture.detach"))dialog.setWindowTitle(QObject::tr("Parar de seguir o alvo"));
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);root->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        QVariantMap params;
        if(needsPicture){if(useName->isChecked()){if(logicalName->text().trimmed().isEmpty()){QMessageBox::warning(&dialog,QObject::tr("Nome obrigatório"),QObject::tr("Informe um nome para identificar a imagem."));return;}params[QStringLiteral("logicalName")]=logicalName->text().trimmed();}else params[QStringLiteral("number")]=number->value();}
        if(group){if(group->text().trimmed().isEmpty()){QMessageBox::warning(&dialog,QObject::tr("Grupo obrigatório"),QObject::tr("Informe o nome do grupo."));return;}params[QStringLiteral("group")]=group->text().trimmed();}
        if(target){if(target->text().trimmed().isEmpty()){QMessageBox::warning(&dialog,QObject::tr("Alvo obrigatório"),QObject::tr("Informe player, event:ID ou picture:nome."));return;}params[QStringLiteral("target")]=target->text().trimmed();params[QStringLiteral("followAxis")]=axis->currentData().toString();params[QStringLiteral("offsetX")]=offsetX->value();params[QStringLiteral("offsetY")]=offsetY->value();params[QStringLiteral("inheritRotation")]=inheritRotation->isChecked();params[QStringLiteral("inheritScale")]=inheritScale->isChecked();params[QStringLiteral("inheritOpacity")]=inheritOpacity->isChecked();}
        if(timelineName){if(timelineName->text().trimmed().isEmpty()){QMessageBox::warning(&dialog,QObject::tr("Nome da animação obrigatório"),QObject::tr("Informe um nome para a animação."));return;}params[QStringLiteral("name")]=timelineName->text().trimmed();if(timelineDuration){QVariantList frames;const QStringList keys{QStringLiteral("time"),QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("opacity"),QStringLiteral("scaleX"),QStringLiteral("scaleY"),QStringLiteral("rotation"),QStringLiteral("tint")};for(int row=0;row<keyframes->rowCount();++row){QVariantMap frame;for(int col=0;col<keys.size();++col){const QString value=keyframes->item(row,col)?keyframes->item(row,col)->text().trimmed():QString();if(value.isEmpty())continue;if(keys[col]==QLatin1String("tint")){if(!QColor(value).isValid()){QMessageBox::warning(&dialog,QObject::tr("Cor inválida"),QObject::tr("Use uma cor como #RRGGBB ou #AARRGGBB."));return;}frame[keys[col]]=value;}else{bool ok=false;const double number=value.toDouble(&ok);if(!ok){QMessageBox::warning(&dialog,QObject::tr("Quadro-chave inválido"),QObject::tr("Os campos numéricos precisam conter números válidos."));return;}frame[keys[col]]=number;}}if(!frame.contains(QStringLiteral("time"))){QMessageBox::warning(&dialog,QObject::tr("Tempo obrigatório"),QObject::tr("Todo quadro-chave precisa informar em que momento da animação acontece."));return;}frames.push_back(frame);}if(frames.isEmpty()){QMessageBox::warning(&dialog,QObject::tr("Quadros-chave obrigatórios"),QObject::tr("Adicione pelo menos um quadro-chave."));return;}params[QStringLiteral("duration")]=timelineDuration->value();params[QStringLiteral("loop")]=timelineLoop->isChecked();params[QStringLiteral("keyframes")]=frames;}if(timelineWait)params[QStringLiteral("wait")]=timelineWait->isChecked();}
        if(commonEvent)params[QStringLiteral("commonEventId")]=commonEvent->currentData().toString();
        for(int i=0;i<enabled.size();++i)if(enabled.at(i)->isChecked())params[keys.at(i)]=values.at(i)->value();
        if(duration){params[QStringLiteral("duration")]=duration->value();params[QStringLiteral("ease")]=ease->currentData().toString();params[QStringLiteral("wait")]=wait->isChecked();}
        command.params=params;dialog.accept();
    });
    dialog.resize(560,dialog.sizeHint().height());
    return dialog.exec()==QDialog::Accepted;
}

PictureCommandDialog::PictureCommandDialog(core::Editor& editorRef, const QString& tipo,
                                           core::EventCommand& cmd, QWidget* parent)
    : QDialog(parent), ed(editorRef), m_tipo(tipo), m_cmd(cmd)
{
    m_def = core::PictureDef::fromParams(cmd.params);
    if (m_def.assetId.isEmpty() && !ed.pictures.isEmpty())
        m_def.assetId = ed.pictures.first().id;

    if (tipo == QLatin1String("picture.text")) {
        m_def.rich = core::PictureRichText::fromParams(
            cmd.params.value(QStringLiteral("rich")).toMap());
        m_def.rich.enabled = true;
        montarShow();
    }
    else if (tipo == QLatin1String("picture.show") || tipo == QLatin1String("picture.showByName")) montarShow();
    else if (tipo == QLatin1String("picture.move")) montarMove();
    else if (tipo == QLatin1String("picture.zoomIn")) montarZoom(true);
    else if (tipo == QLatin1String("picture.zoomOut")) montarZoom(false);
    else                                            montarSimples();
}

void PictureCommandDialog::montarShow()
{
    setWindowTitle(m_tipo == QLatin1String("picture.text") ? tr("Mostrar texto como imagem")
        : m_tipo == QLatin1String("picture.showByName") ? tr("Mostrar imagem por nome lógico") : tr("Mostrar imagem"));
    auto* raiz = new QHBoxLayout(this);

    m_stage = new PictureStageView(ed, m_def, this);
    auto* previewDialog = new CommandPreviewDialog(tr("Prévia da imagem"), m_stage, this);
    previewDialog->resize(940, 700);

    auto* col = new QVBoxLayout;
    col->setSpacing(6);
    auto* form = new QFormLayout;

    m_number = new QSpinBox(this);
    m_number->setRange(1, 100);
    m_number->setValue(m_def.number);
    form->addRow(tr("Número (slot):"), m_number);
    m_logicalName=new QLineEdit(m_cmd.params.value(QStringLiteral("logicalName")).toString(),this);
    m_logicalName->setPlaceholderText(tr("Ex.: retrato-heroi"));
    m_pictureGroup=new QLineEdit(m_cmd.params.value(QStringLiteral("group")).toString(),this);
    m_pictureGroup->setPlaceholderText(tr("Ex.: hud ou retratos"));
    form->addRow(tr("Nome lógico:"),m_logicalName);
    form->addRow(tr("Grupo:"),m_pictureGroup);

    m_asset = new QComboBox(this);
    for (const core::PictureAsset& a : ed.pictures)
        m_asset->addItem(a.name, a.id);
    m_asset->setCurrentIndex(qMax(0, ed.pictureIndexById(m_def.assetId)));
    form->addRow(tr("Imagem:"), m_asset);

    auto* bLib = new QPushButton(tr("Selecionar imagem…"), this);
    form->addRow(QString(), bLib);

    m_spritesheet = new QCheckBox(tr("Spritesheet / imagem em sequência"), this);
    m_spritesheet->setChecked(m_def.animated || m_def.frameCount > 1);
    form->addRow(QString(), m_spritesheet);
    m_frameCount = new QSpinBox(this);m_frameCount->setRange(1, 999);m_frameCount->setValue(qMax(1, m_def.frameCount));
    m_frameColumns = new QSpinBox(this);m_frameColumns->setRange(1, 999);m_frameColumns->setValue(qMax(1, m_def.frameColumns));
    m_frameRows = new QSpinBox(this);m_frameRows->setRange(1, 999);m_frameRows->setValue(qMax(1, m_def.frameRows));
    m_frameIndex = new QSpinBox(this);m_frameIndex->setRange(0, 998);m_frameIndex->setValue(qBound(0, m_def.frameIndex, qMax(1, m_def.frameCount) - 1));
    m_frameFps = new QDoubleSpinBox(this);m_frameFps->setRange(0.0, 120.0);m_frameFps->setDecimals(2);m_frameFps->setValue(m_def.frameFps);m_frameFps->setSuffix(tr(" fps"));
    m_frameLoop = new QCheckBox(tr("Loop"), this);m_frameLoop->setChecked(m_def.frameLoop);
    m_framePlaying = new QCheckBox(tr("Reproduzir animação"), this);m_framePlaying->setChecked(m_def.framePlaying);
    form->addRow(tr("Frames:"), m_frameCount);
    form->addRow(tr("Colunas:"), m_frameColumns);
    form->addRow(tr("Linhas:"), m_frameRows);
    form->addRow(tr("Frame inicial/fixo:"), m_frameIndex);
    form->addRow(tr("Velocidade:"), m_frameFps);
    form->addRow(m_frameLoop);
    form->addRow(m_framePlaying);
    m_frameValidation = new QLabel(this);
    m_frameValidation->setWordWrap(true);
    m_frameValidation->setObjectName(QStringLiteral("pictureFrameValidation"));
    form->addRow(tr("Validação:"), m_frameValidation);

    const QVector<PictureReferenceEntry> referenceEntries = pictureReferences(ed, &m_cmd);
    auto selectedReferences = std::make_shared<QVector<bool>>(referenceEntries.size(), false);
    auto* bReferences = new QPushButton(tr("Imagens de referência…"), this);
    bReferences->setEnabled(!referenceEntries.isEmpty());
    bReferences->setToolTip(tr("Mostra outras pictures no palco como mesa de luz. "
                               "Elas não são gravadas neste comando."));
    form->addRow(tr("Referências:"), bReferences);

    auto grau = [this](double min, double max, double val, const QString& sufixo) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(min, max);
        s->setDecimals(1);
        s->setValue(val);
        if (!sufixo.isEmpty()) s->setSuffix(sufixo);
        return s;
    };
    m_x  = grau(-4000, 4000, m_def.x, QStringLiteral(" px"));
    m_y  = grau(-4000, 4000, m_def.y, QStringLiteral(" px"));
    m_sx = grau(1, 2000, m_def.scaleX, QStringLiteral(" %"));
    m_sy = grau(1, 2000, m_def.scaleY, QStringLiteral(" %"));
    m_op = grau(0, 255, m_def.opacity, QString());
    m_ang = grau(-3600, 3600, m_def.angle, QStringLiteral("°"));
    m_positionPreset = new QComboBox(this);
    m_positionPreset->addItem(tr("Personalizada"), -1);
    m_positionPreset->addItem(tr("Superior esquerdo"), 0);
    m_positionPreset->addItem(tr("Superior centro"), 1);
    m_positionPreset->addItem(tr("Superior direito"), 2);
    m_positionPreset->addItem(tr("Centro esquerdo"), 3);
    m_positionPreset->addItem(tr("Centro"), 4);
    m_positionPreset->addItem(tr("Centro direito"), 5);
    m_positionPreset->addItem(tr("Inferior esquerdo"), 6);
    m_positionPreset->addItem(tr("Inferior centro"), 7);
    m_positionPreset->addItem(tr("Inferior direito"), 8);
    int detectedPreset = -1;
    const double w = ed.gameResolution.width(), h = ed.gameResolution.height();
    const QVector<QPointF> presetPoints{{0,0}, {w/2,0}, {w,0}, {0,h/2}, {w/2,h/2},
                                        {w,h/2}, {0,h}, {w/2,h}, {w,h}};
    for (int i = 0; i < presetPoints.size(); ++i) {
        if (m_def.anchor == core::PictureAnchor(i) &&
            qFuzzyCompare(m_def.x + 1.0, presetPoints[i].x() + 1.0) &&
            qFuzzyCompare(m_def.y + 1.0, presetPoints[i].y() + 1.0)) {
            detectedPreset = i;
            break;
        }
    }
    m_positionPreset->setCurrentIndex(detectedPreset + 1);
    form->addRow(tr("Posição pronta:"), m_positionPreset);
    form->addRow(tr("X:"), m_x);
    form->addRow(tr("Y:"), m_y);
    form->addRow(tr("Escala X:"), m_sx);
    form->addRow(tr("Escala Y:"), m_sy);
    form->addRow(tr("Opacidade (0..255):"), m_op);
    form->addRow(tr("Ângulo:"), m_ang);
    auto expressionText=[this](const QString& key){const QVariant value=m_cmd.params.value(key);const QVariantMap spec=value.toMap();return spec.value(QStringLiteral("source")).toString()==QLatin1String("expression")?spec.value(QStringLiteral("expression")).toString():(value.metaType().id()==QMetaType::QString?value.toString():QString());};
    m_dynamicX=new QLineEdit(expressionText(QStringLiteral("x")),this);
    m_dynamicY=new QLineEdit(expressionText(QStringLiteral("y")),this);
    m_dynamicOpacity=new QLineEdit(expressionText(QStringLiteral("opacity")),this);
    for(QLineEdit* field:{m_dynamicX,m_dynamicY,m_dynamicOpacity})field->setPlaceholderText(tr("Opcional: v[5], gv(\"player.screenX\"), party.gold()…"));
    form->addRow(tr("X dinâmico:"),m_dynamicX);
    form->addRow(tr("Y dinâmico:"),m_dynamicY);
    form->addRow(tr("Opacidade dinâmica:"),m_dynamicOpacity);

    m_anchor = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureAnchor::Custom); ++i) {
        const auto a = core::PictureAnchor(i);
        m_anchor->addItem(core::pictureAnchorLabel(a), core::pictureAnchorId(a));
    }
    m_anchor->setCurrentIndex(int(m_def.anchor));
    form->addRow(tr("Âncora:"), m_anchor);

    m_space = new QComboBox(this);
    m_space->addItem(core::pictureSpaceLabel(core::PictureSpace::Screen), QStringLiteral("screen"));
    m_space->addItem(core::pictureSpaceLabel(core::PictureSpace::Map), QStringLiteral("map"));
    m_space->setCurrentIndex(m_def.space == core::PictureSpace::Map ? 1 : 0);
    form->addRow(tr("Posição medida em:"), m_space);

    m_layer = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureLayer::AboveAll); ++i) {
        const auto l = core::PictureLayer(i);
        m_layer->addItem(core::pictureLayerLabel(l), core::pictureLayerId(l));
    }
    m_layer->setCurrentIndex(int(m_def.layer));
    form->addRow(tr("Camada:"), m_layer);

    m_blend = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureBlend::Screen); ++i) {
        const auto b = core::PictureBlend(i);
        m_blend->addItem(core::pictureBlendLabel(b), core::pictureBlendId(b));
    }
    m_blend->setCurrentIndex(int(m_def.blend));
    form->addRow(tr("Mistura:"), m_blend);

    m_smooth = new QCheckBox(tr("Suavização bilinear ao redimensionar"), this);
    m_smooth->setChecked(m_def.smooth);
    m_smooth->setToolTip(tr("Suaviza a imagem ao ampliar ou reduzir. Desative para manter pixels bem definidos em pixel art."));
    form->addRow(QString(), m_smooth);
    m_flipH = new QCheckBox(tr("Virar horizontalmente"), this);m_flipH->setChecked(m_def.flipH);
    m_flipV = new QCheckBox(tr("Virar verticalmente"), this);m_flipV->setChecked(m_def.flipV);
    form->addRow(QString(), m_flipH);form->addRow(QString(), m_flipV);
    m_duringBattle = new QCheckBox(tr("Mostrar durante batalha"), this);m_duringBattle->setChecked(m_def.duringBattle);
    m_eraseOnMapChange = new QCheckBox(tr("Apagar ao trocar de mapa"), this);m_eraseOnMapChange->setChecked(m_def.eraseOnMapChange);
    m_affectedByTone = new QCheckBox(tr("Afetada pela tonalidade da tela"), this);m_affectedByTone->setChecked(m_def.affectedByTone);
    form->addRow(QString(), m_duringBattle);form->addRow(QString(), m_eraseOnMapChange);form->addRow(QString(), m_affectedByTone);
    auto* snap = new QCheckBox(tr("Snap horizontal/vertical ao arrastar"), this);
    snap->setChecked(true);
    snap->setToolTip(tr("Alinha ao centro, às bordas e às referências. Segure Alt para ignorar."));
    form->addRow(QString(), snap);
    col->addLayout(form);

    // ---- física ----------------------------------------------------------
    auto* gb = new QGroupBox(tr("Movimento automático"), this);
    auto* gf = new QFormLayout(gb);
    auto fisica = [this](double val, double max, const QString& sufixo) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(-max, max);
        s->setDecimals(2);
        s->setValue(val);
        s->setSuffix(sufixo);
        return s;
    };
    m_floatSpeed = fisica(m_def.floatSpeed, 20, tr(" ciclos/s"));
    m_floatRange = fisica(m_def.floatRange, 500, QStringLiteral(" px"));
    m_swaySpeed  = fisica(m_def.swaySpeed, 20, tr(" ciclos/s"));
    m_swayRange  = fisica(m_def.swayRange, 500, QStringLiteral(" px"));
    m_spinSpeed  = fisica(m_def.spinSpeed, 3600, tr(" °/s"));
    m_pulseSpeed = fisica(m_def.pulseSpeed, 20, tr(" ciclos/s"));
    m_pulseRange = fisica(m_def.pulseRange, 200, tr(" % "));
    gf->addRow(tr("Flutuar (vel.):"), m_floatSpeed);
    gf->addRow(tr("Flutuar (alcance):"), m_floatRange);
    gf->addRow(tr("Balançar (vel.):"), m_swaySpeed);
    gf->addRow(tr("Balançar (alcance):"), m_swayRange);
    gf->addRow(tr("Girar:"), m_spinSpeed);
    gf->addRow(tr("Breathing / Respirar (vel.):"), m_pulseSpeed);
    gf->addRow(tr("Breathing / Respirar (escala):"), m_pulseRange);
    auto* animar = new QCheckBox(QStringLiteral("▶"), gb);
    animar->setToolTip(tr("Animar pré-visualização"));
    gf->addRow(tr("Prévia:"), animar);
    col->addWidget(gb);

    // ---- 9-slice da própria Picture -------------------------------------
    // Diferente da borda decorativa em Efeitos: aqui a imagem inteira vira
    // um painel redimensionável, fundação que o Bloco F poderá reutilizar.
    auto* g9 = new QGroupBox(tr("Imagem redimensionável (9-slice)"), this);
    auto* f9 = new QFormLayout(g9);
    m_nineSlice = new QCheckBox(tr("Usar 9-slice"), g9);
    m_nineSlice->setChecked(m_def.nineSlice.enabled);
    f9->addRow(m_nineSlice);
    auto nineInt = [this](int value, int min, int max, const QString& suffix) {
        auto* w = new QSpinBox(this);
        w->setRange(min, max);
        w->setValue(value);
        if (!suffix.isEmpty()) w->setSuffix(suffix);
        return w;
    };
    m_nineWidth = nineInt(m_def.nineSlice.width, 0, 8192, QStringLiteral(" px"));
    m_nineHeight = nineInt(m_def.nineSlice.height, 0, 8192, QStringLiteral(" px"));
    m_nineWidth->setSpecialValueText(tr("Original"));
    m_nineHeight->setSpecialValueText(tr("Original"));
    m_nineLeft = nineInt(m_def.nineSlice.left, 0, 2048, QStringLiteral(" px"));
    m_nineTop = nineInt(m_def.nineSlice.top, 0, 2048, QStringLiteral(" px"));
    m_nineRight = nineInt(m_def.nineSlice.right, 0, 2048, QStringLiteral(" px"));
    m_nineBottom = nineInt(m_def.nineSlice.bottom, 0, 2048, QStringLiteral(" px"));
    f9->addRow(tr("Largura final:"), m_nineWidth);
    f9->addRow(tr("Altura final:"), m_nineHeight);
    f9->addRow(tr("Corte esquerdo:"), m_nineLeft);
    f9->addRow(tr("Corte superior:"), m_nineTop);
    f9->addRow(tr("Corte direito:"), m_nineRight);
    f9->addRow(tr("Corte inferior:"), m_nineBottom);
    m_nineEdgeMode = new QComboBox(g9);
    m_nineCenterMode = new QComboBox(g9);
    for (int i = 0; i <= int(core::PictureNineSliceMode::Tile); ++i) {
        const auto mode = core::PictureNineSliceMode(i);
        m_nineEdgeMode->addItem(core::pictureNineSliceModeLabel(mode), core::pictureNineSliceModeId(mode));
        m_nineCenterMode->addItem(core::pictureNineSliceModeLabel(mode), core::pictureNineSliceModeId(mode));
    }
    m_nineEdgeMode->setCurrentIndex(int(m_def.nineSlice.edgeMode));
    m_nineCenterMode->setCurrentIndex(int(m_def.nineSlice.centerMode));
    f9->addRow(tr("Bordas:"), m_nineEdgeMode);
    f9->addRow(tr("Centro:"), m_nineCenterMode);
    auto* nineHint = new QLabel(tr("Os quatro cantos ficam intactos. Bordas e centro podem ser esticados ou repetidos."), g9);
    nineHint->setWordWrap(true);
    nineHint->setStyleSheet(QStringLiteral("color:#888;"));
    f9->addRow(nineHint);
    col->addWidget(g9);

    auto* ensaiar = new QCheckBox(tr("▶ Ensaiar as transições (entra, segura, sai)"), this);
    ensaiar->setToolTip(tr("Roda a transição de entrada e a de saída em laço, "
                           "com o mesmo código do jogo."));
    col->addWidget(ensaiar);
    connect(ensaiar, &QCheckBox::toggled, this, [this, animar](bool on) {
        if (on) animar->setChecked(true);
        m_stage->playTransitions(on);
    });

    auto* dica = new QLabel(
        tr("Arraste a imagem no palco para mover · quadrado do canto = escala "
           "(Shift mantém a proporção) · círculo de cima = girar (Shift trava em 15°)."), this);
    dica->setWordWrap(true);
    dica->setStyleSheet(QStringLiteral("color:#888;"));
    col->addWidget(dica);
    col->addStretch(1);

    // Os efeitos ficam numa aba ao lado: o formulário inteiro numa coluna só
    // não caberia em tela de notebook.
    auto* abas = new QTabWidget(this);
    auto* pagTransform = new QWidget(abas);
    pagTransform->setLayout(col);
    auto* rolTransform = new QScrollArea(abas);
    rolTransform->setWidgetResizable(true);
    rolTransform->setFrameShape(QFrame::NoFrame);
    rolTransform->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    rolTransform->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    rolTransform->setWidget(pagTransform);
    abas->addTab(rolTransform, tr("Imagem"));

    if (m_tipo == QLatin1String("picture.text")) {
        // Texto rico: a imagem não vem da biblioteca, é desenhada a partir
        // do que o autor escrever — então a escolha de imagem some.
        m_asset->setEnabled(false);
        m_textPanel = new PictureTextPanel(ed, m_def.rich, abas);
        auto* rolTexto = new QScrollArea(abas);
        rolTexto->setWidgetResizable(true);
        rolTexto->setWidget(m_textPanel);
        abas->insertTab(0, rolTexto, tr("Texto"));
        abas->setCurrentIndex(0);
        connect(m_textPanel, &PictureTextPanel::changed, this, [this] {
            if (m_stage) m_stage->update();
        });
    }

    m_fxPanel = new VisualEffectsPanel(ed, m_def.fx, abas);
    auto* rolagem = new QScrollArea(abas);
    rolagem->setWidgetResizable(true);
    rolagem->setWidget(m_fxPanel);
    abas->addTab(rolagem, tr("Efeitos"));
    connect(m_fxPanel, &VisualEffectsPanel::changed, this, [this] {
        if (m_stage) m_stage->update();
    });
    connect(m_fxPanel, &VisualEffectsPanel::previewTransitionIn, this, [this,previewDialog] { if(m_fxPanel)m_fxPanel->puxar();if(m_stage)m_stage->previewTransition(true);previewDialog->present(); });
    connect(m_fxPanel, &VisualEffectsPanel::previewTransitionOut, this, [this,previewDialog] { if(m_fxPanel)m_fxPanel->puxar();if(m_stage)m_stage->previewTransition(false);previewDialog->present(); });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* settingsColumn = new QVBoxLayout;
    settingsColumn->addWidget(abas, 1);
    auto* previewButton = new QPushButton(tr("Ver prévia"), this);
    previewButton->setToolTip(tr("Abre a imagem em uma janela de prévia sem ocupar espaço no comando."));
    settingsColumn->addWidget(previewButton);
    settingsColumn->addWidget(bb);
    connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

    raiz->addLayout(settingsColumn, 1);

    // ---- ligações --------------------------------------------------------
    auto puxarDosCampos = [this] {
        m_def.number = m_number->value();
        m_def.assetId = m_asset->currentData().toString();
        m_def.assetName = m_asset->currentText();
        core::PictureTransformState transform = m_def.transformState();
        transform.x = m_x->value(); transform.y = m_y->value();
        transform.scaleX = m_sx->value(); transform.scaleY = m_sy->value();
        transform.opacity = m_op->value(); transform.angle = m_ang->value();
        transform.anchor = core::pictureAnchorFromId(m_anchor->currentData().toString());
        m_def.setTransformState(transform);

        core::PictureDisplayState display = m_def.displayState();
        display.space = core::pictureSpaceFromId(m_space->currentData().toString());
        display.layer = core::pictureLayerFromId(m_layer->currentData().toString());
        display.blend = core::pictureBlendFromId(m_blend->currentData().toString());
        display.smooth = m_smooth->isChecked();
        display.flipH = m_flipH->isChecked(); display.flipV = m_flipV->isChecked();
        display.duringBattle = m_duringBattle->isChecked();
        display.eraseOnMapChange = m_eraseOnMapChange->isChecked();
        display.affectedByTone = m_affectedByTone->isChecked();
        m_def.setDisplayState(display);

        core::PictureAnimationState animation = m_def.animationState();
        animation.frames.enabled = m_spritesheet->isChecked();
        animation.frames.count = qMax(1, m_frameCount->value());
        animation.frames.columns = qMax(1, m_frameColumns->value());
        animation.frames.rows = qMax(1, m_frameRows->value());
        animation.frames.firstFrame = qBound(0, m_frameIndex->value(), animation.frames.count - 1);
        animation.frames.fps = m_frameFps->value();
        animation.frames.loop = m_frameLoop->isChecked();
        animation.frames.playing = m_framePlaying->isChecked();
        m_def.setAnimationState(animation);
        m_frameIndex->setMaximum(qMax(0, animation.frames.count - 1));

        if (m_frameValidation) {
            if (!m_spritesheet->isChecked()) {
                m_frameValidation->setText(tr("Desativada — a imagem inteira será exibida."));
            } else {
                const core::PictureAsset* selected = ed.pictureFor(m_def.assetId, m_def.assetName);
                if (!selected || selected->image.isNull()) {
                    m_frameValidation->setText(tr("Escolha uma imagem válida para validar a spritesheet."));
                } else {
                    const core::FrameSequenceValidation check = animation.frames.validate(selected->image.size());
                    switch (check.issue) {
                    case core::FrameSequenceIssue::None:
                        m_frameValidation->setText(
                            tr("Sequência válida: %1 frame(s), grade %2×%3, quadro %4×%5 px, início %6, %7 fps.")
                                .arg(animation.frames.count).arg(animation.frames.columns)
                                .arg(animation.frames.rows).arg(check.frameSize.width())
                                .arg(check.frameSize.height()).arg(animation.frames.firstFrame)
                                .arg(animation.frames.fps, 0, 'f', 2));
                        break;
                    case core::FrameSequenceIssue::CountExceedsGrid:
                        m_frameValidation->setText(
                            tr("Configuração inválida: a grade %1×%2 possui %3 células, mas foram pedidos %4 frames. "
                               "O runtime limita a reprodução a %3 para preservar projetos antigos.")
                                .arg(animation.frames.columns).arg(animation.frames.rows)
                                .arg(check.capacity).arg(animation.frames.count));
                        break;
                    case core::FrameSequenceIssue::GridExceedsImage:
                        m_frameValidation->setText(
                            tr("Configuração inválida: a grade %1×%2 é maior que a imagem de %3×%4 px. "
                               "O runtime exibirá a imagem inteira como fallback seguro.")
                                .arg(animation.frames.columns).arg(animation.frames.rows)
                                .arg(selected->image.width()).arg(selected->image.height()));
                        break;
                    case core::FrameSequenceIssue::ImageNotDivisible:
                        m_frameValidation->setText(
                            tr("Configuração inválida: %1×%2 px não é divisível exatamente pela grade %3×%4. "
                               "Ajuste/corte a spritesheet para que todos os frames tenham o mesmo tamanho; "
                               "até lá o runtime exibirá a imagem inteira.")
                                .arg(selected->image.width()).arg(selected->image.height())
                                .arg(animation.frames.columns).arg(animation.frames.rows));
                        break;
                    case core::FrameSequenceIssue::ImageUnavailable:
                        m_frameValidation->setText(tr("Imagem indisponível para validação da spritesheet."));
                        break;
                    }
                }
            }
        }

        core::PicturePhysicsState physics = m_def.physicsState();
        physics.floatSpeed = m_floatSpeed->value(); physics.floatRange = m_floatRange->value();
        physics.swaySpeed = m_swaySpeed->value(); physics.swayRange = m_swayRange->value();
        physics.spinSpeed = m_spinSpeed->value(); physics.pulseSpeed = m_pulseSpeed->value();
        physics.pulseRange = m_pulseRange->value();
        m_def.setPhysicsState(physics);
        if (m_nineSlice) {
            m_def.nineSlice.enabled = m_nineSlice->isChecked();
            m_def.nineSlice.width = m_nineWidth->value();
            m_def.nineSlice.height = m_nineHeight->value();
            m_def.nineSlice.left = m_nineLeft->value();
            m_def.nineSlice.top = m_nineTop->value();
            m_def.nineSlice.right = m_nineRight->value();
            m_def.nineSlice.bottom = m_nineBottom->value();
            m_def.nineSlice.edgeMode = core::pictureNineSliceModeFromId(m_nineEdgeMode->currentData().toString());
            m_def.nineSlice.centerMode = core::pictureNineSliceModeFromId(m_nineCenterMode->currentData().toString());
        }
        if (m_stage) m_stage->update();
    };
    // Arrastar no palco muda a struct direto; os campos precisam acompanhar.
    auto empurrarParaCampos = [this] {
        QSignalBlocker b1(m_x), b2(m_y), b3(m_sx), b4(m_sy), b5(m_ang);
        m_x->setValue(m_def.x);
        m_y->setValue(m_def.y);
        m_sx->setValue(m_def.scaleX);
        m_sy->setValue(m_def.scaleY);
        m_ang->setValue(m_def.angle);
        if (m_positionPreset) {
            QSignalBlocker bp(m_positionPreset);
            m_positionPreset->setCurrentIndex(0);
        }
    };

    for (QDoubleSpinBox* s : { m_x, m_y, m_sx, m_sy, m_op, m_ang, m_frameFps, m_floatSpeed, m_floatRange,
                               m_swaySpeed, m_swayRange, m_spinSpeed, m_pulseSpeed, m_pulseRange })
        connect(s, &QDoubleSpinBox::valueChanged, this, [puxarDosCampos](double) { puxarDosCampos(); });
    for (QSpinBox* s : { m_frameCount, m_frameColumns, m_frameRows, m_frameIndex,
                          m_nineWidth, m_nineHeight, m_nineLeft, m_nineTop, m_nineRight, m_nineBottom })
        connect(s, &QSpinBox::valueChanged, this, [puxarDosCampos](int) { puxarDosCampos(); });
    for (QComboBox* c : { m_asset, m_anchor, m_space, m_layer, m_blend, m_nineEdgeMode, m_nineCenterMode })
        connect(c, &QComboBox::currentIndexChanged, this, [puxarDosCampos](int) { puxarDosCampos(); });
    connect(m_number, &QSpinBox::valueChanged, this, [puxarDosCampos](int) { puxarDosCampos(); });
    for (QCheckBox* c : { m_smooth, m_flipH, m_flipV, m_duringBattle, m_eraseOnMapChange, m_affectedByTone,
                           m_spritesheet, m_frameLoop, m_framePlaying, m_nineSlice })
        connect(c, &QCheckBox::toggled, this, [puxarDosCampos](bool) { puxarDosCampos(); });
    connect(snap, &QCheckBox::toggled, m_stage, &PictureStageView::setSnapEnabled);
    connect(m_positionPreset, &QComboBox::currentIndexChanged, this,
            [this, presetPoints](int index) {
        const int preset = m_positionPreset->itemData(index).toInt();
        if (preset < 0 || preset >= presetPoints.size()) return;
        m_def.anchor = core::PictureAnchor(preset);
        m_def.x = presetPoints[preset].x();
        m_def.y = presetPoints[preset].y();
        {
            QSignalBlocker bx(m_x), by(m_y), ba(m_anchor);
            m_x->setValue(m_def.x);
            m_y->setValue(m_def.y);
            m_anchor->setCurrentIndex(preset);
        }
        m_stage->update();
    });
    connect(m_stage, &PictureStageView::defChanged, this, empurrarParaCampos);
    connect(animar, &QCheckBox::toggled, this, [this](bool on) { m_stage->setAnimated(on); });
    connect(bLib, &QPushButton::clicked, this, [this, puxarDosCampos] {
        // RC2.68: Pictures usam o mesmo Universal Asset Picker de Sprite/Áudio.
        // PictureAsset permanece somente como ponte persistente/runtime.
        const QString selectedId = AssetBrowserDialog::choosePictureAssetId(ed, this);
        if (selectedId.isEmpty()) return;
        QSignalBlocker blocker(m_asset);
        m_asset->clear();
        for (const core::PictureAsset& a : ed.pictures) m_asset->addItem(a.name, a.id);
        const int i = ed.pictureIndexById(selectedId);
        m_asset->setCurrentIndex(i >= 0 ? i : 0);
        puxarDosCampos();
    });
    connect(&ed.resources(), &core::ResourceManager::pictureLibraryChanged, this, [this, puxarDosCampos] {
        const QString atual = m_asset ? m_asset->currentData().toString() : QString();
        if (!m_asset) return;
        QSignalBlocker blocker(m_asset);
        m_asset->clear();
        for (const core::PictureAsset& a : ed.pictures) m_asset->addItem(a.name, a.id);
        const int i = ed.pictureIndexById(atual);
        m_asset->setCurrentIndex(i >= 0 ? i : (m_asset->count() ? 0 : -1));
        puxarDosCampos();
    });
    connect(bReferences, &QPushButton::clicked, this,
            [this, referenceEntries, selectedReferences, bReferences] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Imagens de referência"));
        dialog.resize(640, 420);
        auto* layout = new QVBoxLayout(&dialog);
        auto* help = new QLabel(tr("Marque as pictures que deseja enxergar enquanto monta esta. "
                                   "As referências aparecem translúcidas e também servem ao snap."),
                                &dialog);
        help->setWordWrap(true);
        layout->addWidget(help);
        auto* list = new QListWidget(&dialog);
        for (int i = 0; i < referenceEntries.size(); ++i) {
            auto* item = new QListWidgetItem(referenceEntries[i].label, list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(selectedReferences->at(i) ? Qt::Checked : Qt::Unchecked);
        }
        layout->addWidget(list, 1);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;

        QVector<core::PictureDef> refs;
        int count = 0;
        for (int i = 0; i < referenceEntries.size(); ++i) {
            const bool checked = list->item(i)->checkState() == Qt::Checked;
            (*selectedReferences)[i] = checked;
            if (checked) { refs.push_back(referenceEntries[i].def); ++count; }
        }
        m_stage->setReferencePictures(refs);
        bReferences->setText(count == 0
            ? tr("Imagens de referência…")
            : tr("Imagens de referência… (%1)").arg(count));
    });
    connect(bb, &QDialogButtonBox::accepted, this, [this, puxarDosCampos] {
        puxarDosCampos();
        if (m_fxPanel) m_fxPanel->puxar();
        if (m_textPanel) m_textPanel->puxar();
        aplicarShow();
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    puxarDosCampos();
    fitPictureDialogToScreen(this, QSize(1180, 760));
}

void PictureCommandDialog::aplicarShow()
{
    if (m_textPanel) m_textPanel->puxar();
    const QString localizationKey=m_cmd.params.value(QStringLiteral("localizationKey")).toString();
    const QString logicalName=m_logicalName?m_logicalName->text().trimmed():QString();
    const QString pictureGroup=m_pictureGroup?m_pictureGroup->text().trimmed():QString();
    const QString dynamicX=m_dynamicX?m_dynamicX->text().trimmed():QString();
    const QString dynamicY=m_dynamicY?m_dynamicY->text().trimmed():QString();
    const QString dynamicOpacity=m_dynamicOpacity?m_dynamicOpacity->text().trimmed():QString();
    m_cmd.type = m_def.rich.enabled ? QStringLiteral("picture.text")
        : m_tipo==QLatin1String("picture.showByName") ? QStringLiteral("picture.showByName") : QStringLiteral("picture.show");
    m_cmd.params = m_def.toParams();
    if(!logicalName.isEmpty())m_cmd.params[QStringLiteral("logicalName")]=logicalName;
    if(!pictureGroup.isEmpty())m_cmd.params[QStringLiteral("group")]=pictureGroup;
    if(!dynamicX.isEmpty())m_cmd.params[QStringLiteral("x")]=dynamicX;
    if(!dynamicY.isEmpty())m_cmd.params[QStringLiteral("y")]=dynamicY;
    if(!dynamicOpacity.isEmpty())m_cmd.params[QStringLiteral("opacity")]=dynamicOpacity;
    if(m_def.rich.enabled&&!localizationKey.isEmpty())
        m_cmd.params[QStringLiteral("localizationKey")]=localizationKey;
}

void PictureCommandDialog::montarMove()
{
    setWindowTitle(tr("Mover / animar imagem"));
    auto* raiz = new QVBoxLayout(this);
    auto* corpo = new QHBoxLayout;

    m_stage = new PictureStageView(ed, m_def, this);
    m_stage->setInteractive(false);
    m_stage->setShowPlayerReference(true);
    auto* previewDialog = new CommandPreviewDialog(tr("Prévia do movimento da imagem"), m_stage, this);
    previewDialog->resize(940, 700);

    auto* painel = new QWidget(this);
    auto* painelLayout = new QVBoxLayout(painel);
    painelLayout->setContentsMargins(4, 4, 4, 4);
    auto* form = new QFormLayout;

    m_number = new QSpinBox(this);
    m_number->setRange(1, 100);
    m_number->setValue(m_cmd.params.value(QStringLiteral("number"), 1).toInt());
    form->addRow(tr("Número (slot):"), m_number);

    // Cada propriedade tem uma caixinha "animar esta": mover só o X não pode
    // arrastar junto a opacidade que outro comando ajustou.
    struct Campo { const char* chave; QString rotulo; double min, max, padrao; };
    const QVector<Campo> campos = {
        { "x", tr("X"), -4000, 4000, 0 },
        { "y", tr("Y"), -4000, 4000, 0 },
        { "scaleX", tr("Escala X (%)"), 1, 2000, 100 },
        { "scaleY", tr("Escala Y (%)"), 1, 2000, 100 },
        { "opacity", tr("Opacidade"), 0, 255, 255 },
        { "angle", tr("Ângulo (°)"), -3600, 3600, 0 },
    };
    QVector<QCheckBox*> usa;
    QVector<QDoubleSpinBox*> val;
    for (const Campo& c : campos) {
        const QString k = QString::fromLatin1(c.chave);
        auto* chk = new QCheckBox(c.rotulo, this);
        auto* sp = new QDoubleSpinBox(this);
        sp->setRange(c.min, c.max);
        sp->setDecimals(1);
        sp->setValue(m_cmd.params.value(k, c.padrao).toDouble());
        const bool tem = m_cmd.params.contains(k);
        chk->setChecked(tem);
        sp->setEnabled(tem);
        connect(chk, &QCheckBox::toggled, sp, &QWidget::setEnabled);
        auto* linha = new QHBoxLayout;
        linha->addWidget(chk, 1);
        linha->addWidget(sp, 1);
        form->addRow(linha);
        usa.push_back(chk);
        val.push_back(sp);
    }

    m_dur = new QSpinBox(this);
    m_dur->setRange(0, 6000);
    m_dur->setSuffix(tr(" quadros"));
    m_dur->setValue(m_cmd.params.value(QStringLiteral("duration"), 60).toInt());
    form->addRow(tr("Duração:"), m_dur);

    m_ease = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureEase::Elastic); ++i) {
        const auto e = core::PictureEase(i);
        m_ease->addItem(core::pictureEaseLabel(e), core::pictureEaseId(e));
    }
    m_ease->setCurrentIndex(int(core::pictureEaseFromId(
        m_cmd.params.value(QStringLiteral("ease"), QStringLiteral("easeOut")).toString())));
    form->addRow(tr("Curva:"), m_ease);

    m_wait = new QCheckBox(tr("Esperar a animação terminar"), this);
    m_wait->setChecked(m_cmd.params.value(QStringLiteral("wait"), false).toBool());
    form->addRow(QString(), m_wait);
    painelLayout->addLayout(form);

    auto* origemInfo = new QLabel(painel);
    origemInfo->setWordWrap(true);
    origemInfo->setStyleSheet(QStringLiteral("color:#9aa4b2;"));
    painelLayout->addWidget(origemInfo);

    auto* quadroInfo = new QLabel(painel);
    quadroInfo->setAlignment(Qt::AlignCenter);
    quadroInfo->setStyleSheet(QStringLiteral("font-weight:600;color:#ffd45e;"));
    painelLayout->addWidget(quadroInfo);

    auto* controles = new QHBoxLayout;
    auto* reproduzir = new QPushButton(tr("▶ Reproduzir"), painel);
    auto* pausar = new QPushButton(tr("⏸ Pausar"), painel);
    auto* reiniciar = new QPushButton(tr("↤ Reiniciar"), painel);
    auto* avancar = new QPushButton(tr("+1 quadro"), painel);
    controles->addWidget(reproduzir);
    controles->addWidget(pausar);
    controles->addWidget(reiniciar);
    controles->addWidget(avancar);
    painelLayout->addLayout(controles);

    auto* dica = new QLabel(
        tr("A silhueta azul mostra a posição inicial. O jogador no centro é "
           "apenas uma referência visual de escala e sobreposição."), painel);
    dica->setWordWrap(true);
    dica->setStyleSheet(QStringLiteral("color:#7f8794;"));
    painelLayout->addWidget(dica);
    auto* previewButton = new QPushButton(tr("Ver prévia"), painel);
    previewButton->setToolTip(tr("Abre o ensaio da movimentação em uma janela separada."));
    painelLayout->addWidget(previewButton);
    painelLayout->addStretch(1);
    connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

    auto* rolagem = new QScrollArea(this);
    rolagem->setWidgetResizable(true);
    rolagem->setFrameShape(QFrame::NoFrame);
    rolagem->setWidget(painel);
    corpo->addWidget(rolagem, 2);

    raiz->addLayout(corpo, 1);

    const QVector<PictureReferenceEntry> references = pictureReferences(ed, &m_cmd);
    auto inicio = std::make_shared<core::PictureDef>();
    auto frame = std::make_shared<int>(0);
    auto timer = new QTimer(this);
    timer->setInterval(1000 / 60);

    auto carregarInicio = [this, references, inicio, origemInfo] {
        const int slot = m_number->value();
        bool currentFound = false;
        std::optional<core::PictureDef> before =
            pictureStateBeforeCommand(ed, &m_cmd, slot, &currentFound);

        QString source;
        if (before) {
            *inicio = *before;
            source = tr("Estado inicial reconstruído a partir dos comandos anteriores da imagem %1.").arg(slot);
        } else {
            bool referenceFound = false;
            for (auto it = references.crbegin(); it != references.crend(); ++it) {
                if (it->def.number != slot) continue;
                *inicio = it->def;
                referenceFound = true;
                break;
            }
            if (referenceFound) {
                source = currentFound
                    ? tr("Nenhuma Picture %1 estava visível antes deste comando; usando outra "
                         "Picture do mesmo slot somente como referência.").arg(slot)
                    : tr("Usando outra imagem %1 do projeto como referência visual.").arg(slot);
            } else {
                *inicio = core::PictureDef();
                inicio->number = slot;
                inicio->anchor = core::PictureAnchor::Center;
                inicio->x = ed.gameResolution.width() / 2.0;
                inicio->y = ed.gameResolution.height() / 2.0;
                if (!ed.pictures.isEmpty()) {
                    inicio->assetId = ed.pictures.first().id;
                    inicio->assetName = ed.pictures.first().name;
                    source = tr("Não há um comando Mostrar Picture %1 anterior; usando a primeira "
                                "imagem da biblioteca no centro.").arg(slot);
                } else {
                    source = tr("Não há uma imagem %1 anterior nem uma imagem disponível na biblioteca para a prévia.")
                                 .arg(slot);
                }
            }
        }
        inicio->number = slot;
        m_def = *inicio;
        m_stage->setReferencePictures({*inicio});
        origemInfo->setText(source);
    };

    auto aplicarQuadro = [this, campos, usa, val, inicio, frame, quadroInfo] {
        const int duration = m_dur->value();
        const double progress = duration <= 0 ? 1.0
            : qBound(0.0, *frame / double(duration), 1.0);
        const double eased = core::applyEase(
            core::pictureEaseFromId(m_ease->currentData().toString()), progress);
        m_def = *inicio;
        auto interpolate = [eased](double from, double to) {
            return from + (to - from) * eased;
        };
        for (int i = 0; i < campos.size(); ++i) {
            if (!usa[i]->isChecked()) continue;
            const double target = val[i]->value();
            const QByteArray key(campos[i].chave);
            if (key == "x") m_def.x = interpolate(inicio->x, target);
            else if (key == "y") m_def.y = interpolate(inicio->y, target);
            else if (key == "scaleX") m_def.scaleX = interpolate(inicio->scaleX, target);
            else if (key == "scaleY") m_def.scaleY = interpolate(inicio->scaleY, target);
            else if (key == "opacity") m_def.opacity = interpolate(inicio->opacity, target);
            else if (key == "angle") m_def.angle = interpolate(inicio->angle, target);
        }
        const int shownFrame = duration <= 0 ? 0 : qBound(0, *frame, duration);
        quadroInfo->setText(duration <= 0
            ? tr("Resultado imediato (0 quadros)")
            : tr("Quadro %1 de %2  ·  %3%")
                  .arg(shownFrame).arg(duration).arg(qRound(progress * 100.0)));
        m_stage->update();
    };

    connect(timer, &QTimer::timeout, this, [timer, frame, this, aplicarQuadro] {
        const int duration = m_dur->value();
        if (duration <= 0 || *frame >= duration) {
            timer->stop();
            *frame = qMax(0, duration);
            aplicarQuadro();
            return;
        }
        ++*frame;
        aplicarQuadro();
    });
    connect(reproduzir, &QPushButton::clicked, this,
            [timer, frame, this, aplicarQuadro] {
        const int duration = m_dur->value();
        if (duration <= 0) { *frame = 0; aplicarQuadro(); return; }
        if (*frame >= duration) *frame = 0;
        aplicarQuadro();
        timer->start();
    });
    connect(pausar, &QPushButton::clicked, timer, &QTimer::stop);
    connect(reiniciar, &QPushButton::clicked, this,
            [timer, frame, aplicarQuadro] {
        timer->stop();
        *frame = 0;
        aplicarQuadro();
    });
    connect(avancar, &QPushButton::clicked, this,
            [timer, frame, this, aplicarQuadro] {
        timer->stop();
        *frame = qMin(qMax(0, m_dur->value()), *frame + 1);
        aplicarQuadro();
    });
    connect(m_number, &QSpinBox::valueChanged, this,
            [timer, frame, carregarInicio, aplicarQuadro](int) {
        timer->stop();
        *frame = 0;
        carregarInicio();
        aplicarQuadro();
    });
    connect(m_dur, &QSpinBox::valueChanged, this,
            [timer, frame, this, aplicarQuadro](int duration) {
        timer->stop();
        *frame = qBound(0, *frame, qMax(0, duration));
        aplicarQuadro();
    });
    connect(m_ease, &QComboBox::currentIndexChanged, this,
            [aplicarQuadro](int) { aplicarQuadro(); });
    for (int i = 0; i < usa.size(); ++i) {
        connect(usa[i], &QCheckBox::toggled, this,
                [aplicarQuadro](bool) { aplicarQuadro(); });
        connect(val[i], &QDoubleSpinBox::valueChanged, this,
                [aplicarQuadro](double) { aplicarQuadro(); });
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    raiz->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this, campos, usa, val] {
        QVariantMap p;
        p[QStringLiteral("number")] = m_number->value();
        for (int i = 0; i < campos.size(); ++i)
            if (usa[i]->isChecked())
                p[QString::fromLatin1(campos[i].chave)] = val[i]->value();
        p[QStringLiteral("duration")] = m_dur->value();
        p[QStringLiteral("ease")] = m_ease->currentData().toString();
        p[QStringLiteral("wait")] = m_wait->isChecked();
        m_cmd.type = QStringLiteral("picture.move");
        m_cmd.params = p;
        accept();
    });
    carregarInicio();
    aplicarQuadro();
    fitPictureDialogToScreen(this, QSize(1180, 720));
}

void PictureCommandDialog::montarZoom(bool zoomIn)
{
    setWindowTitle(zoomIn ? tr("Zoom In da imagem") : tr("Zoom Out da imagem"));
    auto* raiz = new QVBoxLayout(this);
    auto* corpo = new QHBoxLayout;

    auto* painel = new QWidget(this);
    auto* left = new QVBoxLayout(painel);
    left->setContentsMargins(4, 4, 4, 4);
    auto* form = new QFormLayout;

    m_number = new QSpinBox(this);
    m_number->setRange(1, 100);
    m_number->setValue(m_cmd.params.value(QStringLiteral("number"), 1).toInt());
    form->addRow(tr("Número (slot):"), m_number);

    const double defaultScale = zoomIn ? 150.0 : 50.0;
    m_sx = new QDoubleSpinBox(this);
    m_sx->setRange(0.01, 2000.0); m_sx->setDecimals(1); m_sx->setSuffix(QStringLiteral(" %"));
    m_sx->setValue(m_cmd.params.value(QStringLiteral("scaleX"), defaultScale).toDouble());
    m_sy = new QDoubleSpinBox(this);
    m_sy->setRange(0.01, 2000.0); m_sy->setDecimals(1); m_sy->setSuffix(QStringLiteral(" %"));
    m_sy->setValue(m_cmd.params.value(QStringLiteral("scaleY"), defaultScale).toDouble());
    form->addRow(tr("Escala X final:"), m_sx);
    form->addRow(tr("Escala Y final:"), m_sy);

    m_dur = new QSpinBox(this);
    m_dur->setRange(0, 6000); m_dur->setSuffix(tr(" quadros"));
    m_dur->setValue(m_cmd.params.value(QStringLiteral("duration"), 30).toInt());
    form->addRow(tr("Duração:"), m_dur);

    m_ease = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureEase::Elastic); ++i) {
        const auto e = core::PictureEase(i);
        m_ease->addItem(core::pictureEaseLabel(e), core::pictureEaseId(e));
    }
    m_ease->setCurrentIndex(int(core::pictureEaseFromId(
        m_cmd.params.value(QStringLiteral("ease"), QStringLiteral("easeOut")).toString())));
    form->addRow(tr("Curva:"), m_ease);

    m_wait = new QCheckBox(tr("Esperar o zoom terminar"), this);
    m_wait->setChecked(m_cmd.params.value(QStringLiteral("wait"), false).toBool());
    form->addRow(QString(), m_wait);
    left->addLayout(form);

    auto* info = new QLabel(this);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color:#9aa4b2;"));
    left->addWidget(info);

    auto* progressInfo = new QLabel(this);
    progressInfo->setAlignment(Qt::AlignCenter);
    progressInfo->setStyleSheet(QStringLiteral("font-weight:600;color:#ffd45e;"));
    left->addWidget(progressInfo);

    auto* controls = new QHBoxLayout;
    auto* play = new QPushButton(tr("▶ Reproduzir"), this);
    auto* reset = new QPushButton(tr("↤ Reiniciar"), this);
    controls->addWidget(play); controls->addWidget(reset);
    left->addLayout(controls);

    auto* hint = new QLabel(
        tr("O Zoom usa o mesmo pivot/âncora da Picture. Flip, rotação e transições "
           "não criam um segundo centro de transformação."), this);
    hint->setWordWrap(true); hint->setStyleSheet(QStringLiteral("color:#7f8794;"));
    left->addWidget(hint);

    m_stage = new PictureStageView(ed, m_def, this);
    m_stage->setInteractive(false);
    m_stage->setShowPlayerReference(true);
    auto* previewDialog = new CommandPreviewDialog(tr("Prévia da aproximação da imagem"), m_stage, this);
    previewDialog->resize(940, 700);

    auto* previewButton = new QPushButton(tr("Ver prévia"), painel);
    previewButton->setToolTip(tr("Abre o ensaio do Zoom em uma janela separada."));
    left->addWidget(previewButton);
    left->addStretch(1);
    connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(painel);
    corpo->addWidget(scroll, 2);

    raiz->addLayout(corpo, 1);

    auto start = std::make_shared<core::PictureDef>();
    auto frame = std::make_shared<int>(0);
    auto timer = new QTimer(this);
    timer->setInterval(1000 / 60);

    auto loadStart = [this, start, info] {
        const int slot = m_number->value();
        if (const auto before = pictureStateBeforeCommand(ed, &m_cmd, slot)) {
            *start = *before;
            info->setText(tr("Estado inicial reconstruído a partir dos comandos anteriores da imagem %1.").arg(slot));
        } else {
            *start = fallbackPreviewPicture(ed, slot);
            info->setText(tr("Sem uma imagem %1 anterior: usando uma referência neutra para a prévia.").arg(slot));
        }
        start->number = slot;
        m_def = *start;
        m_stage->setReferencePictures({*start});
    };

    auto applyFrame = [this, start, frame, progressInfo] {
        const int duration = m_dur->value();
        const double progress = duration <= 0 ? 1.0 : qBound(0.0, *frame / double(duration), 1.0);
        const double eased = core::applyEase(
            core::pictureEaseFromId(m_ease->currentData().toString()), progress);
        m_def = *start;
        m_def.scaleX = start->scaleX + (m_sx->value() - start->scaleX) * eased;
        m_def.scaleY = start->scaleY + (m_sy->value() - start->scaleY) * eased;
        progressInfo->setText(duration <= 0
            ? tr("Resultado imediato")
            : tr("Quadro %1 de %2 · %3%").arg(qBound(0, *frame, duration)).arg(duration).arg(qRound(progress * 100.0)));
        m_stage->update();
    };

    connect(timer, &QTimer::timeout, this, [timer, frame, this, applyFrame] {
        const int duration = m_dur->value();
        if (duration <= 0 || *frame >= duration) { timer->stop(); *frame = qMax(0, duration); applyFrame(); return; }
        ++*frame; applyFrame();
    });
    connect(play, &QPushButton::clicked, this, [timer, frame, this, applyFrame] {
        const int duration = m_dur->value();
        if (duration <= 0) { *frame = 0; applyFrame(); return; }
        *frame = 0; applyFrame(); timer->start();
    });
    connect(reset, &QPushButton::clicked, this, [timer, frame, applyFrame] {
        timer->stop(); *frame = 0; applyFrame();
    });
    connect(m_number, &QSpinBox::valueChanged, this, [timer, frame, loadStart, applyFrame](int) {
        timer->stop(); *frame = 0; loadStart(); applyFrame();
    });
    connect(m_sx, &QDoubleSpinBox::valueChanged, this, [applyFrame](double){ applyFrame(); });
    connect(m_sy, &QDoubleSpinBox::valueChanged, this, [applyFrame](double){ applyFrame(); });
    connect(m_dur, &QSpinBox::valueChanged, this, [timer, frame, applyFrame](int){ timer->stop(); *frame = 0; applyFrame(); });
    connect(m_ease, &QComboBox::currentIndexChanged, this, [applyFrame](int){ applyFrame(); });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    raiz->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        QVariantMap p;
        p[QStringLiteral("number")] = m_number->value();
        p[QStringLiteral("scaleX")] = m_sx->value();
        p[QStringLiteral("scaleY")] = m_sy->value();
        p[QStringLiteral("duration")] = m_dur->value();
        p[QStringLiteral("ease")] = m_ease->currentData().toString();
        p[QStringLiteral("wait")] = m_wait->isChecked();
        m_cmd.type = m_tipo;
        m_cmd.params = p;
        accept();
    });

    loadStart(); applyFrame();
    fitPictureDialogToScreen(this, QSize(1040, 650));
}

void PictureCommandDialog::montarSimples()
{
    auto* raiz = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_number = new QSpinBox(this);
    m_number->setRange(0, 100);
    m_number->setValue(m_cmd.params.value(QStringLiteral("number"), 1).toInt());

    if (m_tipo == QLatin1String("picture.erase")) {
        setWindowTitle(tr("Apagar imagem"));
        form->addRow(tr("Número (0 = todas):"), m_number);
    } else if (m_tipo == QLatin1String("picture.wait")) {
        setWindowTitle(tr("Esperar a imagem"));
        form->addRow(tr("Número (0 = qualquer):"), m_number);
    } else if (m_tipo == QLatin1String("picture.physics")) {
        setWindowTitle(tr("Movimento automático da imagem"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        auto novo = [this](double val, double max, const QString& sufixo) {
            auto* s = new QDoubleSpinBox(this);
            s->setRange(-max, max);
            s->setDecimals(2);
            s->setValue(val);
            s->setSuffix(sufixo);
            return s;
        };
        const QVariantMap& p = m_cmd.params;
        m_floatSpeed = novo(p.value(QStringLiteral("floatSpeed"), 0.0).toDouble(), 20, tr(" ciclos/s"));
        m_floatRange = novo(p.value(QStringLiteral("floatRange"), 12.0).toDouble(), 500, QStringLiteral(" px"));
        m_swaySpeed  = novo(p.value(QStringLiteral("swaySpeed"), 0.0).toDouble(), 20, tr(" ciclos/s"));
        m_swayRange  = novo(p.value(QStringLiteral("swayRange"), 10.0).toDouble(), 500, QStringLiteral(" px"));
        m_spinSpeed  = novo(p.value(QStringLiteral("spinSpeed"), 0.0).toDouble(), 3600, tr(" °/s"));
        m_pulseSpeed = novo(p.value(QStringLiteral("pulseSpeed"), 0.0).toDouble(), 20, tr(" ciclos/s"));
        m_pulseRange = novo(p.value(QStringLiteral("pulseRange"), 8.0).toDouble(), 200, tr(" %"));
        form->addRow(tr("Flutuar (vel.):"), m_floatSpeed);
        form->addRow(tr("Flutuar (alcance):"), m_floatRange);
        form->addRow(tr("Balançar (vel.):"), m_swaySpeed);
        form->addRow(tr("Balançar (alcance):"), m_swayRange);
        form->addRow(tr("Girar:"), m_spinSpeed);
        form->addRow(tr("Breathing / Respirar (vel.):"), m_pulseSpeed);
        form->addRow(tr("Breathing / Respirar (escala):"), m_pulseRange);
    } else if (m_tipo == QLatin1String("picture.effects")) {
        setWindowTitle(tr("Efeitos da imagem"));
        resize(1120, 720);
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        const core::PictureEffects commandFx = core::PictureEffects::fromParams(m_cmd.params.value(QStringLiteral("fx")).toMap());
        if (auto before = pictureStateBeforeCommand(ed, &m_cmd, m_number->value())) m_def = *before;
        m_def.number=m_number->value();m_def.fx=commandFx;
        m_stage=new PictureStageView(ed,m_def,this);m_stage->setInteractive(false);m_stage->setAnimated(true);
        auto* previewDialog = new CommandPreviewDialog(tr("Prévia dos efeitos da imagem"), m_stage, this);
        previewDialog->resize(940,700);
        m_fxPanel = new VisualEffectsPanel(ed, m_def.fx, this);
        auto* rol = new QScrollArea(this);
        rol->setWidgetResizable(true);
        rol->setWidget(m_fxPanel);

        auto* body = new QVBoxLayout;
        body->addLayout(form);
        body->addWidget(rol, 1);
        auto* previewButton = new QPushButton(tr("Ver prévia"), this);
        body->addWidget(previewButton);
        raiz->addLayout(body, 1);
        connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

        connect(m_fxPanel,&VisualEffectsPanel::changed,this,[this]{if(m_stage)m_stage->update();});
        connect(m_fxPanel,&VisualEffectsPanel::previewTransitionIn,this,[this,previewDialog]{m_fxPanel->puxar();if(m_stage)m_stage->previewTransition(true);previewDialog->present();});
        connect(m_fxPanel,&VisualEffectsPanel::previewTransitionOut,this,[this,previewDialog]{m_fxPanel->puxar();if(m_stage)m_stage->previewTransition(false);previewDialog->present();});
        connect(m_number,&QSpinBox::valueChanged,this,[this,commandFx](int slot){if(auto before=pictureStateBeforeCommand(ed,&m_cmd,slot))m_def=*before;m_def.number=slot;m_def.fx=commandFx;if(m_stage)m_stage->update();});
        auto* bb2 = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        raiz->addWidget(bb2);
        connect(bb2, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(bb2, &QDialogButtonBox::accepted, this, [this] {
            m_fxPanel->puxar();
            QVariantMap p;
            p[QStringLiteral("number")] = m_number->value();
            p[QStringLiteral("fx")] = m_def.fx.toParams();
            m_cmd.type = m_tipo;
            m_cmd.params = p;
            accept();
        });
        return;
    } else if (m_tipo == QLatin1String("picture.transitionOut")) {
        setWindowTitle(tr("Sumir com a imagem"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        auto* trans = new QComboBox(this);
        for (int i = 0; i <= int(core::PictureTransition::Bounce); ++i) {
            const auto t = core::PictureTransition(i);
            trans->addItem(core::pictureTransitionLabel(t), core::pictureTransitionId(t));
        }
        trans->setCurrentIndex(int(core::pictureTransitionFromId(
            m_cmd.params.value(QStringLiteral("transition"), QStringLiteral("fade")).toString())));
        form->addRow(tr("Como sumir:"), trans);
        auto* dur = new QSpinBox(this);
        dur->setRange(1, 600);
        dur->setSuffix(tr(" quadros"));
        dur->setValue(m_cmd.params.value(QStringLiteral("duration"), 30).toInt());
        form->addRow(tr("Duração:"), dur);
        auto* esperar = new QCheckBox(tr("Esperar a imagem sumir"), this);
        esperar->setChecked(m_cmd.params.value(QStringLiteral("wait"), true).toBool());
        form->addRow(QString(), esperar);
        auto* nota = new QLabel(tr("Quando a transição termina, a imagem é "
                                   "apagada — não precisa de outro comando."), this);
        nota->setWordWrap(true);
        nota->setStyleSheet(QStringLiteral("color:#888;"));
        form->addRow(nota);
        if(auto before=pictureStateBeforeCommand(ed,&m_cmd,m_number->value()))m_def=*before;m_def.number=m_number->value();
        m_stage=new PictureStageView(ed,m_def,this);m_stage->setInteractive(false);
        auto* previewHost = new QWidget(this);
        auto* previewHostLayout = new QVBoxLayout(previewHost);
        previewHostLayout->setContentsMargins(0,0,0,0);
        previewHostLayout->addWidget(m_stage,1);
        auto* previewOut=new QPushButton(tr("▶ Reproduzir saída"),previewHost);
        previewHostLayout->addWidget(previewOut);
        auto* previewDialog = new CommandPreviewDialog(tr("Prévia da remoção da imagem"), previewHost, this);
        previewDialog->resize(940,700);

        auto* body = new QVBoxLayout;
        body->addLayout(form);
        auto* previewButton = new QPushButton(tr("Ver prévia"),this);
        body->addWidget(previewButton);
        body->addStretch(1);
        raiz->addLayout(body, 1);
        resize(680, 560);
        connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

        connect(previewOut,&QPushButton::clicked,this,[this,trans,dur,previewDialog]{m_def.fx.transitionOut=core::pictureTransitionFromId(trans->currentData().toString());m_def.fx.transitionOutFrames=dur->value();m_stage->previewTransition(false);previewDialog->present();});
        connect(m_number,&QSpinBox::valueChanged,this,[this](int slot){if(auto before=pictureStateBeforeCommand(ed,&m_cmd,slot))m_def=*before;m_def.number=slot;if(m_stage)m_stage->update();});
        auto* bb2 = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        raiz->addWidget(bb2);
        connect(bb2, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(bb2, &QDialogButtonBox::accepted, this, [this, trans, dur, esperar] {
            QVariantMap p;
            p[QStringLiteral("number")] = m_number->value();
            p[QStringLiteral("transition")] = trans->currentData().toString();
            p[QStringLiteral("duration")] = dur->value();
            p[QStringLiteral("wait")] = esperar->isChecked();
            m_cmd.type = m_tipo;
            m_cmd.params = p;
            accept();
        });
        return;
    } else if (m_tipo == QLatin1String("picture.negative")) {
        setWindowTitle(tr("Negativo / Inverter cores"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        m_negativeEnabled = new QCheckBox(tr("Ativar negativo"), this);
        m_negativeEnabled->setChecked(m_cmd.params.value(QStringLiteral("enabled"), true).toBool());
        m_negativeStrength = new QDoubleSpinBox(this);
        m_negativeStrength->setRange(0.0, 1.0); m_negativeStrength->setDecimals(2); m_negativeStrength->setSingleStep(0.05);
        m_negativeStrength->setValue(m_cmd.params.value(QStringLiteral("strength"), 1.0).toDouble());
        m_negativeDuration = new QSpinBox(this);
        m_negativeDuration->setRange(0, 3600); m_negativeDuration->setSuffix(tr(" quadros"));
        m_negativeDuration->setValue(m_cmd.params.value(QStringLiteral("duration"), 0).toInt());
        m_wait = new QCheckBox(tr("Esperar a transição terminar"), this);
        m_wait->setChecked(m_cmd.params.value(QStringLiteral("wait"), false).toBool());
        form->addRow(m_negativeEnabled);
        form->addRow(tr("Intensidade:"), m_negativeStrength);
        form->addRow(tr("Transição:"), m_negativeDuration);
        form->addRow(m_wait);
    } else if (m_tipo == QLatin1String("picture.clearEffects")) {
        setWindowTitle(tr("Limpar efeitos da imagem"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
    } else if (m_tipo == QLatin1String("picture.flip")) {
        setWindowTitle(tr("Virar imagem"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        m_flipH = new QCheckBox(tr("Virar horizontalmente"), this);
        m_flipV = new QCheckBox(tr("Virar verticalmente"), this);
        m_flipH->setChecked(m_cmd.params.value(QStringLiteral("flipH"), false).toBool());
        m_flipV->setChecked(m_cmd.params.value(QStringLiteral("flipV"), false).toBool());
        form->addRow(QString(), m_flipH);
        form->addRow(QString(), m_flipV);
    } else if (m_tipo == QLatin1String("picture.display")) {
        setWindowTitle(tr("Configurações de Exibição"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        m_space = new QComboBox(this);
        m_space->addItem(core::pictureSpaceLabel(core::PictureSpace::Screen), QStringLiteral("screen"));
        m_space->addItem(core::pictureSpaceLabel(core::PictureSpace::Map), QStringLiteral("map"));
        m_space->setCurrentIndex(qMax(0, m_space->findData(m_cmd.params.value(QStringLiteral("space"), QStringLiteral("screen")).toString())));
        form->addRow(tr("Draw On:"), m_space);
        m_layer = new QComboBox(this);
        for (int i = 0; i <= int(core::PictureLayer::AboveAll); ++i) {
            const auto l = core::PictureLayer(i);
            m_layer->addItem(QStringLiteral("%1: %2").arg(i).arg(core::pictureLayerLabel(l)), core::pictureLayerId(l));
        }
        m_layer->setCurrentIndex(qMax(0, m_layer->findData(m_cmd.params.value(QStringLiteral("layer"), QStringLiteral("below-message")).toString())));
        form->addRow(tr("Camada:"), m_layer);
        m_duringBattle = new QCheckBox(tr("During Battle"), this);
        m_duringBattle->setChecked(m_cmd.params.value(QStringLiteral("duringBattle"), true).toBool());
        m_eraseOnMapChange = new QCheckBox(tr("Erase on map change"), this);
        m_eraseOnMapChange->setChecked(m_cmd.params.value(QStringLiteral("eraseOnMapChange"), false).toBool());
        m_affectedByTone = new QCheckBox(tr("Affected by screen tone"), this);
        m_affectedByTone->setChecked(m_cmd.params.value(QStringLiteral("affectedByTone"), false).toBool());
        form->addRow(m_duringBattle);
        form->addRow(m_eraseOnMapChange);
        form->addRow(m_affectedByTone);
    } else {   // picture.anchor
        setWindowTitle(tr("Âncora da imagem"));
        m_number->setRange(1, 100);
        form->addRow(tr("Número (slot):"), m_number);
        m_anchor = new QComboBox(this);
        for (int i = 0; i <= int(core::PictureAnchor::Custom); ++i) {
            const auto a = core::PictureAnchor(i);
            m_anchor->addItem(core::pictureAnchorLabel(a), core::pictureAnchorId(a));
        }
        m_anchor->setCurrentIndex(int(core::pictureAnchorFromId(
            m_cmd.params.value(QStringLiteral("anchor")).toString())));
        form->addRow(tr("Âncora:"), m_anchor);
    }
    if(m_tipo==QLatin1String("picture.erase")){
        raiz->addLayout(form);
    } else {
        auto refreshPreview=[this]{
            const int slot=m_number->value();if(auto before=pictureStateBeforeCommand(ed,&m_cmd,slot))m_def=*before;else m_def=fallbackPreviewPicture(ed,slot);m_def.number=slot;
            if(m_tipo==QLatin1String("picture.physics")){m_def.floatSpeed=m_floatSpeed->value();m_def.floatRange=m_floatRange->value();m_def.swaySpeed=m_swaySpeed->value();m_def.swayRange=m_swayRange->value();m_def.spinSpeed=m_spinSpeed->value();m_def.pulseSpeed=m_pulseSpeed->value();m_def.pulseRange=m_pulseRange->value();}
            else if(m_tipo==QLatin1String("picture.anchor"))m_def.anchor=core::pictureAnchorFromId(m_anchor->currentData().toString());
            else if(m_tipo==QLatin1String("picture.flip")){m_def.flipH=m_flipH->isChecked();m_def.flipV=m_flipV->isChecked();}
            else if(m_tipo==QLatin1String("picture.negative")){m_def.fx.negative.enabled=m_negativeEnabled->isChecked();m_def.fx.negative.strength=m_negativeEnabled->isChecked()?m_negativeStrength->value():0.0;m_def.fx.negative.transitionFrames=m_negativeDuration->value();}
            else if(m_tipo==QLatin1String("picture.display")){m_def.space=core::pictureSpaceFromId(m_space->currentData().toString());m_def.layer=core::pictureLayerFromId(m_layer->currentData().toString());m_def.duringBattle=m_duringBattle->isChecked();m_def.eraseOnMapChange=m_eraseOnMapChange->isChecked();m_def.affectedByTone=m_affectedByTone->isChecked();}
            else if(m_tipo==QLatin1String("picture.clearEffects"))m_def.fx=core::PictureEffects();
            if(m_stage)m_stage->update();
        };
        refreshPreview();
        m_stage=new PictureStageView(ed,m_def,this);
        m_stage->setInteractive(false);
        if(m_tipo==QLatin1String("picture.physics"))m_stage->setAnimated(true);
        auto* previewDialog = new CommandPreviewDialog(tr("Prévia da imagem"), m_stage, this);
        previewDialog->resize(940,700);

        auto* body = new QVBoxLayout;
        body->addLayout(form);
        auto* previewButton = new QPushButton(tr("Ver prévia"),this);
        body->addWidget(previewButton);
        body->addStretch(1);
        raiz->addLayout(body, 1);
        resize(680, 560);
        connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

        connect(m_number,&QSpinBox::valueChanged,this,[refreshPreview](int){refreshPreview();});
        if(m_flipH){connect(m_flipH,&QCheckBox::toggled,this,[refreshPreview](bool){refreshPreview();});connect(m_flipV,&QCheckBox::toggled,this,[refreshPreview](bool){refreshPreview();});}
        if(m_negativeEnabled){connect(m_negativeEnabled,&QCheckBox::toggled,this,[refreshPreview](bool){refreshPreview();});connect(m_negativeStrength,&QDoubleSpinBox::valueChanged,this,[refreshPreview](double){refreshPreview();});connect(m_negativeDuration,&QSpinBox::valueChanged,this,[refreshPreview](int){refreshPreview();});}
        if(m_anchor)connect(m_anchor,&QComboBox::currentIndexChanged,this,[refreshPreview](int){refreshPreview();});
        if(m_space){connect(m_space,&QComboBox::currentIndexChanged,this,[refreshPreview](int){refreshPreview();});connect(m_layer,&QComboBox::currentIndexChanged,this,[refreshPreview](int){refreshPreview();});connect(m_duringBattle,&QCheckBox::toggled,this,[refreshPreview](bool){refreshPreview();});connect(m_affectedByTone,&QCheckBox::toggled,this,[refreshPreview](bool){refreshPreview();});}
        for(QDoubleSpinBox* w:{m_floatSpeed,m_floatRange,m_swaySpeed,m_swayRange,m_spinSpeed,m_pulseSpeed,m_pulseRange})if(w)connect(w,&QDoubleSpinBox::valueChanged,this,[refreshPreview](double){refreshPreview();});
        refreshPreview();
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    raiz->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        QVariantMap p;
        p[QStringLiteral("number")] = m_number->value();
        if (m_tipo == QLatin1String("picture.physics")) {
            p[QStringLiteral("floatSpeed")] = m_floatSpeed->value();
            p[QStringLiteral("floatRange")] = m_floatRange->value();
            p[QStringLiteral("swaySpeed")]  = m_swaySpeed->value();
            p[QStringLiteral("swayRange")]  = m_swayRange->value();
            p[QStringLiteral("spinSpeed")]  = m_spinSpeed->value();
            p[QStringLiteral("pulseSpeed")] = m_pulseSpeed->value();
            p[QStringLiteral("pulseRange")] = m_pulseRange->value();
        } else if (m_tipo == QLatin1String("picture.anchor")) {
            p[QStringLiteral("anchor")] = m_anchor->currentData().toString();
        } else if (m_tipo == QLatin1String("picture.flip")) {
            p[QStringLiteral("flipH")] = m_flipH->isChecked();
            p[QStringLiteral("flipV")] = m_flipV->isChecked();
        } else if (m_tipo == QLatin1String("picture.negative")) {
            p[QStringLiteral("enabled")] = m_negativeEnabled->isChecked();
            p[QStringLiteral("strength")] = m_negativeStrength->value();
            p[QStringLiteral("duration")] = m_negativeDuration->value();
            p[QStringLiteral("wait")] = m_wait->isChecked();
        } else if (m_tipo == QLatin1String("picture.display")) {
            p[QStringLiteral("space")] = m_space->currentData().toString();
            p[QStringLiteral("layer")] = m_layer->currentData().toString();
            p[QStringLiteral("duringBattle")] = m_duringBattle->isChecked();
            p[QStringLiteral("eraseOnMapChange")] = m_eraseOnMapChange->isChecked();
            p[QStringLiteral("affectedByTone")] = m_affectedByTone->isChecked();
        }
        m_cmd.type = m_tipo;
        m_cmd.params = p;
        accept();
    });
}

// ============================================================================
//  Texto rico de Pictures
// ============================================================================

PictureTextPanel::PictureTextPanel(core::Editor& editorRef, core::PictureRichText& rt,
                                   QWidget* parent)
    : QWidget(parent), ed(editorRef), m_rt(rt)
{
    m_c1 = m_rt.color;
    m_c2 = m_rt.gradient2.isValid() ? m_rt.gradient2 : QColor(120, 160, 255);
    m_cCont = m_rt.outlineColor;
    m_cSombra = m_rt.shadowColor;
    m_cBg = m_rt.bgColor;
    m_cBg2 = m_rt.bgGradient2;

    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);

    auto aviso = [this] { puxar(); emit changed(); };

    m_texto = new QPlainTextEdit(m_rt.text, this);
    m_texto->setPlaceholderText(tr("Escreva o texto. Códigos: \\FC[#ff0000] cor · \\FS[32] tamanho · "
                                   "\\B negrito · \\IT itálico · \\I[3] ícone · \\v[1] variável · "
                                   "\\WV[4,6] onda · \\BO[6,8] pulo · \\SC[10,6] respiração · "
                                   "\\SP[4] girar · \\SW[40,2] brilho · \\RB[90] arco-íris · \\OFF"));
    m_texto->setMinimumHeight(90);
    connect(m_texto, &QPlainTextEdit::textChanged, this, [aviso] { aviso(); });
    raiz->addWidget(m_texto);

    auto botaoCor = [this, aviso](QColor& destino) {
        auto* b = new QPushButton(this);
        b->setFixedWidth(52);
        auto pintar = [b, &destino] {
            b->setStyleSheet(QStringLiteral("background:%1;border:1px solid #666;")
                                 .arg(destino.name()));
        };
        pintar();
        connect(b, &QPushButton::clicked, this, [this, &destino, pintar, aviso] {
            const QColor c = QColorDialog::getColor(destino, this, tr("Escolher cor"),
                                                    QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            destino = c;
            pintar();
            aviso();
        });
        return b;
    };
    auto inteiro = [this, aviso](int val, int min, int max, const QString& sufixo = QString()) {
        auto* s = new QSpinBox(this);
        s->setRange(min, max);
        s->setValue(val);
        if (!sufixo.isEmpty()) s->setSuffix(sufixo);
        connect(s, &QSpinBox::valueChanged, this, [aviso](int) { aviso(); });
        return s;
    };
    auto marcar = [this, aviso](const QString& r, bool v) {
        auto* c = new QCheckBox(r, this);
        c->setChecked(v);
        connect(c, &QCheckBox::toggled, this, [aviso](bool) { aviso(); });
        return c;
    };

    // ---- letra -----------------------------------------------------------
    auto* gL = new QGroupBox(tr("Letra"), this);
    auto* fL = new QFormLayout(gL);
    m_fonte = new QComboBox(this);
    m_fonte->setEditable(true);
    m_fonte->addItem(tr("(fonte principal do jogo)"), QString());
    for(const core::ProjectFont& pf:ed.projectFonts){QFontDatabase::addApplicationFont(QDir(ed.projectRoot()).filePath(pf.sourcePath));if(m_fonte->findText(pf.family)<0)m_fonte->addItem(tr("Projeto — %1").arg(pf.family),pf.family);}
    m_fonte->insertSeparator(m_fonte->count());
    for (const QString& f : QFontDatabase::families()) if(m_fonte->findData(f)<0)m_fonte->addItem(f, f);
    if (!m_rt.fontFamily.isEmpty()) m_fonte->setCurrentIndex(qMax(0,m_fonte->findData(m_rt.fontFamily)));
    connect(m_fonte, &QComboBox::currentTextChanged, this, [aviso](const QString&) { aviso(); });
    fL->addRow(tr("Fonte:"), m_fonte);
    m_tam = inteiro(m_rt.fontSize, 6, 200, QStringLiteral(" px"));
    fL->addRow(tr("Tamanho:"), m_tam);
    m_negrito = marcar(tr("Negrito"), m_rt.bold);
    m_italico = marcar(tr("Itálico"), m_rt.italic);
    fL->addRow(m_negrito);
    fL->addRow(m_italico);
    m_cor = botaoCor(m_c1);
    fL->addRow(tr("Cor:"), m_cor);
    m_usarGrad = marcar(tr("Usar gradiente"), m_rt.gradient2.isValid());
    fL->addRow(m_usarGrad);
    m_cor2 = botaoCor(m_c2);
    fL->addRow(tr("Cor de baixo:"), m_cor2);
    m_corContorno = botaoCor(m_cCont);
    fL->addRow(tr("Cor do contorno:"), m_corContorno);
    m_contorno = inteiro(m_rt.outlineWidth, 0, 16, QStringLiteral(" px"));
    fL->addRow(tr("Contorno:"), m_contorno);
    m_sombra = marcar(tr("Sombra"), m_rt.shadow);
    fL->addRow(m_sombra);
    m_corSombra = botaoCor(m_cSombra);
    fL->addRow(tr("Cor da sombra:"), m_corSombra);
    m_somX = inteiro(m_rt.shadowOffsetX, -32, 32, QStringLiteral(" px"));
    m_somY = inteiro(m_rt.shadowOffsetY, -32, 32, QStringLiteral(" px"));
    fL->addRow(tr("Sombra X:"), m_somX);
    fL->addRow(tr("Sombra Y:"), m_somY);
    raiz->addWidget(gL);

    core::TextGradientSpec unifiedGradient = m_rt.gradient;
    if (!unifiedGradient.enabled() && m_rt.gradient2.isValid()) {
        unifiedGradient.direction = QStringLiteral("vertical");
        unifiedGradient.colors = {m_rt.color, m_rt.gradient2};
    }
    m_textEffects = new TextEffectsEditorWidget(ed, m_rt.effects, unifiedGradient, this);
    m_textEffects->setSampleText(m_rt.text);
    connect(m_textEffects, &TextEffectsEditorWidget::changed, this, [aviso] { aviso(); });
    raiz->addWidget(m_textEffects);

    // ---- caixa -----------------------------------------------------------
    auto* gC = new QGroupBox(tr("Caixa"), this);
    auto* fC = new QFormLayout(gC);
    m_align = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureTextAlign::Right); ++i) {
        const auto a = core::PictureTextAlign(i);
        m_align->addItem(core::pictureTextAlignLabel(a), core::pictureTextAlignId(a));
    }
    m_align->setCurrentIndex(int(m_rt.align));
    connect(m_align, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
    fC->addRow(tr("Alinhamento:"), m_align);
    m_auto = marcar(tr("Tamanho automático (ajusta ao texto)"), m_rt.autoSize);
    fC->addRow(m_auto);
    m_quebra = marcar(tr("Quebrar linha sozinho"), m_rt.wordWrap);
    fC->addRow(m_quebra);
    m_larg = inteiro(m_rt.width, 16, 4000, QStringLiteral(" px"));
    m_alt = inteiro(m_rt.height, 16, 4000, QStringLiteral(" px"));
    fC->addRow(tr("Largura:"), m_larg);
    fC->addRow(tr("Altura:"), m_alt);
    m_padding = inteiro(m_rt.padding, 0, 128, QStringLiteral(" px"));
    fC->addRow(tr("Margem interna:"), m_padding);
    raiz->addWidget(gC);

    // ---- fundo -----------------------------------------------------------
    auto* gF = new QGroupBox(tr("Fundo"), this);
    auto* fF = new QFormLayout(gF);
    m_bg = new QComboBox(this);
    for (int i = 0; i <= int(core::PictureTextBg::Window); ++i) {
        const auto b = core::PictureTextBg(i);
        m_bg->addItem(core::pictureTextBgLabel(b), core::pictureTextBgId(b));
    }
    m_bg->setCurrentIndex(int(m_rt.bg));
    connect(m_bg, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
    fF->addRow(tr("Estilo:"), m_bg);
    m_corBg = botaoCor(m_cBg);
    fF->addRow(tr("Cor:"), m_corBg);
    m_corBg2 = botaoCor(m_cBg2);
    fF->addRow(tr("Cor de baixo:"), m_corBg2);
    m_raio = inteiro(m_rt.bgRadius, 0, 64, QStringLiteral(" px"));
    fF->addRow(tr("Cantos arredondados:"), m_raio);
    m_bgAlfa = new QDoubleSpinBox(this);
    m_bgAlfa->setRange(0.0, 1.0);
    m_bgAlfa->setDecimals(2);
    m_bgAlfa->setSingleStep(0.05);
    m_bgAlfa->setValue(m_rt.bgAlpha);
    connect(m_bgAlfa, &QDoubleSpinBox::valueChanged, this, [aviso](double) { aviso(); });
    fF->addRow(tr("Opacidade:"), m_bgAlfa);
    m_bgBorda = new QComboBox(this);
    m_bgBorda->addItem(tr("(nenhuma)"), QString());
    for (const core::PictureAsset& a : ed.pictures) m_bgBorda->addItem(a.name, a.id);
    {
        const int i = ed.pictureIndexById(m_rt.bgBorderImageId);
        m_bgBorda->setCurrentIndex(i >= 0 ? i + 1 : 0);
    }
    connect(m_bgBorda, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
    fF->addRow(tr("Moldura da janela:"), m_bgBorda);
    m_bgSlice = inteiro(m_rt.bgBorderSlice, 1, 128, QStringLiteral(" px"));
    fF->addRow(tr("Canto da moldura:"), m_bgSlice);
    m_bgEscala = new QDoubleSpinBox(this);
    m_bgEscala->setRange(0.25, 8.0);
    m_bgEscala->setDecimals(2);
    m_bgEscala->setValue(m_rt.bgBorderScale);
    connect(m_bgEscala, &QDoubleSpinBox::valueChanged, this, [aviso](double) { aviso(); });
    fF->addRow(tr("Escala da moldura:"), m_bgEscala);
    raiz->addWidget(gF);

    auto* nota = new QLabel(tr("O <b>vidro fosco</b> não borra o cenário atrás: a imagem é "
                               "composta sozinha (e vira textura na GPU). Ele clareia e "
                               "ilumina a borda, que é o que dá a leitura de vidro."), this);
    nota->setWordWrap(true);
    nota->setStyleSheet(QStringLiteral("color:#888;"));
    raiz->addWidget(nota);
    raiz->addStretch(1);
    puxar();
}

void PictureTextPanel::puxar()
{
    m_rt.enabled = true;
    m_rt.text = m_texto->toPlainText();
    m_rt.fontFamily = m_fonte->currentData().toString();
    m_rt.fontSize = m_tam->value();
    m_rt.bold = m_negrito->isChecked();
    m_rt.italic = m_italico->isChecked();
    m_rt.color = m_c1;
    m_rt.gradient2 = m_usarGrad->isChecked() ? m_c2 : QColor(); // compatibilidade legada
    m_rt.gradient = m_textEffects ? m_textEffects->gradient() : core::TextGradientSpec{};
    m_rt.effects = m_textEffects ? m_textEffects->effects() : core::TextEffectStack{};
    if (m_textEffects) m_textEffects->setSampleText(m_rt.text);
    m_rt.outlineColor = m_cCont;
    m_rt.outlineWidth = m_contorno->value();
    m_rt.shadow = m_sombra->isChecked();
    m_rt.shadowColor = m_cSombra;
    m_rt.shadowOffsetX = m_somX->value();
    m_rt.shadowOffsetY = m_somY->value();
    m_rt.wordWrap = m_quebra->isChecked();
    m_rt.autoSize = m_auto->isChecked();
    m_rt.width = m_larg->value();
    m_rt.height = m_alt->value();
    m_rt.padding = m_padding->value();
    m_rt.align = core::pictureTextAlignFromId(m_align->currentData().toString());
    m_rt.bg = core::pictureTextBgFromId(m_bg->currentData().toString());
    m_rt.bgColor = m_cBg;
    m_rt.bgGradient2 = m_cBg2;
    m_rt.bgRadius = m_raio->value();
    m_rt.bgAlpha = m_bgAlfa->value();
    m_rt.bgBorderImageId = m_bgBorda->currentData().toString();
    m_rt.bgBorderSlice = m_bgSlice->value();
    m_rt.bgBorderScale = m_bgEscala->value();
}

} // namespace ui
