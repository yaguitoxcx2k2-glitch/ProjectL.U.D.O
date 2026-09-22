#include "FootstepManagerDialog.h"

#include "AudioPicker.h"
#include "core/Editor.h"
#include "core/ProjectReferenceIndex.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace ui {
namespace {

void fillSurfaceCombo(QComboBox* combo, const core::Editor& ed, const QString& selected,
                      const QString& emptyLabel)
{
    if (!combo) return;
    combo->clear(); combo->addItem(emptyLabel, QString());
    for (const core::FootstepSurface& surface : ed.footstepSurfaces)
        combo->addItem(surface.name, surface.id);
    const int idx = combo->findData(selected);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

} // namespace

FootstepManagerDialog::FootstepManagerDialog(core::Editor& editor, QWidget* parent)
    : QDialog(parent), ed(editor)
{
    setWindowTitle(tr("Gerenciador de Sons de Passos"));
    resize(920, 650);
    auto* root = new QVBoxLayout(this);
    auto* splitter = new QSplitter(this); root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter); auto* lv = new QVBoxLayout(left);
    lv->addWidget(new QLabel(tr("Superfícies"), left));
    m_surfaces = new QListWidget(left); lv->addWidget(m_surfaces, 1);
    auto* lbuttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("+ Nova"), left);
    auto* del = new QPushButton(tr("Excluir"), left);
    lbuttons->addWidget(add); lbuttons->addWidget(del); lv->addLayout(lbuttons);

    auto* right = new QWidget(splitter); auto* rv = new QVBoxLayout(right);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(right); form->addRow(tr("Nome:"), m_name);
    m_volume = new QSpinBox(right); m_volume->setRange(0,100); m_volume->setSuffix("%"); form->addRow(tr("Volume:"), m_volume);
    auto* pitchRow = new QHBoxLayout;
    m_pitchMin = new QSpinBox(right); m_pitchMin->setRange(50,200); m_pitchMin->setSuffix("%");
    m_pitchMax = new QSpinBox(right); m_pitchMax->setRange(50,200); m_pitchMax->setSuffix("%");
    pitchRow->addWidget(m_pitchMin); pitchRow->addWidget(new QLabel(tr("até"), right)); pitchRow->addWidget(m_pitchMax);
    form->addRow(tr("Pitch aleatório:"), pitchRow);
    m_noRepeat = new QCheckBox(tr("Evitar repetir imediatamente a mesma variação"), right); form->addRow(QString(), m_noRepeat);
    rv->addLayout(form);

    auto* soundsBox = new QGroupBox(tr("Variações de som"), right); auto* sv = new QVBoxLayout(soundsBox);
    m_sounds = new QTableWidget(0,2,soundsBox); m_sounds->setHorizontalHeaderLabels({tr("Arquivo"),tr("Peso")});
    m_sounds->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); m_sounds->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    sv->addWidget(m_sounds);
    auto* sb = new QHBoxLayout; auto* addSound = new QPushButton(tr("Adicionar áudio…"),soundsBox); auto* removeSound = new QPushButton(tr("Remover"),soundsBox);
    sb->addWidget(addSound); sb->addWidget(removeSound); sb->addStretch(); sv->addLayout(sb); rv->addWidget(soundsBox,1);

    auto* defaults = new QGroupBox(tr("Fallbacks de terreno"),right); auto* dv = new QVBoxLayout(defaults);
    auto* df = new QFormLayout; m_defaultSurface = new QComboBox(defaults); df->addRow(tr("Superfície padrão do projeto:"),m_defaultSurface); dv->addLayout(df);
    m_terrain = new QTableWidget(0,2,defaults); m_terrain->setHorizontalHeaderLabels({tr("Número do terreno (RPG Maker)"),tr("Superfície")}); m_terrain->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch); dv->addWidget(m_terrain);
    auto* tb=new QHBoxLayout; auto* addTerrain=new QPushButton(tr("Adicionar Tag…"),defaults); auto* removeTerrain=new QPushButton(tr("Remover Tag"),defaults); tb->addWidget(addTerrain);tb->addWidget(removeTerrain);tb->addStretch();dv->addLayout(tb);
    rv->addWidget(defaults);

    auto* wangBox = new QGroupBox(tr("Terrenos automáticos"), right); auto* wv = new QVBoxLayout(wangBox);
    m_wang = new QTableWidget(0,3,wangBox); m_wang->setHorizontalHeaderLabels({tr("Grupo de conexões"),tr("Terreno"),tr("Superfície de passo")}); m_wang->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch); wv->addWidget(m_wang); rv->addWidget(wangBox);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close,this); root->addWidget(bb); connect(bb,&QDialogButtonBox::rejected,this,&QDialog::reject);

    connect(add,&QPushButton::clicked,this,[this]{
        core::FootstepSurface surface; surface.name=tr("Nova superfície"); ed.footstepSurfaces.push_back(surface); markChanged(); refreshSurfaces(surface.id);
    });
    connect(del,&QPushButton::clicked,this,[this]{
        if(m_current<0||m_current>=ed.footstepSurfaces.size())return;
        const QString id=ed.footstepSurfaces[m_current].id;
        const int uses=core::findProjectUses(ed,core::ReferenceSymbolKind::FootstepSurface,id).size();
        const QString question=uses>0
            ? tr("Esta superfície ainda é usada no projeto. Usos encontrados: %1, incluindo extensões visuais quando aplicável. Remover e limpar todas as referências?").arg(uses)
            : tr("Remover esta superfície?");
        if(QMessageBox::question(this,tr("Excluir superfície"),question,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        core::replaceFootstepSurfaceReferences(ed,id,QString());
        ed.footstepSurfaces.removeAt(m_current); markChanged(); refreshSurfaces();
    });
    connect(m_surfaces,&QListWidget::currentRowChanged,this,[this](int row){saveCurrent();m_current=row;loadCurrent();});
    auto saveSignal=[this]{saveCurrent();};
    connect(m_name,&QLineEdit::editingFinished,this,saveSignal); connect(m_volume,qOverload<int>(&QSpinBox::valueChanged),this,[saveSignal](int){saveSignal();});
    connect(m_pitchMin,qOverload<int>(&QSpinBox::valueChanged),this,[saveSignal](int){saveSignal();}); connect(m_pitchMax,qOverload<int>(&QSpinBox::valueChanged),this,[saveSignal](int){saveSignal();});
    connect(m_noRepeat,&QCheckBox::toggled,this,[saveSignal](bool){saveSignal();});
    connect(addSound,&QPushButton::clicked,this,[this]{
        if(m_current<0||m_current>=ed.footstepSurfaces.size())return; QString source; int volume=ed.footstepSurfaces[m_current].volume;
        if(!chooseGameAudio(ed,this,tr("Adicionar som de passo"),source,volume,QStringLiteral("SE"))||source.trimmed().isEmpty())return;
        core::FootstepSound sound; sound.sourcePath=source.trimmed(); sound.assetId=ed.assetDatabase.idForPath(sound.sourcePath); ed.footstepSurfaces[m_current].sounds.push_back(sound); ed.footstepSurfaces[m_current].volume=volume; markChanged(); loadCurrent();
    });
    connect(removeSound,&QPushButton::clicked,this,[this]{if(m_current<0||m_current>=ed.footstepSurfaces.size())return;const int r=m_sounds->currentRow();if(r<0||r>=ed.footstepSurfaces[m_current].sounds.size())return;ed.footstepSurfaces[m_current].sounds.removeAt(r);markChanged();refreshSoundTable();});
    connect(m_sounds,&QTableWidget::cellChanged,this,[this](int row,int col){if(m_loading||col!=1||m_current<0||m_current>=ed.footstepSurfaces.size()||row<0||row>=ed.footstepSurfaces[m_current].sounds.size())return;bool ok=false;int w=m_sounds->item(row,col)->text().toInt(&ok);if(ok){ed.footstepSurfaces[m_current].sounds[row].weight=qMax(1,w);markChanged();}});
    connect(m_defaultSurface,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){if(m_loading)return;ed.footstepSettings.defaultSurfaceId=m_defaultSurface->currentData().toString();markChanged();});
    connect(addTerrain,&QPushButton::clicked,this,[this]{bool ok=false;int tag=QInputDialog::getInt(this,tr("Número do terreno (RPG Maker)"),tr("Número do terreno usado no RPG Maker:"),0,0,999999,1,&ok);if(!ok)return;ed.footstepSettings.terrainSurfaceIds[tag]=currentSurfaceId();markChanged();refreshTerrainTable();});
    connect(removeTerrain,&QPushButton::clicked,this,[this]{int r=m_terrain->currentRow();if(r<0)return;ed.footstepSettings.terrainSurfaceIds.remove(m_terrain->item(r,0)->data(Qt::UserRole).toInt());markChanged();refreshTerrainTable();});

    refreshSurfaces();
}

