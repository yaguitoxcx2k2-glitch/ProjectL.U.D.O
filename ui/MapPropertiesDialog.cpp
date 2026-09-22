#include "MapPropertiesDialog.h"

#include "AssetBrowser.h"
#include "AudioPicker.h"
#include "Icons.h"
#include "VisualEffectsPanel.h"
#include "PreviewClock.h"
#include "game/PictureFx.h"
#include "core/Weather.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QSpinBox>
#include <QSet>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>
#include <memory>

using namespace core;

namespace ui {

namespace {

QString fogDescription(const FogDef& f)
{
    return QObject::tr("Slot %1 · %2 · %3 · opacidade %4 · scroll (%5, %6)%7")
        .arg(f.slot).arg(QFileInfo(f.sourcePath).fileName(), fogBlendLabel(f.blend))
        .arg(f.opacity).arg(f.scrollX).arg(f.scrollY)
        .arg(f.enabled ? QString() : QObject::tr(" · desativada"));
}

bool editFog(Editor& ed, FogDef& fog, QWidget* parent)
{
    QDialog d(parent); d.setWindowTitle(QObject::tr("Configurar névoa")); d.resize(540,470);
    auto* v=new QVBoxLayout(&d); auto* form=new QFormLayout;
    auto* slot=new QSpinBox(&d);slot->setRange(1,5);slot->setValue(fog.slot);
    auto* enabled=new QCheckBox(QObject::tr("Ativada ao entrar no mapa"),&d);enabled->setChecked(fog.enabled);
    auto* imageRow=new QWidget(&d);auto* ih=new QHBoxLayout(imageRow);ih->setContentsMargins(0,0,0,0);
    auto* image=new QLineEdit(fog.sourcePath,imageRow);image->setReadOnly(true);
    auto* choose=new QPushButton(icons::get(QStringLiteral("open")),QObject::tr("Escolher…"),imageRow);ih->addWidget(image,1);ih->addWidget(choose);
    auto* blend=new QComboBox(&d);
    for(FogBlend b:{FogBlend::Normal,FogBlend::Add,FogBlend::Multiply,FogBlend::Screen,FogBlend::Overlay})blend->addItem(fogBlendLabel(b),fogBlendId(b));
    blend->setCurrentIndex(qMax(0,blend->findData(fogBlendId(fog.blend))));
    auto* opacity=new QSpinBox(&d);opacity->setRange(0,255);opacity->setValue(fog.opacity);
    auto decimal=[&](double value,double lo,double hi){auto* s=new QDoubleSpinBox(&d);s->setRange(lo,hi);s->setDecimals(2);s->setValue(value);return s;};
    auto* sx=decimal(fog.scrollX,-100,100);auto* sy=decimal(fog.scrollY,-100,100);
    auto* zoom=decimal(fog.zoom,.1,5);auto* repeat=new QCheckBox(QObject::tr("Repetir imagem para cobrir a tela"),&d);repeat->setChecked(fog.tileRepeat);
    auto* fade=new QSpinBox(&d);fade->setRange(0,6000);fade->setValue(fog.fadeInFrames);fade->setSuffix(QObject::tr(" frames"));
    form->addRow(QObject::tr("Slot:"),slot);form->addRow(enabled);form->addRow(QObject::tr("Imagem:"),imageRow);
    form->addRow(QObject::tr("Mistura:"),blend);form->addRow(QObject::tr("Opacidade:"),opacity);
    form->addRow(QObject::tr("Scroll X:"),sx);form->addRow(QObject::tr("Scroll Y:"),sy);
    form->addRow(QObject::tr("Zoom:"),zoom);form->addRow(repeat);form->addRow(QObject::tr("Fade inicial:"),fade);v->addLayout(form);
    auto* note=new QLabel(QObject::tr("Até cinco slots podem coexistir. Overlay usa Screen como equivalente no caminho GPU."),&d);note->setWordWrap(true);note->setProperty("uiRole",QStringLiteral("hint"));v->addWidget(note);
    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(box);
    QObject::connect(choose,&QPushButton::clicked,&d,[&]{const QString p=AssetBrowserDialog::chooseImage(ed,&d,QStringLiteral("Fogs"));if(p.isEmpty())return;QImage img(p);if(img.isNull())return;fog.image=img.convertToFormat(QImage::Format_ARGB32_Premultiplied);fog.sourcePath=ed.projectRelativePath(p);image->setText(fog.sourcePath);});
    QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    if(d.exec()!=QDialog::Accepted)return false;
    if(fog.image.isNull()){QMessageBox::information(parent,QObject::tr("Névoa"),QObject::tr("Escolha uma imagem para a névoa."));return false;}
    fog.slot=slot->value();fog.enabled=enabled->isChecked();fog.blend=fogBlendFromId(blend->currentData().toString());fog.opacity=opacity->value();fog.scrollX=sx->value();fog.scrollY=sy->value();fog.zoom=zoom->value();fog.tileRepeat=repeat->isChecked();fog.fadeInFrames=fade->value();return true;
}


QString panoramaDescription(const PanoramaDef& p,int index)
{
    const QString file=p.sourcePath.isEmpty()?QObject::tr("(sem imagem)"):QFileInfo(p.sourcePath).fileName();
    return QObject::tr("%1 · %2 · scroll (%3, %4) · parallax (%5, %6) · %7%8")
        .arg(p.name.isEmpty()?QObject::tr("Panorama %1").arg(index+1):p.name,file)
        .arg(p.speedX).arg(p.speedY).arg(p.fixed?0.0:p.parallaxX).arg(p.fixed?0.0:p.parallaxY)
        .arg(p.animated?QObject::tr("animado"):QObject::tr("estático"))
        .arg(p.enabled?QString():QObject::tr(" · desativado"));
}

bool editPanorama(Editor& ed, PanoramaDef& pano, QWidget* parent)
{
    QDialog d(parent);d.setWindowTitle(QObject::tr("Configurar panorama / parallax"));d.resize(860,720);
    auto* root=new QVBoxLayout(&d);auto* tabs=new QTabWidget(&d);root->addWidget(tabs,1);
    auto* page=new QWidget(tabs);auto* h=new QHBoxLayout(page);auto* left=new QVBoxLayout;auto* form=new QFormLayout;left->addLayout(form);
    auto* name=new QLineEdit(pano.name,page);auto* enabled=new QCheckBox(QObject::tr("Ativado"),page);enabled->setChecked(pano.enabled);
    auto* imageRow=new QWidget(page);auto* ih=new QHBoxLayout(imageRow);ih->setContentsMargins(0,0,0,0);auto* image=new QLineEdit(pano.sourcePath,imageRow);image->setReadOnly(true);auto* choose=new QPushButton(QObject::tr("Escolher…"),imageRow);ih->addWidget(image,1);ih->addWidget(choose);
    auto* loopX=new QCheckBox(QObject::tr("Loop horizontal"),page);loopX->setChecked(pano.loopX);auto* loopY=new QCheckBox(QObject::tr("Loop vertical"),page);loopY->setChecked(pano.loopY);
    auto dec=[&](double v,double lo,double hi){auto*s=new QDoubleSpinBox(page);s->setRange(lo,hi);s->setDecimals(2);s->setValue(v);return s;};
    auto* sx=dec(pano.speedX,-1000,1000);auto* sy=dec(pano.speedY,-1000,1000);
    auto* parallaxX=dec(pano.fixed?0.0:pano.parallaxX,0.0,4.0);auto* parallaxY=dec(pano.fixed?0.0:pano.parallaxY,0.0,4.0);
    parallaxX->setSingleStep(0.05);parallaxY->setSingleStep(0.05);
    parallaxX->setToolTip(QObject::tr("0 = não acompanha o deslocamento da câmera (ainda recebe zoom) · 0,5 = fundo mais lento · 1 = presa ao mapa · acima de 1 = mais rápida"));
    parallaxY->setToolTip(parallaxX->toolTip());
    auto* fixed=new QCheckBox(QObject::tr("Fixar na tela (sem câmera/zoom)"),page);fixed->setChecked(pano.fixed);auto* showEditor=new QCheckBox(QObject::tr("Mostrar no editor"),page);showEditor->setChecked(pano.showInEditor);
    auto* opacity=new QSpinBox(page);opacity->setRange(0,255);opacity->setValue(pano.opacity);auto* blend=new QComboBox(page);for(int i=int(PictureBlend::Normal);i<=int(PictureBlend::Screen);++i){auto b=PictureBlend(i);blend->addItem(pictureBlendLabel(b),pictureBlendId(b));}blend->setCurrentIndex(qMax(0,blend->findData(pictureBlendId(pano.blend))));
    auto* flipH=new QCheckBox(QObject::tr("Virar horizontalmente"),page);flipH->setChecked(pano.flipH);auto* flipV=new QCheckBox(QObject::tr("Virar verticalmente"),page);flipV->setChecked(pano.flipV);
    auto* animated=new QCheckBox(QObject::tr("Spritesheet / imagem em sequência"),page);animated->setChecked(pano.animated||pano.frameCount>1);auto* frames=new QSpinBox(page);frames->setRange(1,999);frames->setValue(qMax(1,pano.frameCount));auto* cols=new QSpinBox(page);cols->setRange(1,999);cols->setValue(qMax(1,pano.frameColumns));auto* rows=new QSpinBox(page);rows->setRange(1,999);rows->setValue(qMax(1,pano.frameRows));auto* frame=new QSpinBox(page);frame->setRange(0,qMax(0,pano.frameCount-1));frame->setValue(qBound(0,pano.frameIndex,qMax(0,pano.frameCount-1)));auto* fps=dec(pano.frameFps,0,120);fps->setSuffix(QObject::tr(" fps"));auto* frameLoop=new QCheckBox(QObject::tr("Loop da animação"),page);frameLoop->setChecked(pano.frameLoop);auto* playing=new QCheckBox(QObject::tr("Reproduzir animação"),page);playing->setChecked(pano.framePlaying);
    form->addRow(QObject::tr("Nome:"),name);form->addRow(enabled);form->addRow(QObject::tr("Imagem:"),imageRow);form->addRow(loopX,sx);form->addRow(loopY,sy);
    form->addRow(QObject::tr("Parallax câmera X:"),parallaxX);form->addRow(QObject::tr("Parallax câmera Y:"),parallaxY);form->addRow(fixed);form->addRow(showEditor);form->addRow(QObject::tr("Opacidade:"),opacity);form->addRow(QObject::tr("Mistura:"),blend);form->addRow(flipH);form->addRow(flipV);form->addRow(animated);form->addRow(QObject::tr("Frames:"),frames);form->addRow(QObject::tr("Colunas:"),cols);form->addRow(QObject::tr("Linhas:"),rows);form->addRow(QObject::tr("Frame inicial/fixo:"),frame);form->addRow(QObject::tr("Velocidade:"),fps);form->addRow(frameLoop);form->addRow(playing);left->addStretch(1);h->addLayout(left,1);
    auto* preview=new QLabel(page);preview->setMinimumSize(360,300);preview->setAlignment(Qt::AlignCenter);preview->setProperty("uiRole",QStringLiteral("previewCanvas"));h->addWidget(preview,1);tabs->addTab(page,QObject::tr("Panorama"));
    auto* fxPage=new QWidget(tabs);auto* fxLayout=new QVBoxLayout(fxPage);auto* fxPanel=new VisualEffectsPanel(ed,pano.fx,fxPage);auto* scroll=new QScrollArea(fxPage);scroll->setWidgetResizable(true);scroll->setWidget(fxPanel);fxLayout->addWidget(scroll);tabs->addTab(fxPage,QObject::tr("Efeitos"));
    game::VisualFxCache cache;auto* clock=new PreviewClock(&d);
    auto pull=[&]{pano.name=name->text().trimmed();pano.enabled=enabled->isChecked();pano.loopX=loopX->isChecked();pano.loopY=loopY->isChecked();pano.speedX=sx->value();pano.speedY=sy->value();pano.fixed=fixed->isChecked();pano.parallaxX=pano.fixed?0.0:parallaxX->value();pano.parallaxY=pano.fixed?0.0:parallaxY->value();pano.showInEditor=showEditor->isChecked();pano.opacity=opacity->value();pano.blend=pictureBlendFromId(blend->currentData().toString());pano.flipH=flipH->isChecked();pano.flipV=flipV->isChecked();pano.animated=animated->isChecked();pano.frameCount=frames->value();pano.frameColumns=cols->value();pano.frameRows=rows->value();pano.frameIndex=qBound(0,frame->value(),qMax(0,pano.frameCount-1));pano.frameFps=fps->value();pano.frameLoop=frameLoop->isChecked();pano.framePlaying=playing->isChecked();fxPanel->puxar();frame->setMaximum(qMax(0,pano.frameCount-1));};
    auto refresh=[&]{pull();if(pano.image.isNull()){preview->setPixmap(QPixmap());preview->setText(QObject::tr("Escolha uma imagem"));return;}game::LivePicture lp;lp.age=clock->elapsedSeconds();lp.def.number=19001;lp.def.opacity=pano.opacity;lp.def.flipH=pano.flipH;lp.def.flipV=pano.flipV;lp.def.setFrameSequence(pano.frameSequence());lp.def.fx=pano.fx;const auto out=cache.buildImage(lp,ed,pano.image,QStringLiteral("panorama-editor"));if(!out.valid)return;QPixmap pm=QPixmap::fromImage(out.image);pm=pm.scaled(preview->size()-QSize(8,8),Qt::KeepAspectRatio,Qt::SmoothTransformation);preview->setPixmap(pm);};
    QObject::connect(choose,&QPushButton::clicked,&d,[&]{const QString path=AssetBrowserDialog::chooseImage(ed,&d,QStringLiteral("Panorama"));if(path.isEmpty())return;QImage img(path);if(img.isNull())return;pano.image=img.convertToFormat(QImage::Format_ARGB32_Premultiplied);pano.sourcePath=ed.projectRelativePath(path);image->setText(pano.sourcePath);cache.clear();refresh();});
    for(QDoubleSpinBox* w:{sx,sy,parallaxX,parallaxY,fps})QObject::connect(w,&QDoubleSpinBox::valueChanged,&d,[&](double){refresh();});for(QSpinBox* w:{opacity,frames,cols,rows,frame})QObject::connect(w,&QSpinBox::valueChanged,&d,[&](int){refresh();});for(QCheckBox* w:{enabled,loopX,loopY,fixed,showEditor,flipH,flipV,animated,frameLoop,playing})QObject::connect(w,&QCheckBox::toggled,&d,[&](bool){parallaxX->setEnabled(!fixed->isChecked());parallaxY->setEnabled(!fixed->isChecked());refresh();});QObject::connect(blend,&QComboBox::currentIndexChanged,&d,[&](int){refresh();});QObject::connect(fxPanel,&VisualEffectsPanel::changed,&d,[&]{cache.clear();refresh();});QObject::connect(clock,&PreviewClock::frame,&d,[&](double){refresh();});clock->start(33);
    parallaxX->setEnabled(!fixed->isChecked());parallaxY->setEnabled(!fixed->isChecked());
    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);root->addWidget(box);QObject::connect(box,&QDialogButtonBox::accepted,&d,&QDialog::accept);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);refresh();if(d.exec()!=QDialog::Accepted)return false;pull();if(pano.name.isEmpty())pano.name=QFileInfo(pano.sourcePath).completeBaseName();return !pano.image.isNull();
}

