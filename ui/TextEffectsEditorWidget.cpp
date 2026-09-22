#include "TextEffectsEditorWidget.h"

#include "game/TextBox.h"
#include "game/TextDraw.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <initializer_list>
#include <utility>

namespace ui {
namespace {

class TextEffectsPreview final : public QWidget
{
public:
    TextEffectsPreview(core::Editor& editor, QWidget* parent = nullptr)
        : QWidget(parent), ed(editor)
    {
        setMinimumHeight(118);
        clock.start();
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
        timer->start(33);
    }

    void setData(QString text, core::TextEffectStack effects, core::TextGradientSpec gradient,
                 QString phaseId)
    {
        raw = std::move(text);
        fx = std::move(effects);
        grad = std::move(gradient);
        phase = std::move(phaseId);
        restart();
    }

    void restart()
    {
        frozenSec = 0.0;
        baseSec = 0.0;
        running = true;
        clock.restart();
        update();
    }

    void setPaused(bool paused)
    {
        if (paused == !running) return;
        if (paused) {
            frozenSec = currentSeconds();
            running = false;
        } else {
            baseSec = frozenSec;
            clock.restart();
            running = true;
        }
        update();
    }

    bool paused() const { return !running; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Base));
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(rect().adjusted(0,0,-1,-1));

        QFont f = font();
        f.setPixelSize(qMax(12, ed.gameUi.fontSize));
        const auto pages = game::layoutMessage(raw.isEmpty() ? tr("Texto de exemplo") : raw,
                                               f, qMax(40, width()-24), 4, {}, &ed.iconSet);
        if (pages.isEmpty()) return;

        // O preview mostra exatamente a fase selecionada. Isso evita que, ao
        // editar "Saída", o usuário precise esperar Entrada + Loop para vê-la.
        core::TextEffectStack previewFx;
        game::TextDrawOpts opt;
        const double t = currentSeconds();
        if (phase == QLatin1String("loop")) {
            previewFx.loop = fx.loop;
            opt.time = t;
        } else if (phase == QLatin1String("exit")) {
            previewFx.exit = fx.exit;
            opt.time = 0.0;
            opt.exitTime = t;
        } else {
            previewFx.entrance = fx.entrance;
            opt.time = t;
        }

        opt.color = palette().color(QPalette::Text);
        opt.icons = &ed.iconSet;
        opt.effects = previewFx;
        opt.gradient = grad;
        game::drawTextPage(p, pages.front(), f, QRectF(12,12,width()-24,height()-24), opt);
    }

private:
    double currentSeconds() const
    {
        return running ? baseSec + clock.elapsed()/1000.0 : frozenSec;
    }

    core::Editor& ed;
    QString raw = QStringLiteral("LUDO Game Engine");
    core::TextEffectStack fx;
    core::TextGradientSpec grad;
    QString phase = QStringLiteral("entrance");
    QElapsedTimer clock;
    bool running = true;
    double baseSec = 0.0;
    double frozenSec = 0.0;
};

QDoubleSpinBox* realSpin(QWidget* parent, double min, double max, double step, int decimals=2)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min,max); s->setSingleStep(step); s->setDecimals(decimals);
    return s;
}

QString colorLabel(const QColor& c)
{
    return c.isValid() ? c.name(QColor::HexArgb) : QObject::tr("(inválida)");
}

} // namespace

