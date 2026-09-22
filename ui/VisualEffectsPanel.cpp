#include "VisualEffectsPanel.h"
#include "AssetBrowser.h"
#include "core/ResourceManager.h"

#include "core/Editor.h"
#include "core/Picture.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ui {

VisualEffectsPanel::VisualEffectsPanel(core::Editor& editorRef, core::VisualEffects& fx, QWidget* parent)
    : QWidget(parent), ed(editorRef), m_fx(fx)
{
    m_corBorda = m_fx.border.color;
    m_corTint = m_fx.border.tint;
    m_corBrilho = m_fx.glow.color;
    m_corDesliza = m_fx.shine.color;
    m_corColorizar = m_fx.tint.color;

    auto* raiz = new QVBoxLayout(this);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(6);

    auto aviso = [this] { puxar(); emit changed(); };
    auto num = [this, aviso](double val, double min, double max, const QString& sufixo, int casas = 1) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(min, max);
        s->setDecimals(casas);
        s->setValue(val);
        if (!sufixo.isEmpty()) s->setSuffix(sufixo);
        connect(s, &QDoubleSpinBox::valueChanged, this, [aviso](double) { aviso(); });
        return s;
    };
    auto inteiro = [this, aviso](int val, int min, int max, const QString& sufixo = QString()) {
        auto* s = new QSpinBox(this);
        s->setRange(min, max);
        s->setValue(val);
        if (!sufixo.isEmpty()) s->setSuffix(sufixo);
        connect(s, &QSpinBox::valueChanged, this, [aviso](int) { aviso(); });
        return s;
    };
    auto marcar = [this, aviso](const QString& rotulo, bool ligado) {
        auto* c = new QCheckBox(rotulo, this);
        c->setChecked(ligado);
        connect(c, &QCheckBox::toggled, this, [aviso](bool) { aviso(); });
        return c;
    };
    // Botão de cor: guarda a cor num membro (variável local morreria antes do
    // clique — erro que já apareceu no diálogo de legendas).
    auto botaoCor = [this, aviso](QColor& destino) {
        auto* b = new QPushButton(this);
        b->setFixedWidth(52);
        auto pintar = [b, &destino] {
            b->setStyleSheet(QStringLiteral("background:%1;border:1px solid #666;")
                                 .arg(destino.name()));
        };
        pintar();
        connect(b, &QPushButton::clicked, this, [this, &destino, pintar, aviso] {
            const QColor c = QColorDialog::getColor(destino, this, tr("Escolher cor"));
            if (!c.isValid()) return;
            destino = c;
            pintar();
            aviso();
        });
        return b;
    };
    auto listaImagens = [this, aviso](const QString& atual) {
        auto* c = new QComboBox(this);
        c->addItem(tr("(nenhuma)"), QString());
        for (const core::PictureAsset& a : ed.pictures) c->addItem(a.name, a.id);
        const int i = ed.pictureIndexById(atual);
        c->setCurrentIndex(i >= 0 ? i + 1 : 0);
        connect(c, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
        return c;
    };

    // ---- borda -----------------------------------------------------------
    auto* gb = new QGroupBox(tr("Borda"), this);
    auto* fb = new QFormLayout(gb);
    m_borda = marcar(tr("Usar borda"), m_fx.border.enabled);
    fb->addRow(m_borda);
    m_bordaEstilo = new QComboBox(this);
    m_bordaEstilo->addItem(core::pictureBorderStyleLabel(core::PictureBorderStyle::Simple),
                           QStringLiteral("simple"));
    m_bordaEstilo->addItem(core::pictureBorderStyleLabel(core::PictureBorderStyle::NineSlice),
                           QStringLiteral("image"));
    m_bordaEstilo->setCurrentIndex(m_fx.border.style == core::PictureBorderStyle::NineSlice ? 1 : 0);
    connect(m_bordaEstilo, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
    fb->addRow(tr("Estilo:"), m_bordaEstilo);
    m_bordaLarg = num(m_fx.border.width, 0.5, 64, QStringLiteral(" px"));
    fb->addRow(tr("Espessura:"), m_bordaLarg);
    m_bordaCor = botaoCor(m_corBorda);
    fb->addRow(tr("Cor:"), m_bordaCor);
    m_bordaAlfa = num(m_fx.border.alpha, 0.0, 1.0, QString(), 2);
    fb->addRow(tr("Opacidade da borda:"), m_bordaAlfa);
    m_bordaImg = listaImagens(m_fx.border.imageAssetId);
    auto* bordaImagemRow = new QWidget(gb);
    auto* bordaImagemLay = new QHBoxLayout(bordaImagemRow);
    bordaImagemLay->setContentsMargins(0, 0, 0, 0);
    bordaImagemLay->addWidget(m_bordaImg, 1);
    auto* escolherBorda = new QPushButton(tr("Selecionar…"), bordaImagemRow);
    bordaImagemLay->addWidget(escolherBorda);
    fb->addRow(tr("Imagem da moldura:"), bordaImagemRow);
    connect(escolherBorda, &QPushButton::clicked, this, [this, aviso] {
        const QString id = AssetBrowserDialog::choosePictureAssetId(ed, this);
        if (id.isEmpty()) return;
        QSignalBlocker blocker(m_bordaImg);
        m_bordaImg->clear(); m_bordaImg->addItem(tr("(nenhuma)"), QString());
        for (const core::PictureAsset& a : ed.pictures) m_bordaImg->addItem(a.name, a.id);
        const int i = ed.pictureIndexById(id);
        m_bordaImg->setCurrentIndex(i >= 0 ? i + 1 : 0);
        aviso();
    });
    m_bordaSlice = inteiro(m_fx.border.slice, 1, 128, QStringLiteral(" px"));
    fb->addRow(tr("Canto (slice):"), m_bordaSlice);
    m_bordaEscala = num(m_fx.border.scale, 0.25, 16, QStringLiteral("×"), 2);
    fb->addRow(tr("Escala da moldura:"), m_bordaEscala);
    m_bordaPadX = inteiro(m_fx.border.paddingX, -100000, 100000, QStringLiteral(" px"));
    fb->addRow(tr("Folga horizontal:"), m_bordaPadX);
    m_bordaPadY = inteiro(m_fx.border.paddingY, -100000, 100000, QStringLiteral(" px"));
    fb->addRow(tr("Folga vertical:"), m_bordaPadY);
    m_bordaTint = botaoCor(m_corTint);
    fb->addRow(tr("Tingir moldura:"), m_bordaTint);
    m_bordaTileH = marcar(tr("Repetir na horizontal"), m_fx.border.tileH);
    m_bordaTileV = marcar(tr("Repetir na vertical"), m_fx.border.tileV);
    fb->addRow(m_bordaTileH);
    fb->addRow(m_bordaTileV);
    raiz->addWidget(gb);

    // ---- brilho ----------------------------------------------------------
    auto* gg = new QGroupBox(tr("Brilho (glow)"), this);
    auto* fg = new QFormLayout(gg);
    m_brilho = marcar(tr("Usar brilho"), m_fx.glow.enabled);
    fg->addRow(m_brilho);
    m_brilhoCor = botaoCor(m_corBrilho);
    fg->addRow(tr("Cor:"), m_brilhoCor);
    m_brilhoForca = num(m_fx.glow.strength, 1, 30, QStringLiteral(" px"));
    fg->addRow(tr("Força:"), m_brilhoForca);
    m_brilhoBorda = marcar(tr("Aplicar brilho somente à borda"), m_fx.glow.borderOnly);
    m_brilhoBorda->setToolTip(tr("Usa a borda/moldura como fonte do Glow; o conteúdo da Picture não recebe halo."));
    fg->addRow(m_brilhoBorda);
    m_brilhoPiscar = marcar(tr("Piscar / pulsar brilho"), m_fx.glow.blink);
    fg->addRow(m_brilhoPiscar);
    m_brilhoPiscarVel = num(m_fx.glow.blinkSpeed, 0.1, 10, tr(" ciclos/s"), 2);
    fg->addRow(tr("Velocidade do piscar:"), m_brilhoPiscarVel);
    raiz->addWidget(gg);

    // ---- tonalidade -------------------------------------------------------
    auto* gc = new QGroupBox(tr("Tonalidade"), this);
    auto* fc = new QFormLayout(gc);
    m_tone = marcar(tr("Aplicar tonalidade"), m_fx.tone.enabled);
    fc->addRow(m_tone);
    m_toneR = inteiro(m_fx.tone.red, -255, 255);
    m_toneG = inteiro(m_fx.tone.green, -255, 255);
    m_toneB = inteiro(m_fx.tone.blue, -255, 255);
    m_toneGray = inteiro(m_fx.tone.gray, 0, 255);
    fc->addRow(tr("Vermelho:"), m_toneR);
    fc->addRow(tr("Verde:"), m_toneG);
    fc->addRow(tr("Azul:"), m_toneB);
    fc->addRow(tr("Cinza:"), m_toneGray);
    auto* toneHint = new QLabel(tr("Usa o mesmo modelo R/G/B/Cinza da Tonalidade da Tela da LUDO. Projetos antigos com Colorizar/Tint continuam carregando por compatibilidade."), gc);
    toneHint->setWordWrap(true);
    fc->addRow(toneHint);
    raiz->addWidget(gc);

    // ---- negativo --------------------------------------------------------
    auto* gn = new QGroupBox(tr("Negative / Inverter cores"), this);
    auto* fn = new QFormLayout(gn);
    m_negative = marcar(tr("Usar negativo"), m_fx.negative.enabled);
    fn->addRow(m_negative);
    m_negativeForca = num(m_fx.negative.strength, 0.0, 1.0, QString(), 2);
    m_negativeForca->setToolTip(tr("0 = imagem original; 1 = inversão completa."));
    fn->addRow(tr("Intensidade:"), m_negativeForca);
    m_negativeTransicao = inteiro(m_fx.negative.transitionFrames, 0, 3600, tr(" quadros"));
    m_negativeTransicao->setToolTip(tr("0 aplica imediatamente. Valores maiores fazem uma transição suave até o negativo."));
    fn->addRow(tr("Transição:"), m_negativeTransicao);
    raiz->addWidget(gn);

    // ---- piscar ----------------------------------------------------------
    auto* gp = new QGroupBox(tr("Piscar"), this);
    auto* fp = new QFormLayout(gp);
    m_piscar = marcar(tr("Ficar piscando"), m_fx.blink.enabled);
    fp->addRow(m_piscar);
    m_piscarModo = new QComboBox(this);
    m_piscarModo->addItem(tr("Suave (vai e volta)"), false);
    m_piscarModo->addItem(tr("Direto (liga e desliga)"), true);
    m_piscarModo->setCurrentIndex(m_fx.blink.hard ? 1 : 0);
    connect(m_piscarModo, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
    fp->addRow(tr("Modo:"), m_piscarModo);
    m_piscarVel = num(m_fx.blink.speed, 0.1, 10, QString(), 2);
    fp->addRow(tr("Velocidade:"), m_piscarVel);
    m_piscarMin = num(m_fx.blink.minAlpha, 0.0, 1.0, QString(), 2);
    fp->addRow(tr("Opacidade mínima:"), m_piscarMin);
    m_piscarDelay = num(m_fx.blink.delay, 0.0, 30.0, tr(" s"), 2);
    fp->addRow(tr("Delay entre piscadas:"), m_piscarDelay);
    raiz->addWidget(gp);

    // ---- onda ------------------------------------------------------------
    auto* go = new QGroupBox(tr("Onda (distorção)"), this);
    auto* fo = new QFormLayout(go);
    m_onda = marcar(tr("Ondular"), m_fx.distort.enabled);
    fo->addRow(m_onda);
    m_ondaAmp = num(m_fx.distort.amplitude, 0, 128, QStringLiteral(" px"));
    fo->addRow(tr("Amplitude:"), m_ondaAmp);
    m_ondaComp = num(m_fx.distort.wavelength, 2, 512, QStringLiteral(" px"));
    fo->addRow(tr("Comprimento:"), m_ondaComp);
    m_ondaVel = num(m_fx.distort.speed, -10, 10, tr(" ciclos/s"), 2);
    fo->addRow(tr("Velocidade:"), m_ondaVel);
    raiz->addWidget(go);

    // ---- brilho deslizante ------------------------------------------------
    auto* gs = new QGroupBox(tr("Brilho deslizante"), this);
    auto* fs = new QFormLayout(gs);
    m_desliza = marcar(tr("Passar um brilho"), m_fx.shine.enabled);
    fs->addRow(m_desliza);
    m_deslizaCor = botaoCor(m_corDesliza);
    fs->addRow(tr("Cor:"), m_deslizaCor);
    m_deslizaVel = num(m_fx.shine.speed, 0.1, 10, tr(" passagens/s"), 2);
    fs->addRow(tr("Velocidade:"), m_deslizaVel);
    m_deslizaLarg = num(m_fx.shine.width, 2, 400, QStringLiteral(" px"));
    fs->addRow(tr("Largura:"), m_deslizaLarg);
    m_deslizaDelay = num(m_fx.shine.delay, 0, 10, tr(" s"), 2);
    m_deslizaDelay->setToolTip(tr("Pausa depois que o brilho sai completamente da imagem."));
    fs->addRow(tr("Delay entre brilhos:"), m_deslizaDelay);
    raiz->addWidget(gs);

    // ---- máscara ----------------------------------------------------------
    auto* gm = new QGroupBox(tr("Máscara"), this);
    auto* fm = new QFormLayout(gm);
    m_mascara = marcar(tr("Recortar com outra imagem"), m_fx.mask.enabled);
    fm->addRow(m_mascara);
    m_mascaraImg = listaImagens(m_fx.mask.assetId);
    auto* mascaraImagemRow = new QWidget(gm);
    auto* mascaraImagemLay = new QHBoxLayout(mascaraImagemRow);
    mascaraImagemLay->setContentsMargins(0, 0, 0, 0);
    mascaraImagemLay->addWidget(m_mascaraImg, 1);
    auto* escolherMascara = new QPushButton(tr("Selecionar…"), mascaraImagemRow);
    mascaraImagemLay->addWidget(escolherMascara);
    fm->addRow(tr("Imagem:"), mascaraImagemRow);
    connect(escolherMascara, &QPushButton::clicked, this, [this, aviso] {
        const QString id = AssetBrowserDialog::choosePictureAssetId(ed, this);
        if (id.isEmpty()) return;
        QSignalBlocker blocker(m_mascaraImg);
        m_mascaraImg->clear(); m_mascaraImg->addItem(tr("(nenhuma)"), QString());
        for (const core::PictureAsset& a : ed.pictures) m_mascaraImg->addItem(a.name, a.id);
        const int i = ed.pictureIndexById(id);
        m_mascaraImg->setCurrentIndex(i >= 0 ? i + 1 : 0);
        aviso();
    });
    m_mascaraInv = marcar(tr("Recortar por fora"), m_fx.mask.invert);
    fm->addRow(m_mascaraInv);
    m_mascaraX = num(m_fx.mask.offsetX, -10000, 10000, QStringLiteral(" px"));
    m_mascaraY = num(m_fx.mask.offsetY, -10000, 10000, QStringLiteral(" px"));
    m_mascaraEscalaX = num(m_fx.mask.scaleX, 1, 1000, QStringLiteral(" %"));
    m_mascaraEscalaY = num(m_fx.mask.scaleY, 1, 1000, QStringLiteral(" %"));
    m_mascaraAngulo = num(m_fx.mask.angle, -3600, 3600, QStringLiteral("°"));
    fm->addRow(tr("Deslocamento X:"), m_mascaraX);
    fm->addRow(tr("Deslocamento Y:"), m_mascaraY);
    fm->addRow(tr("Escala X:"), m_mascaraEscalaX);
    fm->addRow(tr("Escala Y:"), m_mascaraEscalaY);
    fm->addRow(tr("Rotação:"), m_mascaraAngulo);
    auto* maskHint = new QLabel(tr("A transformação da máscara é pré-composta e mantida em cache; não gera trabalho extra por quadro."), gm);
    maskHint->setWordWrap(true); maskHint->setStyleSheet(QStringLiteral("color:#888"));
    fm->addRow(maskHint);
    auto* atualizarListas = new QPushButton(tr("Atualizar listas de imagens"), this);
    fm->addRow(QString(), atualizarListas);
    connect(atualizarListas, &QPushButton::clicked, this, [this, aviso] {
        auto atualizar = [this](QComboBox* combo, const QString& atual) {
            QSignalBlocker blocker(combo);
            combo->clear();
            combo->addItem(tr("(nenhuma)"), QString());
            for (const core::PictureAsset& a : ed.pictures) combo->addItem(a.name, a.id);
            const int i = ed.pictureIndexById(atual);
            combo->setCurrentIndex(i >= 0 ? i + 1 : 0);
        };
        atualizar(m_bordaImg, m_bordaImg->currentData().toString());
        atualizar(m_mascaraImg, m_mascaraImg->currentData().toString());
        aviso();
    });
    raiz->addWidget(gm);
    connect(&ed.resources(), &core::ResourceManager::pictureLibraryChanged, this, [this, aviso] {
        auto atualizar = [this](QComboBox* combo) {
            const QString atual = combo->currentData().toString();
            QSignalBlocker blocker(combo);combo->clear();combo->addItem(tr("(nenhuma)"), QString());
            for (const core::PictureAsset& a : ed.pictures) combo->addItem(a.name, a.id);
            combo->setCurrentIndex(qMax(0, combo->findData(atual)));
        };
        atualizar(m_bordaImg);atualizar(m_mascaraImg);aviso();
    });

    // ---- transições --------------------------------------------------------
    auto* gt = new QGroupBox(tr("Transições"), this);
    auto* ft = new QFormLayout(gt);
    auto listaTrans = [this, aviso](core::PictureTransition atual) {
        auto* c = new QComboBox(this);
        for (int i = 0; i <= int(core::PictureTransition::Bounce); ++i) {
            const auto t = core::PictureTransition(i);
            c->addItem(core::pictureTransitionLabel(t), core::pictureTransitionId(t));
        }
        c->setCurrentIndex(int(atual));
        connect(c, &QComboBox::currentIndexChanged, this, [aviso](int) { aviso(); });
        return c;
    };
    m_transIn = listaTrans(m_fx.transitionIn);
    ft->addRow(tr("Ao aparecer:"), m_transIn);
    m_transInQ = inteiro(m_fx.transitionInFrames, 1, 600, tr(" quadros"));
    ft->addRow(tr("Duração:"), m_transInQ);
    auto* previewIn = new QPushButton(QStringLiteral("▶ IN"), this);
    ft->addRow(tr("Prévia de entrada:"), previewIn);
    connect(previewIn, &QPushButton::clicked, this, [this, aviso] { aviso(); emit previewTransitionIn(); });
    m_transOut = listaTrans(m_fx.transitionOut);
    ft->addRow(tr("Ao sumir:"), m_transOut);
    m_transOutQ = inteiro(m_fx.transitionOutFrames, 1, 600, tr(" quadros"));
    ft->addRow(tr("Duração:"), m_transOutQ);
    auto* previewOut = new QPushButton(QStringLiteral("▶ OUT"), this);
    ft->addRow(tr("Prévia de saída:"), previewOut);
    connect(previewOut, &QPushButton::clicked, this, [this, aviso] { aviso(); emit previewTransitionOut(); });
    auto* dicaT = new QLabel(tr("A saída só acontece quando um evento manda "
                                "<i>Sumir com a imagem</i>."), this);
    dicaT->setWordWrap(true);
    dicaT->setStyleSheet(QStringLiteral("color:#888;"));
    ft->addRow(dicaT);
    raiz->addWidget(gt);
    raiz->addStretch(1);

    puxar();
}

void VisualEffectsPanel::puxar()
{
    m_fx.border.enabled = m_borda->isChecked();
    m_fx.border.style = core::pictureBorderStyleFromId(m_bordaEstilo->currentData().toString());
    m_fx.border.width = m_bordaLarg->value();
    m_fx.border.color = m_corBorda;
    m_fx.border.alpha = m_bordaAlfa->value();
    m_fx.border.imageAssetId = m_bordaImg->currentData().toString();
    m_fx.border.slice = m_bordaSlice->value();
    m_fx.border.scale = m_bordaEscala->value();
    m_fx.border.paddingX = m_bordaPadX->value();
    m_fx.border.paddingY = m_bordaPadY->value();
    m_fx.border.tint = m_corTint;
    m_fx.border.tileH = m_bordaTileH->isChecked();
    m_fx.border.tileV = m_bordaTileV->isChecked();

    m_fx.glow.enabled = m_brilho->isChecked();
    m_fx.glow.color = m_corBrilho;
    m_fx.glow.strength = m_brilhoForca->value();
    m_fx.glow.borderOnly = m_brilhoBorda->isChecked();
    m_fx.glow.blink = m_brilhoPiscar->isChecked();
    m_fx.glow.blinkSpeed = m_brilhoPiscarVel->value();

    m_fx.tone.enabled = m_tone->isChecked();
    m_fx.tone.red = m_toneR->value();
    m_fx.tone.green = m_toneG->value();
    m_fx.tone.blue = m_toneB->value();
    m_fx.tone.gray = m_toneGray->value();
    // Não criamos mais Tint/Colorizar novo. O campo legado permanece intacto
    // e invisível para que abrir/aceitar um projeto antigo nunca destrua o efeito.

    m_fx.negative.enabled = m_negative->isChecked();
    m_fx.negative.strength = m_negativeForca->value();
    m_fx.negative.transitionFrames = m_negativeTransicao->value();

    m_fx.blink.enabled = m_piscar->isChecked();
    m_fx.blink.hard = m_piscarModo->currentData().toBool();
    m_fx.blink.speed = m_piscarVel->value();
    m_fx.blink.minAlpha = m_piscarMin->value();
    m_fx.blink.delay = m_piscarDelay->value();

    m_fx.distort.enabled = m_onda->isChecked();
    m_fx.distort.amplitude = m_ondaAmp->value();
    m_fx.distort.wavelength = m_ondaComp->value();
    m_fx.distort.speed = m_ondaVel->value();

    m_fx.shine.enabled = m_desliza->isChecked();
    m_fx.shine.color = m_corDesliza;
    m_fx.shine.speed = m_deslizaVel->value();
    m_fx.shine.width = m_deslizaLarg->value();
    m_fx.shine.delay = m_deslizaDelay->value();

    m_fx.mask.enabled = m_mascara->isChecked();
    m_fx.mask.assetId = m_mascaraImg->currentData().toString();
    m_fx.mask.invert = m_mascaraInv->isChecked();
    m_fx.mask.offsetX = m_mascaraX->value();
    m_fx.mask.offsetY = m_mascaraY->value();
    m_fx.mask.scaleX = m_mascaraEscalaX->value();
    m_fx.mask.scaleY = m_mascaraEscalaY->value();
    m_fx.mask.angle = m_mascaraAngulo->value();

    m_fx.transitionIn = core::pictureTransitionFromId(m_transIn->currentData().toString());
    m_fx.transitionInFrames = m_transInQ->value();
    m_fx.transitionOut = core::pictureTransitionFromId(m_transOut->currentData().toString());
    m_fx.transitionOutFrames = m_transOutQ->value();
}


} // namespace ui