QWidget* assetRow(Editor& ed, const QString& initial, const QString& folder, bool imageOnly,
                  QWidget* parent)
{
    auto* w=new QWidget(parent);auto* h=new QHBoxLayout(w);h->setContentsMargins(0,0,0,0);
    auto* line=new QLineEdit(initial,w);line->setReadOnly(true);line->setObjectName(QStringLiteral("assetPath"));
    auto* choose=new QPushButton(icons::get(QStringLiteral("open")),QObject::tr("…"),w);choose->setFixedWidth(38);
    auto* clear=new QPushButton(QObject::tr("×"),w);clear->setFixedWidth(30);
    h->addWidget(line,1);h->addWidget(choose);h->addWidget(clear);
    QObject::QObject::connect(choose,&QPushButton::clicked,w,[&ed,folder,imageOnly,line,w]{
        const QString p=imageOnly?AssetBrowserDialog::chooseImage(ed,w,folder):AssetBrowserDialog::chooseAny(ed,w,folder);
        if(!p.isEmpty())line->setText(ed.projectRelativePath(p));
    });
    QObject::QObject::connect(clear,&QPushButton::clicked,w,[line]{line->clear();});
    return w;
}

QWidget* audioRow(Editor& ed, const QString& initial, int initialVolume,
                  const QString& title, const QString& context, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* line = new QLineEdit(initial, w);
    line->setReadOnly(true);
    line->setObjectName(QStringLiteral("assetPath"));
    auto* volume = new QSpinBox(w);
    volume->setObjectName(QStringLiteral("audioVolume"));
    volume->setRange(0, 100);
    volume->setSuffix(QStringLiteral("%"));
    volume->setValue(qBound(0, initialVolume, 100));
    volume->setToolTip(QObject::tr("Volume usado ao entrar neste mapa"));
    auto* choose = new QPushButton(QObject::tr("Escolher / ouvir…"), w);
    auto* clear = new QPushButton(QObject::tr("×"), w);
    clear->setFixedWidth(30);
    h->addWidget(line, 1);
    h->addWidget(volume);
    h->addWidget(choose);
    h->addWidget(clear);
    QObject::connect(choose, &QPushButton::clicked, w,
                     [&ed, line, volume, title, context, w] {
        QString source = line->text();
        int selectedVolume = volume->value();
        if (chooseGameAudio(ed, w, title, source, selectedVolume, context)) {
            line->setText(source);
            volume->setValue(selectedVolume);
        }
    });
    QObject::connect(clear, &QPushButton::clicked, w, [line] { line->clear(); });
    return w;
}