TextEffectsEditorWidget::TextEffectsEditorWidget(core::Editor& editor,
                                                 const core::TextEffectStack& effects,
                                                 const core::TextGradientSpec& gradient,
                                                 QWidget* parent)
    : QWidget(parent), ed(editor), m_effects(effects), m_gradient(gradient)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);

    auto* rich = new QGroupBox(tr("Rich Text · Gradiente"), this);
    auto* rg = new QGridLayout(rich);
    m_gradientEnabled = new QCheckBox(tr("Usar gradiente"), rich);
    m_gradientEnabled->setChecked(m_gradient.enabled());
    m_gradientDirection = new QComboBox(rich);
    m_gradientDirection->addItem(tr("Vertical"), QStringLiteral("vertical"));
    m_gradientDirection->addItem(tr("Horizontal"), QStringLiteral("horizontal"));
    m_gradientDirection->addItem(tr("Diagonal"), QStringLiteral("diagonal"));
    m_gradientDirection->setCurrentIndex(qMax(0,m_gradientDirection->findData(m_gradient.direction)));
    m_gradientColors = new QListWidget(rich);
    m_gradientColors->setMaximumHeight(78);
    auto* addColor = new QPushButton(tr("+ Cor"), rich);
    auto* editColor = new QPushButton(tr("Editar"), rich);
    auto* delColor = new QPushButton(tr("Remover"), rich);
    rg->addWidget(m_gradientEnabled,0,0);
    rg->addWidget(new QLabel(tr("Direção:"),rich),0,1);
    rg->addWidget(m_gradientDirection,0,2);
    rg->addWidget(m_gradientColors,1,0,1,3);
    auto* cr = new QHBoxLayout; cr->addWidget(addColor);cr->addWidget(editColor);cr->addWidget(delColor);cr->addStretch();
    rg->addLayout(cr,2,0,1,3);
    root->addWidget(rich);

    auto* group = new QGroupBox(tr("Efeitos de texto"), this);
    auto* form = new QFormLayout(group);
    m_phase = new QComboBox(group);
    m_phase->addItem(tr("Entrada"),QStringLiteral("entrance"));
    m_phase->addItem(tr("Permanência / Loop"),QStringLiteral("loop"));
    m_phase->addItem(tr("Saída"),QStringLiteral("exit"));
    form->addRow(tr("Fase:"),m_phase);
    m_enabled = new QCheckBox(tr("Ativar esta fase"),group); form->addRow(m_enabled);
    m_continuous = new QCheckBox(tr("Efeito contínuo enquanto estiver visível"), group);
    m_continuous->setToolTip(tr("Mantém o efeito em movimento em vez de parar no estado final."));
    form->addRow(m_continuous);
    m_fadeReveal = new QCheckBox(tr("Fade ao revelar as letras"), group);
    m_fadeReveal->setToolTip(tr("Cada letra aparece com uma transição curta de opacidade quando é revelada."));
    m_fadeRevealMs = new QSpinBox(group); m_fadeRevealMs->setRange(1,5000); m_fadeRevealMs->setSuffix(tr(" ms"));
    auto* revealFadeRow = new QHBoxLayout; revealFadeRow->addWidget(m_fadeReveal); revealFadeRow->addWidget(m_fadeRevealMs); revealFadeRow->addStretch();
    form->addRow(tr("Transição das letras:"), revealFadeRow);
    auto* presetRow = new QHBoxLayout;
    m_preset = new QComboBox(group);
    m_preset->addItem(tr("(personalizado / nenhum)"),QString());
    for (const auto& p : core::builtInTextEffectPresets()) m_preset->addItem(p.name,p.id);
    if (!ed.textEffectPresets.isEmpty()) {
        m_preset->insertSeparator(m_preset->count());
        QStringList ids=ed.textEffectPresets.keys(); ids.sort(Qt::CaseInsensitive);
        for (const QString& id : ids) m_preset->addItem(tr("Meu preset — %1").arg(ed.textEffectPresets.value(id).name),id);
    }
    auto* apply = new QPushButton(tr("Aplicar"),group);
    auto* save = new QPushButton(tr("Salvar preset…"),group);
    presetRow->addWidget(m_preset,1);presetRow->addWidget(apply);presetRow->addWidget(save);
    form->addRow(tr("Preset:"),presetRow);

    m_target = new QComboBox(group);
    m_target->addItem(tr("Caractere"),QStringLiteral("character"));m_target->addItem(tr("Palavra"),QStringLiteral("word"));m_target->addItem(tr("Bloco"),QStringLiteral("block"));
    m_motion = new QComboBox(group);
    const std::initializer_list<std::pair<const char*,const char*>> motions = {
        {"tween","Interpolação"},{"wave","Wave / Onda"},{"shake","Shake / Tremida"},{"float","Float / Flutuação"},
        {"jitter","Jitter"},{"pulse","Pulse / Breathing"},{"rainbow","Rainbow"},{"glow","Glow"},
        {"sweep","Gradient Sweep"},{"typewriter","Typewriter"}};
    for (const auto& m:motions) m_motion->addItem(tr(m.second),QLatin1String(m.first));
    m_easing = new QComboBox(group);
    for (const char* e : {"linear","quad-in","quad-out","quad-in-out","expo-out","back-out","elastic-out"})
        m_easing->addItem(core::textEffectEasingLabel(QLatin1String(e)),QLatin1String(e));
    m_loopMode = new QComboBox(group);
    for (const char* l : {"once","while-visible","count","ping-pong"}) m_loopMode->addItem(core::textEffectLoopLabel(QLatin1String(l)),QLatin1String(l));
    m_loopCount = new QSpinBox(group);m_loopCount->setRange(1,9999);
    m_duration = new QSpinBox(group);m_duration->setRange(1,60000);m_duration->setSuffix(tr(" ms"));
    m_delay = new QSpinBox(group);m_delay->setRange(0,60000);m_delay->setSuffix(tr(" ms"));
    m_stagger = new QSpinBox(group);m_stagger->setRange(0,10000);m_stagger->setSuffix(tr(" ms"));
    m_opacityFrom=realSpin(group,0,1,.05);m_opacityTo=realSpin(group,0,1,.05);
    m_xFrom=realSpin(group,-10000,10000,1);m_xTo=realSpin(group,-10000,10000,1);
    m_yFrom=realSpin(group,-10000,10000,1);m_yTo=realSpin(group,-10000,10000,1);
    m_scaleXFrom=realSpin(group,.01,20,.05);m_scaleXTo=realSpin(group,.01,20,.05);
    m_scaleYFrom=realSpin(group,.01,20,.05);m_scaleYTo=realSpin(group,.01,20,.05);
    m_rotationFrom=realSpin(group,-3600,3600,1);m_rotationTo=realSpin(group,-3600,3600,1);
    m_amount=realSpin(group,0,10000,.1);m_frequency=realSpin(group,.01,1000,.1);
    form->addRow(tr("Alvo:"),m_target);form->addRow(tr("Movimento:"),m_motion);form->addRow(tr("Easing:"),m_easing);
    auto* times=new QHBoxLayout;times->addWidget(new QLabel(tr("Duração")));times->addWidget(m_duration);times->addWidget(new QLabel(tr("Atraso inicial")));times->addWidget(m_delay);times->addWidget(new QLabel(tr("Atraso entre alvos")));times->addWidget(m_stagger);form->addRow(times);
    auto pairRow=[&](const QString& label,QDoubleSpinBox* a,QDoubleSpinBox* b){auto* h=new QHBoxLayout;h->addWidget(a);h->addWidget(new QLabel(QStringLiteral("→")));h->addWidget(b);form->addRow(label,h);};
    pairRow(tr("Opacidade:"),m_opacityFrom,m_opacityTo);pairRow(tr("X:"),m_xFrom,m_xTo);pairRow(tr("Y:"),m_yFrom,m_yTo);
    pairRow(tr("Escala X:"),m_scaleXFrom,m_scaleXTo);pairRow(tr("Escala Y:"),m_scaleYFrom,m_scaleYTo);pairRow(tr("Rotação:"),m_rotationFrom,m_rotationTo);
    auto* proc=new QHBoxLayout;proc->addWidget(new QLabel(tr("Intensidade")));proc->addWidget(m_amount);proc->addWidget(new QLabel(tr("Velocidade / oscilação")));proc->addWidget(m_frequency);form->addRow(tr("Movimento contínuo:"),proc);
    auto* loops=new QHBoxLayout;loops->addWidget(m_loopMode);loops->addWidget(new QLabel(tr("N vezes:")));loops->addWidget(m_loopCount);form->addRow(tr("Repetição:"),loops);
    root->addWidget(group);

    auto* previewGroup=new QGroupBox(tr("Prévia — mesmo resultado do jogo"),this);
    auto* pv=new QVBoxLayout(previewGroup);
    auto* preview=new TextEffectsPreview(ed,previewGroup);m_preview=preview;pv->addWidget(preview);
    auto* previewButtons=new QHBoxLayout;
    auto* pause=new QPushButton(tr("Pausar"),previewGroup);
    auto* restart=new QPushButton(tr("Reiniciar"),previewGroup);
    previewButtons->addStretch();previewButtons->addWidget(pause);previewButtons->addWidget(restart);
    pv->addLayout(previewButtons);
    root->addWidget(previewGroup);

    if (m_gradient.colors.size()<2) m_gradient.colors={QColor(QStringLiteral("#ffffff")),QColor(QStringLiteral("#88aaff"))};
    refreshGradientList(); loadPhase(); refreshPreview();

    connect(m_phase,&QComboBox::currentIndexChanged,this,[this](int){
        // Cada controle já persiste a fase atual imediatamente em changedControl.
        // No currentIndexChanged o QComboBox já aponta para a NOVA fase; chamar
        // storePhase() aqui copiaria os controles da fase anterior sobre ela.
        if(!m_loading){loadPhase();refreshPreview();}
    });
    auto changedControl=[this]{if(m_loading)return;storePhase();refreshPreview();emit changed();};
    connect(m_enabled,&QCheckBox::toggled,this,[changedControl](bool){changedControl();});
    connect(m_continuous,&QCheckBox::toggled,this,[this,changedControl](bool on){
        QSignalBlocker b(m_loopMode);
        const QString mode = on ? QStringLiteral("while-visible") : QStringLiteral("once");
        m_loopMode->setCurrentIndex(qMax(0,m_loopMode->findData(mode)));
        changedControl();
    });
    connect(m_fadeReveal,&QCheckBox::toggled,this,[changedControl](bool){changedControl();});
    connect(m_fadeRevealMs,&QSpinBox::valueChanged,this,[changedControl](int){changedControl();});
    for(QComboBox* c:{m_target,m_motion,m_easing,m_loopMode})connect(c,&QComboBox::currentIndexChanged,this,[changedControl](int){changedControl();});
    for(QSpinBox* s:{m_loopCount,m_duration,m_delay,m_stagger})connect(s,&QSpinBox::valueChanged,this,[changedControl](int){changedControl();});
    for(QDoubleSpinBox* s:{m_opacityFrom,m_opacityTo,m_xFrom,m_xTo,m_yFrom,m_yTo,m_scaleXFrom,m_scaleXTo,m_scaleYFrom,m_scaleYTo,m_rotationFrom,m_rotationTo,m_amount,m_frequency})connect(s,&QDoubleSpinBox::valueChanged,this,[changedControl](double){changedControl();});
    connect(apply,&QPushButton::clicked,this,&TextEffectsEditorWidget::applyPreset);
    connect(save,&QPushButton::clicked,this,&TextEffectsEditorWidget::saveCustomPreset);
    connect(restart,&QPushButton::clicked,this,[this,pause]{
        static_cast<TextEffectsPreview*>(m_preview)->restart();
        pause->setText(tr("Pausar"));
    });
    connect(pause,&QPushButton::clicked,this,[this,pause]{
        auto* previewWidget=static_cast<TextEffectsPreview*>(m_preview);
        previewWidget->setPaused(!previewWidget->paused());
        pause->setText(previewWidget->paused()?tr("Continuar"):tr("Pausar"));
    });
    connect(m_gradientEnabled,&QCheckBox::toggled,this,[this](bool on){
        if(m_loading)return;
        // Desativar gradiente é não-destrutivo no editor: as cores continuam
        // disponíveis caso o usuário reative a opção. gradient() continua
        // retornando um spec vazio enquanto a caixa estiver desmarcada.
        if(on && m_gradient.colors.size()<2)
            m_gradient.colors={QColor("#ffffff"),QColor("#88aaff")};
        refreshGradientList();refreshPreview();emit changed();
    });
    connect(m_gradientDirection,&QComboBox::currentIndexChanged,this,[this](int){if(m_loading)return;m_gradient.direction=m_gradientDirection->currentData().toString();refreshPreview();emit changed();});
    connect(addColor,&QPushButton::clicked,this,[this]{QColor c=QColorDialog::getColor(QColor("#ffffff"),this,tr("Adicionar cor"),QColorDialog::ShowAlphaChannel);if(!c.isValid())return;if(m_gradient.colors.size()<8)m_gradient.colors.push_back(c);m_gradientEnabled->setChecked(true);refreshGradientList();refreshPreview();emit changed();});
    connect(editColor,&QPushButton::clicked,this,[this]{int row=m_gradientColors->currentRow();if(row<0||row>=m_gradient.colors.size())return;QColor c=QColorDialog::getColor(m_gradient.colors[row],this,tr("Editar cor"),QColorDialog::ShowAlphaChannel);if(!c.isValid())return;m_gradient.colors[row]=c;refreshGradientList();m_gradientColors->setCurrentRow(row);refreshPreview();emit changed();});
    connect(delColor,&QPushButton::clicked,this,[this]{int row=m_gradientColors->currentRow();if(row<0||row>=m_gradient.colors.size())return;m_gradient.colors.remove(row);if(m_gradient.colors.size()<2)m_gradientEnabled->setChecked(false);refreshGradientList();refreshPreview();emit changed();});
}

