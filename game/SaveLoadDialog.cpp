#include "SaveLoadDialog.h"

#include "game/GameSave.h"
#include "game/GamepadInput.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace game {
namespace {
QString timeText(qint64 seconds)
{
    const qint64 h=seconds/3600,m=(seconds%3600)/60,s=seconds%60;
    return QStringLiteral("%1:%2:%3").arg(h,2,10,QLatin1Char('0')).arg(m,2,10,QLatin1Char('0')).arg(s,2,10,QLatin1Char('0'));
}
}

SaveLoadDialog::SaveLoadDialog(const core::Editor& editor,Mode mode,QWidget* parent)
    :QDialog(parent),m_editor(editor),m_mode(mode)
{
    setWindowTitle(mode==Mode::LoadOnly?tr("Carregar partida"):tr("Salvar / carregar"));resize(680,520);
    auto* root=new QVBoxLayout(this);auto* title=new QLabel(tr("Escolha um slot"),this);QFont f=title->font();f.setPointSize(f.pointSize()+5);f.setBold(true);title->setFont(f);root->addWidget(title);
    m_slots=new QListWidget(this);m_slots->setAlternatingRowColors(true);root->addWidget(m_slots,1);
    auto* buttons=new QHBoxLayout;
    if(mode==Mode::SaveAndLoad){m_save=new QPushButton(tr("Salvar"),this);buttons->addWidget(m_save);connect(m_save,&QPushButton::clicked,this,[this]{choose(Action::Save);});}
    m_load=new QPushButton(tr("Carregar"),this);buttons->addWidget(m_load);buttons->addStretch();auto* cancel=new QPushButton(tr("Voltar"),this);buttons->addWidget(cancel);root->addLayout(buttons);
    connect(m_load,&QPushButton::clicked,this,[this]{choose(Action::Load);});connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    // Duplo clique sempre carrega. Salvar exige o botão explícito para não
    // sobrescrever um slot por acidente.
    connect(m_slots,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){choose(Action::Load);});
    connect(m_slots,&QListWidget::currentRowChanged,this,[this](int){GameSaveSummary s;const bool occupied=readGameSaveSummary(gameSavePath(m_editor,selectedSlot()),m_editor,&s);m_load->setEnabled(occupied);});
    refresh();
    m_slots->setFocus();
    new GamepadDialogNavigator(this, m_editor.inputSystem);
}

int SaveLoadDialog::selectedSlot() const{return m_slots?qMax(1,m_slots->currentRow()+1):1;}

void SaveLoadDialog::refresh()
{
    m_slots->clear();
    for(int slot=1;slot<=99;++slot){GameSaveSummary s;QString line;if(readGameSaveSummary(gameSavePath(m_editor,slot),m_editor,&s)&&s.valid){line=tr("Slot %1   %2   %3   %4").arg(slot,2,10,QLatin1Char('0')).arg(s.mapName).arg(timeText(s.playTimeSeconds)).arg(s.savedAt.isValid()?s.savedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")):tr("data desconhecida"));}else line=tr("Slot %1   — vazio —").arg(slot,2,10,QLatin1Char('0'));auto* item=new QListWidgetItem(line,m_slots);item->setData(Qt::UserRole,slot);}
    m_slots->setCurrentRow(0);
}

void SaveLoadDialog::choose(Action action)
{
    if(action==Action::Load){GameSaveSummary s;if(!readGameSaveSummary(gameSavePath(m_editor,selectedSlot()),m_editor,&s))return;}
    m_action=action;accept();
}

} // namespace game