QString cutsceneRegionDescription(const CutsceneRegion& region)
{
    const QRect r=region.tileArea;QStringList edges;if(region.triggerOnEnter)edges<<QObject::tr("entrada");if(region.triggerOnExit)edges<<QObject::tr("saída");
    return QObject::tr("%1 · (%2,%3) %4×%5 · %6%7").arg(region.name).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).arg(edges.join(QStringLiteral(" + ")),region.enabled?QString():QObject::tr(" · desativada"));
}

bool editCutsceneRegion(Editor& ed,const MapDoc& map,CutsceneRegion& region,QWidget* parent)
{
    QDialog d(parent);d.setWindowTitle(QObject::tr("Região de cutscene"));d.resize(560,520);auto* root=new QVBoxLayout(&d);auto* form=new QFormLayout;
    auto* name=new QLineEdit(region.name,&d);auto spin=[&d](int value,int maximum){auto*s=new QSpinBox(&d);s->setRange(0,qMax(0,maximum));s->setValue(value);return s;};
    auto* x=spin(region.tileArea.x(),map.map.width-1);auto* y=spin(region.tileArea.y(),map.map.height-1);auto* width=spin(qMax(1,region.tileArea.width()),map.map.width);width->setMinimum(1);auto* height=spin(qMax(1,region.tileArea.height()),map.map.height);height->setMinimum(1);
    auto* bounds=new QWidget(&d);auto* bh=new QHBoxLayout(bounds);bh->setContentsMargins(0,0,0,0);bh->addWidget(new QLabel("X",bounds));bh->addWidget(x);bh->addWidget(new QLabel("Y",bounds));bh->addWidget(y);bh->addWidget(new QLabel(QObject::tr("L"),bounds));bh->addWidget(width);bh->addWidget(new QLabel(QObject::tr("A"),bounds));bh->addWidget(height);
    auto* common=new QComboBox(&d);common->addItem(QObject::tr("Escolha…"),QString());for(const CommonEvent& ce:ed.commonEvents)common->addItem(QStringLiteral("%1 — %2").arg(ce.number).arg(ce.name),ce.id);common->setCurrentIndex(qMax(0,common->findData(region.commonEventId)));
    auto* enter=new QCheckBox(QObject::tr("Disparar ao entrar"),&d);enter->setChecked(region.triggerOnEnter);auto* exit=new QCheckBox(QObject::tr("Disparar ao sair"),&d);exit->setChecked(region.triggerOnExit);auto* oneShot=new QCheckBox(QObject::tr("Executar uma única vez"),&d);oneShot->setChecked(region.oneShot);
    auto* scope=new QComboBox(&d);scope->addItem(QObject::tr("Por visita ao mapa"),QStringLiteral("mapVisit"));scope->addItem(QObject::tr("Por save"),QStringLiteral("saveGame"));scope->setCurrentIndex(qMax(0,scope->findData(cutsceneRegionScopeId(region.oneShotScope))));scope->setEnabled(region.oneShot);auto* enabled=new QCheckBox(QObject::tr("Região ativada"),&d);enabled->setChecked(region.enabled);
    form->addRow(QObject::tr("Nome:"),name);form->addRow(QObject::tr("Área em tiles:"),bounds);form->addRow(QObject::tr("Evento Comum:"),common);form->addRow(enter);form->addRow(exit);form->addRow(oneShot);form->addRow(QObject::tr("Repetição:"),scope);form->addRow(enabled);root->addLayout(form);
    auto* hint=new QLabel(QObject::tr("Quando o jogador entrar nesta região, o Evento Comum será executado. Se ele tiver uma cutscene configurada, ela poderá ser pulada normalmente."),&d);hint->setWordWrap(true);hint->setProperty("uiRole",QStringLiteral("hint"));root->addWidget(hint);auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);root->addWidget(box);
    QObject::connect(oneShot,&QCheckBox::toggled,scope,&QWidget::setEnabled);QObject::connect(box,&QDialogButtonBox::rejected,&d,&QDialog::reject);QObject::connect(box,&QDialogButtonBox::accepted,&d,[&]{if(name->text().trimmed().isEmpty()||common->currentData().toString().isEmpty()||(!enter->isChecked()&&!exit->isChecked())){QMessageBox::information(&d,QObject::tr("Região de cutscene"),QObject::tr("Informe nome, Evento Comum e ao menos uma borda de disparo."));return;}d.accept();});if(d.exec()!=QDialog::Accepted)return false;
    region.name=name->text().trimmed();region.tileArea=QRect(x->value(),y->value(),qMin(width->value(),map.map.width-x->value()),qMin(height->value(),map.map.height-y->value()));region.commonEventId=common->currentData().toString();region.triggerOnEnter=enter->isChecked();region.triggerOnExit=exit->isChecked();region.oneShot=oneShot->isChecked();region.oneShotScope=cutsceneRegionScopeFromId(scope->currentData().toString());region.enabled=enabled->isChecked();return true;
}

} // namespace