core::TextEffectPhaseSpec* TextEffectsEditorWidget::currentPhase()
{
    const QString id=m_phase->currentData().toString();
    if(id==QLatin1String("loop"))return &m_effects.loop;
    if(id==QLatin1String("exit"))return &m_effects.exit;
    return &m_effects.entrance;
}
const core::TextEffectPhaseSpec* TextEffectsEditorWidget::currentPhase() const
{
    return const_cast<TextEffectsEditorWidget*>(this)->currentPhase();
}

core::TextGradientSpec TextEffectsEditorWidget::gradient() const
{
    if (!m_gradientEnabled || !m_gradientEnabled->isChecked()) return {};
    return m_gradient;
}

void TextEffectsEditorWidget::loadPhase()
{
    m_loading=true;
    const auto& p=*currentPhase();
    m_enabled->setChecked(p.enabled);
    m_continuous->setChecked(p.loopMode == QLatin1String("while-visible"));
    m_fadeReveal->setChecked(p.fadeOnReveal);
    m_fadeRevealMs->setValue(p.fadeRevealMs);
    m_target->setCurrentIndex(qMax(0,m_target->findData(p.target)));m_motion->setCurrentIndex(qMax(0,m_motion->findData(p.motion)));m_easing->setCurrentIndex(qMax(0,m_easing->findData(p.easing)));m_loopMode->setCurrentIndex(qMax(0,m_loopMode->findData(p.loopMode)));
    m_loopCount->setValue(p.loopCount);m_duration->setValue(p.durationMs);m_delay->setValue(p.delayMs);m_stagger->setValue(p.staggerMs);
    m_opacityFrom->setValue(p.opacityFrom);m_opacityTo->setValue(p.opacityTo);m_xFrom->setValue(p.translateXFrom);m_xTo->setValue(p.translateXTo);m_yFrom->setValue(p.translateYFrom);m_yTo->setValue(p.translateYTo);
    m_scaleXFrom->setValue(p.scaleXFrom);m_scaleXTo->setValue(p.scaleXTo);m_scaleYFrom->setValue(p.scaleYFrom);m_scaleYTo->setValue(p.scaleYTo);m_rotationFrom->setValue(p.rotationFrom);m_rotationTo->setValue(p.rotationTo);m_amount->setValue(p.amount);m_frequency->setValue(p.frequency);
    m_loading=false;
}