QString FootstepManagerDialog::currentSurfaceId() const { return (m_current>=0&&m_current<ed.footstepSurfaces.size())?ed.footstepSurfaces[m_current].id:QString(); }
void FootstepManagerDialog::markChanged(){ed.markDirty();emit ed.tilesetsChanged();emit ed.wangChanged();emit ed.mapChanged();}
void FootstepManagerDialog::refreshSurfaces(const QString& selectId){m_loading=true;const QString wanted=selectId.isEmpty()?currentSurfaceId():selectId;m_surfaces->clear();int target=-1;for(int i=0;i<ed.footstepSurfaces.size();++i){m_surfaces->addItem(ed.footstepSurfaces[i].name);if(ed.footstepSurfaces[i].id==wanted)target=i;}m_loading=false;if(target<0&&!ed.footstepSurfaces.isEmpty())target=0;m_surfaces->setCurrentRow(target);m_current=target;loadCurrent();refreshTerrainTable();refreshWangTable();}
void FootstepManagerDialog::loadCurrent(){m_loading=true;const bool ok=m_current>=0&&m_current<ed.footstepSurfaces.size();m_name->setEnabled(ok);m_volume->setEnabled(ok);m_pitchMin->setEnabled(ok);m_pitchMax->setEnabled(ok);m_noRepeat->setEnabled(ok);if(ok){const auto&s=ed.footstepSurfaces[m_current];m_name->setText(s.name);m_volume->setValue(s.volume);m_pitchMin->setValue(s.pitchMin);m_pitchMax->setValue(s.pitchMax);m_noRepeat->setChecked(s.avoidImmediateRepeat);}else{m_name->clear();}fillSurfaceCombo(m_defaultSurface,ed,ed.footstepSettings.defaultSurfaceId,tr("Nenhuma"));m_loading=false;refreshSoundTable();}
void FootstepManagerDialog::saveCurrent(){if(m_loading||m_current<0||m_current>=ed.footstepSurfaces.size())return;auto&s=ed.footstepSurfaces[m_current];const QString name=m_name->text().trimmed().isEmpty()?tr("Superfície"):m_name->text().trimmed();const int mn=qMin(m_pitchMin->value(),m_pitchMax->value()),mx=qMax(m_pitchMin->value(),m_pitchMax->value());if(s.name==name&&s.volume==m_volume->value()&&s.pitchMin==mn&&s.pitchMax==mx&&s.avoidImmediateRepeat==m_noRepeat->isChecked())return;s.name=name;s.volume=m_volume->value();s.pitchMin=mn;s.pitchMax=mx;s.avoidImmediateRepeat=m_noRepeat->isChecked();if(m_surfaces->item(m_current))m_surfaces->item(m_current)->setText(name);markChanged();refreshTerrainTable();refreshWangTable();}
void FootstepManagerDialog::refreshSoundTable(){m_loading=true;m_sounds->setRowCount(0);if(m_current>=0&&m_current<ed.footstepSurfaces.size())for(const auto& sound:ed.footstepSurfaces[m_current].sounds){int r=m_sounds->rowCount();m_sounds->insertRow(r);m_sounds->setItem(r,0,new QTableWidgetItem(sound.sourcePath.isEmpty()?sound.assetId:sound.sourcePath));m_sounds->setItem(r,1,new QTableWidgetItem(QString::number(qMax(1,sound.weight))));}m_loading=false;}
void FootstepManagerDialog::refreshTerrainTable(){m_loading=true;m_terrain->setRowCount(0);QList<int> tags=ed.footstepSettings.terrainSurfaceIds.keys();std::sort(tags.begin(),tags.end());for(int tag:tags){int r=m_terrain->rowCount();m_terrain->insertRow(r);auto*it=new QTableWidgetItem(QString::number(tag));it->setData(Qt::UserRole,tag);m_terrain->setItem(r,0,it);auto*c=new QComboBox(m_terrain);fillSurfaceCombo(c,ed,ed.footstepSettings.terrainSurfaceIds.value(tag),tr("Nenhuma"));m_terrain->setCellWidget(r,1,c);connect(c,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,tag,c](int){if(m_loading)return;const QString id=c->currentData().toString();if(id.isEmpty())ed.footstepSettings.terrainSurfaceIds.remove(tag);else ed.footstepSettings.terrainSurfaceIds[tag]=id;markChanged();});}m_loading=false;}
void FootstepManagerDialog::refreshWangTable(){m_loading=true;m_wang->setRowCount(0);for(int wi=0;wi<ed.wangSets.size();++wi){auto&ws=ed.wangSets[wi];for(int ci=0;ci<ws.colors.size();++ci){auto&color=ws.colors[ci];int r=m_wang->rowCount();m_wang->insertRow(r);m_wang->setItem(r,0,new QTableWidgetItem(ws.name));m_wang->setItem(r,1,new QTableWidgetItem(color.name));auto*c=new QComboBox(m_wang);fillSurfaceCombo(c,ed,color.footstepSurfaceId,tr("Herdar"));m_wang->setCellWidget(r,2,c);connect(c,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,wi,ci,c](int){if(m_loading||wi>=ed.wangSets.size()||ci>=ed.wangSets[wi].colors.size())return;ed.wangSets[wi].colors[ci].footstepSurfaceId=c->currentData().toString();markChanged();});}}m_loading=false;}

} // namespace ui