MapPropertiesDialog::MapPropertiesDialog(Editor& editor, QWidget* parent)
    : QDialog(parent)
{
    MapDoc* doc=editor.doc();
    if(!doc){reject();return;}
    setWindowTitle(tr("%1 — Propriedades do Mapa").arg(doc->name));resize(720,620);
    const DocSnapshot before=editor.snapshotDoc();
    auto state=std::make_shared<MapEnvironment>(doc->environment);
    auto regions=std::make_shared<QVector<CutsceneRegion>>(doc->cutsceneRegions);
    const QString bgm=state->bgmPath,bgs=state->bgsPath,battle=state->battleBackgroundPath,panoPath=state->panoramaPath;

    auto* outer=new QVBoxLayout(this);auto* tabs=new QTabWidget(this);outer->addWidget(tabs,1);
    auto* general=new QWidget(tabs);auto* gv=new QVBoxLayout(general);
    auto* gbox=new QGroupBox(tr("Configurações Gerais"),general);auto* gf=new QFormLayout(gbox);
    auto* name=new QLineEdit(doc->name,gbox);auto* width=new QSpinBox(gbox);width->setRange(1,999);width->setValue(doc->map.width);
    auto* height=new QSpinBox(gbox);height->setRange(1,999);height->setValue(doc->map.height);
    auto* sizeRow=new QWidget(gbox);auto* sh=new QHBoxLayout(sizeRow);sh->setContentsMargins(0,0,0,0);sh->addWidget(new QLabel(tr("Largura:"),sizeRow));sh->addWidget(width);sh->addWidget(new QLabel(tr("Altura:"),sizeRow));sh->addWidget(height);
    gf->addRow(tr("Nome:"),name);gf->addRow(tr("Tamanho:"),sizeRow);
    auto* autoBgm=new QCheckBox(tr("BGM automática"),gbox);autoBgm->setChecked(state->autoBgm);auto* bgmRow=audioRow(editor,bgm,state->bgmVolume,tr("Escolher BGM do mapa"),QStringLiteral("BGM"),gbox);gf->addRow(autoBgm,bgmRow);
    auto* autoBgs=new QCheckBox(tr("BGS automática"),gbox);autoBgs->setChecked(state->autoBgs);auto* bgsRow=audioRow(editor,bgs,state->bgsVolume,tr("Escolher BGS do mapa"),QStringLiteral("BGS"),gbox);gf->addRow(autoBgs,bgsRow);
    auto* battleRow=assetRow(editor,battle,QStringLiteral("Pictures"),true,gbox);gf->addRow(tr("Fundo de batalha:"),battleRow);gv->addWidget(gbox);
    auto* notes=new QPlainTextEdit(state->note,general);notes->setPlaceholderText(tr("Anotações do mapa (sem scripts)…"));gv->addWidget(new QLabel(tr("Anotação:"),general));gv->addWidget(notes,1);tabs->addTab(general,tr("Geral"));

    if(state->panoramas.isEmpty()&&!state->panorama.isNull()){PanoramaDef p;p.name=tr("Panorama 1");p.sourcePath=state->panoramaPath;p.image=state->panorama;p.loopX=state->panoramaLoopX;p.loopY=state->panoramaLoopY;p.speedX=state->panoramaSpeedX;p.speedY=state->panoramaSpeedY;p.fixed=state->panoramaFixed;p.showInEditor=state->panoramaInEditor;state->panoramas.push_back(p);}
    auto* panorama=new QWidget(tabs);auto* pv=new QVBoxLayout(panorama);
    auto* panoInfo=new QLabel(tr("Use várias camadas para criar profundidade de parallax. Cada camada pode ter scroll, animação por spritesheet e os mesmos efeitos das Pictures."),panorama);panoInfo->setWordWrap(true);pv->addWidget(panoInfo);
    auto* panoList=new QListWidget(panorama);pv->addWidget(panoList,1);
    auto panoReload=std::make_shared<std::function<void()>>();*panoReload=[state,panoList]{panoList->clear();for(int i=0;i<state->panoramas.size();++i)panoList->addItem(panoramaDescription(state->panoramas[i],i));};(*panoReload)();
    auto* panoButtons=new QHBoxLayout;auto* panoAdd=new QPushButton(tr("Adicionar…"),panorama);auto* panoEdit=new QPushButton(tr("Editar…"),panorama);auto* panoRemove=new QPushButton(tr("Remover"),panorama);auto* panoUp=new QPushButton(tr("Subir"),panorama);auto* panoDown=new QPushButton(tr("Descer"),panorama);panoButtons->addWidget(panoAdd);panoButtons->addWidget(panoEdit);panoButtons->addWidget(panoRemove);panoButtons->addStretch(1);panoButtons->addWidget(panoUp);panoButtons->addWidget(panoDown);pv->addLayout(panoButtons);
    QObject::connect(panoAdd,&QPushButton::clicked,this,[this,state,panoReload,&editor]{PanoramaDef p;p.name=tr("Panorama %1").arg(state->panoramas.size()+1);if(editPanorama(editor,p,this)){state->panoramas.push_back(p);(*panoReload)();}});
    QObject::connect(panoEdit,&QPushButton::clicked,this,[this,state,panoReload,panoList,&editor]{const int i=panoList->currentRow();if(i<0)return;PanoramaDef p=state->panoramas[i];if(editPanorama(editor,p,this)){state->panoramas[i]=p;(*panoReload)();panoList->setCurrentRow(i);}});
    QObject::connect(panoRemove,&QPushButton::clicked,this,[state,panoReload,panoList]{const int i=panoList->currentRow();if(i>=0){state->panoramas.remove(i);(*panoReload)();}});
    QObject::connect(panoUp,&QPushButton::clicked,this,[state,panoReload,panoList]{const int i=panoList->currentRow();if(i>0){state->panoramas.swapItemsAt(i,i-1);(*panoReload)();panoList->setCurrentRow(i-1);}});
    QObject::connect(panoDown,&QPushButton::clicked,this,[state,panoReload,panoList]{const int i=panoList->currentRow();if(i>=0&&i+1<state->panoramas.size()){state->panoramas.swapItemsAt(i,i+1);(*panoReload)();panoList->setCurrentRow(i+1);}});
    tabs->addTab(panorama,tr("Panoramas"));

    auto* weatherPage=new QWidget(tabs);auto* wf=new QFormLayout(weatherPage);
    auto* weatherType=new QComboBox(weatherPage);
    weatherType->addItem(tr("Sem clima"),QStringLiteral("none"));
    weatherType->addItem(tr("Chuva"),QStringLiteral("rain"));
    weatherType->addItem(tr("Neve"),QStringLiteral("snow"));
    weatherType->addItem(tr("Tempestade"),QStringLiteral("storm"));
    weatherType->setCurrentIndex(qMax(0,weatherType->findData(state->weather.typeId())));
    auto* weatherIntensity=new QSpinBox(weatherPage);weatherIntensity->setRange(0,100);
    weatherIntensity->setSuffix(QStringLiteral("%"));
    const bool weatherEnabled = weatherType->currentData().toString()!=QLatin1String("none");
    weatherIntensity->setValue(weatherEnabled ? state->weather.intensity : 0);
    weatherIntensity->setEnabled(weatherEnabled);
    auto* thunderRow=audioRow(editor,state->weather.thunderSePath,state->weather.thunderVolume,
                              tr("Escolher SE do trovão"),QStringLiteral("SE"),weatherPage);
    wf->addRow(tr("Tipo:"),weatherType);wf->addRow(tr("Intensidade:"),weatherIntensity);
    wf->addRow(tr("SE do trovão:"),thunderRow);
    auto updateThunderVisibility=[wf,thunderRow](const QString& type){
        wf->setRowVisible(thunderRow,type==QLatin1String("storm"));
    };
    updateThunderVisibility(weatherType->currentData().toString());
    QObject::connect(weatherType,&QComboBox::currentIndexChanged,weatherPage,
                     [weatherType,weatherIntensity,updateThunderVisibility](int){
        const QString type=weatherType->currentData().toString();
        const bool enabled=type!=QLatin1String("none");
        updateThunderVisibility(type);
        weatherIntensity->setEnabled(enabled);
        if(enabled&&weatherIntensity->value()<=0)weatherIntensity->setValue(50);
    });
    auto* weatherHint=new QLabel(tr("O clima aparece durante o jogo. As alterações feitas aqui também valem para o comando “Alterar clima”. Use Testar Mapa ou Testar Jogo para conferir o resultado."),weatherPage);
    weatherHint->setWordWrap(true);weatherHint->setProperty("uiRole",QStringLiteral("hint"));wf->addRow(weatherHint);
    tabs->addTab(weatherPage,tr("Clima"));

    auto* fogPage=new QWidget(tabs);auto* fv=new QVBoxLayout(fogPage);auto* fogList=new QListWidget(fogPage);fv->addWidget(fogList,1);
    auto reload=std::make_shared<std::function<void()>>();
    *reload=[state,fogList]{fogList->clear();for(const FogDef& f:state->fogs)fogList->addItem(fogDescription(f));};(*reload)();
    auto* fr=new QHBoxLayout;auto* add=new QPushButton(tr("Adicionar…"),fogPage);auto* edit=new QPushButton(tr("Editar…"),fogPage);auto* remove=new QPushButton(tr("Remover"),fogPage);fr->addWidget(add);fr->addWidget(edit);fr->addWidget(remove);fr->addStretch(1);fv->addLayout(fr);
    QObject::connect(add,&QPushButton::clicked,this,[this,state,reload,&editor]{if(state->fogs.size()>=5){QMessageBox::information(this,tr("Névoas"),tr("O limite é de cinco slots."));return;}FogDef f;QSet<int> used;for(const FogDef& x:state->fogs)used.insert(x.slot);while(used.contains(f.slot)&&f.slot<5)++f.slot;if(editFog(editor,f,this)){for(int i=0;i<state->fogs.size();++i)if(state->fogs[i].slot==f.slot){state->fogs.remove(i);break;}state->fogs.push_back(f);(*reload)();}});
    QObject::connect(edit,&QPushButton::clicked,this,[this,state,reload,fogList,&editor]{int i=fogList->currentRow();if(i<0)return;FogDef f=state->fogs[i];if(editFog(editor,f,this)){for(int j=0;j<state->fogs.size();++j)if(j!=i&&state->fogs[j].slot==f.slot){state->fogs.remove(j);if(j<i)--i;break;}state->fogs[i]=f;(*reload)();fogList->setCurrentRow(i);}});
    QObject::connect(remove,&QPushButton::clicked,this,[state,reload,fogList]{int i=fogList->currentRow();if(i>=0){state->fogs.remove(i);(*reload)();}});tabs->addTab(fogPage,tr("Névoas"));

    auto* encounterPage=new QWidget(tabs);auto* ev=new QVBoxLayout(encounterPage);
    auto* encounterHint=new QLabel(tr("Marque as tropas que podem aparecer enquanto o jogador caminha neste mapa."),encounterPage);encounterHint->setWordWrap(true);ev->addWidget(encounterHint);
    auto* encounterList=new QListWidget(encounterPage);ev->addWidget(encounterList,1);
    for(const DatabaseRecord& troop:editor.database.value(QStringLiteral("troops"))){auto* item=new QListWidgetItem(troop.name.isEmpty()?tr("Tropa %1").arg(troop.number):troop.name,encounterList);item->setData(Qt::UserRole,troop.id);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(doc->encounterTroopIds.contains(troop.id)?Qt::Checked:Qt::Unchecked);}
    auto* encounterForm=new QFormLayout;auto* encounterSteps=new QSpinBox(encounterPage);encounterSteps->setRange(0,99999);encounterSteps->setValue(doc->encounterSteps);encounterSteps->setSpecialValueText(tr("Desativados"));encounterSteps->setSuffix(tr(" passos em média"));encounterForm->addRow(tr("Frequência:"),encounterSteps);ev->addLayout(encounterForm);tabs->addTab(encounterPage,tr("Encontros"));

    auto* cutscenePage=new QWidget(tabs);auto* cv=new QVBoxLayout(cutscenePage);auto* cutsceneHint=new QLabel(tr("Regiões podem executar Eventos Comuns quando o jogador entra nelas. Cada região usa o fluxo normal de eventos do jogo."),cutscenePage);cutsceneHint->setWordWrap(true);cv->addWidget(cutsceneHint);auto* regionList=new QListWidget(cutscenePage);cv->addWidget(regionList,1);
    auto regionReload=std::make_shared<std::function<void()>>();*regionReload=[regions,regionList]{regionList->clear();for(const CutsceneRegion& region:*regions)regionList->addItem(cutsceneRegionDescription(region));};(*regionReload)();auto* regionButtons=new QHBoxLayout;auto* regionAdd=new QPushButton(tr("Adicionar…"),cutscenePage);auto* regionEdit=new QPushButton(tr("Editar…"),cutscenePage);auto* regionRemove=new QPushButton(tr("Remover"),cutscenePage);regionButtons->addWidget(regionAdd);regionButtons->addWidget(regionEdit);regionButtons->addWidget(regionRemove);regionButtons->addStretch(1);cv->addLayout(regionButtons);tabs->addTab(cutscenePage,tr("Cutscenes"));
    QObject::connect(regionAdd,&QPushButton::clicked,this,[this,regions,regionReload,&editor,doc]{CutsceneRegion region;region.name=tr("Cutscene %1").arg(regions->size()+1);if(editCutsceneRegion(editor,*doc,region,this)){regions->push_back(region);(*regionReload)();}});QObject::connect(regionEdit,&QPushButton::clicked,this,[this,regions,regionReload,regionList,&editor,doc]{const int i=regionList->currentRow();if(i<0)return;CutsceneRegion region=regions->at(i);if(editCutsceneRegion(editor,*doc,region,this)){(*regions)[i]=region;(*regionReload)();regionList->setCurrentRow(i);}});QObject::connect(regionRemove,&QPushButton::clicked,this,[regions,regionReload,regionList]{const int i=regionList->currentRow();if(i>=0){regions->remove(i);(*regionReload)();}});

    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);outer->addWidget(box);QObject::connect(box,&QDialogButtonBox::rejected,this,&QDialog::reject);
    QObject::connect(box,&QDialogButtonBox::accepted,this,[this,&editor,state,regions,before,name,width,height,autoBgm,bgmRow,autoBgs,bgsRow,battleRow,notes,weatherType,weatherIntensity,thunderRow,encounterList,encounterSteps]{
        if(name->text().trimmed().isEmpty()){QMessageBox::information(this,tr("Mapa"),tr("Informe um nome."));return;}
        MapDoc* current=editor.doc();if(!current)return;
        current->name=name->text().trimmed();const bool resized=current->map.width!=width->value()||current->map.height!=height->value();current->map.width=width->value();current->map.height=height->value();
        auto pathOf=[](QWidget* row){auto* line=row->findChild<QLineEdit*>(QStringLiteral("assetPath"));return line?line->text():QString();};
        auto volumeOf=[](QWidget* row){auto* spin=row->findChild<QSpinBox*>(QStringLiteral("audioVolume"));return spin?spin->value():90;};
        state->autoBgm=autoBgm->isChecked();state->bgmPath=pathOf(bgmRow);state->bgmVolume=volumeOf(bgmRow);state->autoBgs=autoBgs->isChecked();state->bgsPath=pathOf(bgsRow);state->bgsVolume=volumeOf(bgsRow);state->battleBackgroundPath=pathOf(battleRow);state->note=notes->toPlainText();
        // Mantém os campos legados sincronizados com a primeira camada para compatibilidade.
        if(!state->panoramas.isEmpty()){const PanoramaDef& p=state->panoramas.first();state->panoramaPath=p.sourcePath;state->panorama=p.image;state->panoramaLoopX=p.loopX;state->panoramaLoopY=p.loopY;state->panoramaSpeedX=p.speedX;state->panoramaSpeedY=p.speedY;state->panoramaFixed=p.fixed;state->panoramaInEditor=p.showInEditor;}else{state->panoramaPath.clear();state->panorama=QImage();}
        current->environment=*state;
        state->weather.setConfig(weatherType->currentData().toString(), weatherIntensity->value(),
                                 pathOf(thunderRow), volumeOf(thunderRow), false);
        current->environment=*state;
        current->encounterSteps=encounterSteps->value();current->encounterTroopIds.clear();for(int i=0;i<encounterList->count();++i)if(encounterList->item(i)->checkState()==Qt::Checked)current->encounterTroopIds.push_back(encounterList->item(i)->data(Qt::UserRole).toString());
        current->cutsceneRegions=*regions;
        if(resized)editor.resyncLayerGrids();editor.pushDocHistory(before,tr("Propriedades do mapa"));emit editor.docsChanged();emit editor.mapChanged();accept();
    });
}

} // namespace ui