void TextEffectsEditorWidget::storePhase()
{
    auto& p=*currentPhase();
    p.enabled=m_enabled->isChecked();p.fadeOnReveal=m_fadeReveal->isChecked();p.fadeRevealMs=m_fadeRevealMs->value();p.target=m_target->currentData().toString();p.motion=m_motion->currentData().toString();p.easing=m_easing->currentData().toString();p.loopMode=m_continuous->isChecked()?QStringLiteral("while-visible"):m_loopMode->currentData().toString();p.loopCount=m_loopCount->value();p.durationMs=m_duration->value();p.delayMs=m_delay->value();p.staggerMs=m_stagger->value();
    p.opacityFrom=m_opacityFrom->value();p.opacityTo=m_opacityTo->value();p.translateXFrom=m_xFrom->value();p.translateXTo=m_xTo->value();p.translateYFrom=m_yFrom->value();p.translateYTo=m_yTo->value();p.scaleXFrom=m_scaleXFrom->value();p.scaleXTo=m_scaleXTo->value();p.scaleYFrom=m_scaleYFrom->value();p.scaleYTo=m_scaleYTo->value();p.rotationFrom=m_rotationFrom->value();p.rotationTo=m_rotationTo->value();p.amount=m_amount->value();p.frequency=m_frequency->value();
}

void TextEffectsEditorWidget::refreshGradientList()
{
    QSignalBlocker b(m_gradientColors);m_gradientColors->clear();
    for(const QColor& c:m_gradient.colors){auto* item=new QListWidgetItem(colorLabel(c),m_gradientColors);item->setBackground(c);item->setForeground(c.lightnessF()<.45?Qt::white:Qt::black);}
    m_gradientColors->setEnabled(m_gradientEnabled->isChecked());m_gradientDirection->setEnabled(m_gradientEnabled->isChecked());
}
void TextEffectsEditorWidget::refreshPreview()
{
    core::TextGradientSpec g=m_gradient;if(!m_gradientEnabled->isChecked())g.colors.clear();
    static_cast<TextEffectsPreview*>(m_preview)->setData(m_sampleText,m_effects,g,
                                                        m_phase->currentData().toString());
}
void TextEffectsEditorWidget::setSampleText(const QString& text)
{
    m_sampleText = text.trimmed().isEmpty() ? QStringLiteral("LUDO Game Engine") : text;
    refreshPreview();
}
void TextEffectsEditorWidget::applyPreset()
{
    const QString id=m_preset->currentData().toString();if(id.isEmpty())return;
    bool found=false;auto p=core::builtInTextEffectPreset(id,&found);
    if(!found&&ed.textEffectPresets.contains(id)){p=ed.textEffectPresets.value(id).phase;found=true;}
    if(!found)return;*currentPhase()=p;loadPhase();refreshPreview();emit changed();
}
void TextEffectsEditorWidget::saveCustomPreset()
{
    storePhase();bool ok=false;const QString name=QInputDialog::getText(this,tr("Salvar preset"),tr("Nome do preset:"),QLineEdit::Normal,QString(),&ok).trimmed();if(!ok||name.isEmpty())return;
    core::TextEffectPreset p;p.id=QStringLiteral("custom.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));p.name=name;p.phase=*currentPhase();ed.textEffectPresets.insert(p.id,p);ed.markDirty();m_preset->addItem(tr("Meu preset — %1").arg(name),p.id);m_preset->setCurrentIndex(m_preset->count()-1);
}

} // namespace ui
