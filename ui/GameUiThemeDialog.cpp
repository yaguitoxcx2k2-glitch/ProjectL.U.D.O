#include "GameUiThemeDialog.h"

#include "AssetBrowser.h"
#include "UniversalAssetPicker.h"
#include "AudioPicker.h"
#include "EditorDialogGeometry.h"
#include "core/UiThemePackage.h"
#include "game/ui/UiPainterRenderer.h"
#include "game/ui/UiTheme.h"
#include "game/ui/UiStyleResolver.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QUuid>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace ui {

GameUiThemeDialog::GameUiThemeDialog(core::Editor& ed, QWidget* parent)
    : QDialog(parent), m_editor(ed), m_working(ed.gameUi)
{
    setWindowTitle(tr("Interface do jogo"));
    // Compartilha o mesmo contrato de geometria segura introduzido no RC2.52:
    // respeita availableGeometry, escala/DPI e restaura a ultima geometria sem
    // reaparecer atras da barra de tarefas ou fora de um monitor removido.
    restoreEditorDialogGeometry(*this, QStringLiteral("editor/gameUiThemeDialog"),
                                QSize(1240, 780), QSize(820, 560));
    connect(this, &QDialog::finished, this, [this](int) {
        saveEditorDialogGeometry(*this, QStringLiteral("editor/gameUiThemeDialog"));
    });
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Defina a aparência da interface do jogo e ajuste cada componente. A prévia mostra o mesmo visual que será usado durante o jogo."), this);
    intro->setWordWrap(true); intro->setStyleSheet(QStringLiteral("color:#8b93a7"));
    root->addWidget(intro);
    auto* themeActions = new QHBoxLayout;
    auto* loadThemeButton = new QPushButton(tr("Carregar tema…"), this);
    auto* saveThemeButton = new QPushButton(tr("Salvar tema…"), this);
    loadThemeButton->setToolTip(tr("Aplica cores, estilos e sons sem substituir suas telas ou a organização dos elementos."));
    saveThemeButton->setToolTip(tr("Salva a aparência atual em um arquivo .ludotheme que pode ser reutilizado em outros projetos."));
    themeActions->addWidget(loadThemeButton); themeActions->addWidget(saveThemeButton); themeActions->addStretch(1);
    root->addLayout(themeActions);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    auto* componentList = new QListWidget(splitter);
    componentList->setMinimumWidth(170); componentList->setMaximumWidth(240);
    componentList->setAlternatingRowColors(true);
    for (const QString& id : game::ui::nativeUiComponentIds()) {
        auto* item = new QListWidgetItem(game::ui::nativeUiComponentLabel(id), componentList);
        item->setData(Qt::UserRole, id);
    }
    componentList->setCurrentRow(0);

    auto* center = new QWidget(splitter);
    auto* centerLayout = new QVBoxLayout(center); centerLayout->setContentsMargins(0,0,0,0);
    auto* tabs = new QTabWidget(center); centerLayout->addWidget(tabs);
    auto page = [&](const QString& title) {
        auto* scroll = new QScrollArea(tabs); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto* w = new QWidget(scroll); auto* form = new QFormLayout(w); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); scroll->setWidget(w); tabs->addTab(scroll, title); return form;
    };
    auto* visual = page(tr("Aparência geral"));
    auto* behavior = page(tr("Transições"));
    auto* sounds = page(tr("Sons"));
    // O editor de Component/Style pode ficar mais alto que a area util da
    // janela (principalmente em notebooks e escalas de DPI maiores). Mantenha
    // todas as opcoes em um unico conteudo rolavel, em vez de deixar os
    // controles inferiores inacessiveis atras da borda/barra de tarefas.
    auto* widgetStylesScroll = new QScrollArea(tabs);
    widgetStylesScroll->setWidgetResizable(true);
    widgetStylesScroll->setFrameShape(QFrame::NoFrame);
    widgetStylesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    widgetStylesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* widgetStylesPage = new QWidget(widgetStylesScroll);
    auto* widgetStylesRoot = new QVBoxLayout(widgetStylesPage);
    widgetStylesRoot->setContentsMargins(10, 10, 10, 10);
    widgetStylesScroll->setWidget(widgetStylesPage);

    auto* previewPane = new QWidget(splitter);
    auto* previewLayout = new QVBoxLayout(previewPane); previewLayout->setContentsMargins(6,0,0,0);
    auto* previewHeader = new QHBoxLayout;
    auto* previewScene = new QComboBox(previewPane);
    previewScene->addItem(tr("Mensagem + nome"), QStringLiteral("message"));
    previewScene->addItem(tr("Escolhas"), QStringLiteral("choices"));
    previewScene->addItem(tr("Menu"), QStringLiteral("menu"));
    previewScene->addItem(tr("HUD e componentes"), QStringLiteral("hud"));
    auto* previewState = new QComboBox(previewPane);
    for(const auto& pair:{qMakePair(tr("Normal"),QStringLiteral("normal")),qMakePair(tr("Ao apontar"),QStringLiteral("hover")),qMakePair(tr("Com foco"),QStringLiteral("focused")),qMakePair(tr("Pressionado"),QStringLiteral("pressed")),qMakePair(tr("Selecionado"),QStringLiteral("selected")),qMakePair(tr("Desativado"),QStringLiteral("disabled"))}) previewState->addItem(pair.first,pair.second);
    previewHeader->addWidget(new QLabel(tr("Cena:"),previewPane)); previewHeader->addWidget(previewScene,1);
    previewHeader->addWidget(new QLabel(tr("Estado:"),previewPane)); previewHeader->addWidget(previewState);
    previewLayout->addLayout(previewHeader);
    auto* preview = new QLabel(previewPane);
    preview->setMinimumSize(390, 360); preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QStringLiteral("background:#101217;border:1px solid #353a46;border-radius:6px"));
    previewLayout->addWidget(preview,1);
    auto* previewHint = new QLabel(tr("O estado acima é aplicado ao componente selecionado na lista à esquerda."), previewPane);
    previewHint->setWordWrap(true); previewHint->setStyleSheet(QStringLiteral("color:#6f7789;font-size:11px")); previewLayout->addWidget(previewHint);

    splitter->addWidget(componentList); splitter->addWidget(center); splitter->addWidget(previewPane);
    splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,1); splitter->setStretchFactor(2,1);
    splitter->setSizes({190,560,470});
    root->addWidget(splitter,1);

    auto makePathRow = [&](QLineEdit** lineOut, const QString& initial, const QString& folder,
                           std::function<void(const QString&)> chosen) {
        auto* row = new QWidget(this); auto* h = new QHBoxLayout(row); h->setContentsMargins(0,0,0,0);
        auto* line = new QLineEdit(initial, row); line->setReadOnly(true); *lineOut = line;
        auto* choose = new QPushButton(tr("Escolher…"), row); auto* clear = new QPushButton(tr("Limpar"), row);
        h->addWidget(line,1); h->addWidget(choose); h->addWidget(clear);
        connect(choose,&QPushButton::clicked,this,[this,line,folder,chosen]{
            const QString path=AssetBrowserDialog::chooseImage(m_editor,this,folder); if(path.isEmpty())return;
            line->setText(m_editor.projectRelativePath(path)); chosen(path);
        });
        connect(clear,&QPushButton::clicked,this,[line,chosen]{line->clear();chosen(QString());});
        return row;
    };

    QLineEdit *skinLine=nullptr,*cursorLine=nullptr;
    auto* skinRow = makePathRow(&skinLine, m_working.windowSkinPath, QStringLiteral("Pictures"),
        [this](const QString& path){m_working.windowSkinPath=path.isEmpty()?QString():m_editor.projectRelativePath(path);m_working.windowSkin=path.isEmpty()?QImage():QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);});
    auto* cursorRow = makePathRow(&cursorLine, m_working.cursorPath, QStringLiteral("Pictures"),
        [this](const QString& path){m_working.cursorPath=path.isEmpty()?QString():m_editor.projectRelativePath(path);m_working.cursorImage=path.isEmpty()?QImage():QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);});
    visual->addRow(tr("Imagem da janela (9-slice):"),skinRow);
    visual->addRow(tr("Cursor de seleção:"),cursorRow);

    auto* slices = new QWidget(this); auto* sliceRow = new QHBoxLayout(slices); sliceRow->setContentsMargins(0,0,0,0);
    auto spinSlice=[&](int value){auto* s=new QSpinBox(slices);s->setRange(0,512);s->setValue(value);s->setSuffix(tr(" px"));sliceRow->addWidget(s);return s;};
    auto* sl=spinSlice(m_working.windowSkinSlices.left());auto* st=spinSlice(m_working.windowSkinSlices.top());
    auto* sr=spinSlice(m_working.windowSkinSlices.right());auto* sb=spinSlice(m_working.windowSkinSlices.bottom());
    visual->addRow(tr("Bordas 9-slice (E / C / D / B):"),slices);

    auto colorButton=[&](QColor* value){auto* b=new QPushButton(this);auto refresh=[b,value]{b->setText(value->name(QColor::HexArgb));b->setStyleSheet(QStringLiteral("background:%1;color:%2").arg(value->name(),value->lightness()>128?QStringLiteral("#111"):QStringLiteral("#fff")));};refresh();connect(b,&QPushButton::clicked,this,[=]{const QColor c=QColorDialog::getColor(*value,this,tr("Escolher cor"),QColorDialog::ShowAlphaChannel);if(c.isValid()){*value=c;refresh();}});return b;};
    auto* fillColorButton=colorButton(&m_working.windowFill);
    auto* borderColorButton=colorButton(&m_working.windowBorder);
    auto* innerBorderColorButton=colorButton(&m_working.innerBorder);
    auto* textColorButton=colorButton(&m_working.textColor);
    auto* selectedTextColorButton=colorButton(&m_working.selectedTextColor);
    auto* accentColorButton=colorButton(&m_working.accentColor);
    auto* selectionColorButton=colorButton(&m_working.selectionColor);
    visual->addRow(tr("Fundo:"),fillColorButton);
    visual->addRow(tr("Borda:"),borderColorButton);
    visual->addRow(tr("Borda interna:"),innerBorderColorButton);
    visual->addRow(tr("Texto:"),textColorButton);
    visual->addRow(tr("Texto selecionado:"),selectedTextColorButton);
    visual->addRow(tr("Destaque:"),accentColorButton);
    visual->addRow(tr("Seleção:"),selectionColorButton);

    auto* fontFamily=new QComboBox(this);
    fontFamily->addItem(tr("(fonte principal do projeto)"), QString());
    for (const core::ProjectFont& pf : m_editor.projectFonts) {
        if (!pf.sourcePath.isEmpty()) QFontDatabase::addApplicationFont(QDir(m_editor.projectRoot()).filePath(pf.sourcePath));
        if (fontFamily->findData(pf.family) < 0) fontFamily->addItem(tr("Projeto — %1").arg(pf.family), pf.family);
    }
    const QStringList systemFamilies = QFontDatabase::families();
    for (const QString& family : systemFamilies)
        if (fontFamily->findData(family) < 0) fontFamily->addItem(family, family);
    fontFamily->setCurrentIndex(qMax(0, fontFamily->findData(m_working.fontFamily)));
    visual->addRow(tr("Fonte:"),fontFamily);
    auto* fontSize=new QSpinBox(this);fontSize->setRange(6,96);fontSize->setValue(m_working.fontSize);fontSize->setSuffix(tr(" px"));visual->addRow(tr("Tamanho da fonte:"),fontSize);
    auto* padX=new QSpinBox(this);padX->setRange(0,128);padX->setValue(m_working.paddingX);padX->setSuffix(tr(" px"));visual->addRow(tr("Padding horizontal:"),padX);
    auto* padY=new QSpinBox(this);padY->setRange(0,128);padY->setValue(m_working.paddingY);padY->setSuffix(tr(" px"));visual->addRow(tr("Padding vertical:"),padY);
    auto* opacity=new QSpinBox(this);opacity->setRange(0,100);opacity->setValue(m_working.windowOpacity);opacity->setSuffix(QStringLiteral(" %"));visual->addRow(tr("Opacidade da janela:"),opacity);

    auto animationCombo=[&](const QString& current){
        auto* c=new QComboBox(this);
        c->addItem(tr("Nenhuma"),QStringLiteral("none"));
        c->addItem(tr("Fade"),QStringLiteral("fade"));
        c->addItem(tr("Scale + Fade"),QStringLiteral("scale-fade"));
        c->addItem(tr("Deslizar de baixo"),QStringLiteral("slide-up"));
        c->addItem(tr("Deslizar de cima"),QStringLiteral("slide-down"));
        c->addItem(tr("Deslizar da direita"),QStringLiteral("slide-left"));
        c->addItem(tr("Deslizar da esquerda"),QStringLiteral("slide-right"));
        c->setCurrentIndex(qMax(0,c->findData(current)));
        return c;
    };
    auto* openAnim=animationCombo(m_working.openAnimation);auto* closeAnim=animationCombo(m_working.closeAnimation);
    auto* easing=new QComboBox(this);
    easing->addItem(tr("Linear"),QStringLiteral("linear"));
    easing->addItem(tr("Ease In"),QStringLiteral("ease-in"));
    easing->addItem(tr("Ease Out"),QStringLiteral("ease-out"));
    easing->addItem(tr("Ease In/Out"),QStringLiteral("ease-in-out"));
    easing->addItem(tr("Back / Overshoot"),QStringLiteral("back"));
    easing->addItem(tr("Bounce"),QStringLiteral("bounce"));
    easing->addItem(tr("Elastic"),QStringLiteral("elastic"));
    easing->setCurrentIndex(qMax(0,easing->findData(m_working.animationEasing)));
    auto* duration=new QSpinBox(this);duration->setRange(0,2000);duration->setValue(m_working.animationMs);duration->setSuffix(tr(" ms"));
    behavior->addRow(tr("Abrir:"),openAnim);behavior->addRow(tr("Fechar:"),closeAnim);behavior->addRow(tr("Easing:"),easing);behavior->addRow(tr("Duração:"),duration);

    auto* soundVolume=new QSpinBox(this);soundVolume->setRange(0,100);soundVolume->setValue(m_working.soundVolume);soundVolume->setSuffix(QStringLiteral(" %"));sounds->addRow(tr("Volume da UI:"),soundVolume);
    auto addSound=[&](const QString& label,QString* path){auto* b=new QPushButton(path->isEmpty()?tr("Escolher…"):*path,this);sounds->addRow(label,b);connect(b,&QPushButton::clicked,this,[this,b,path,soundVolume]{int v=soundVolume->value();QString source=*path;if(chooseGameAudio(m_editor,this,tr("Som da interface"),source,v,QStringLiteral("SE"))){*path=source;soundVolume->setValue(v);b->setText(source.isEmpty()?tr("Escolher…"):source);}});return b;};
    auto* cursorSoundButton=addSound(tr("Mover cursor:"),&m_working.cursorSePath);
    auto* confirmSoundButton=addSound(tr("Confirmar:"),&m_working.confirmSePath);
    auto* cancelSoundButton=addSound(tr("Cancelar:"),&m_working.cancelSePath);

    // Compatibilidade de leitura com temas antigos; painel não exposto.
    auto* themeForm = new QFormLayout;
    auto* themeName = new QLineEdit(m_working.themeName, widgetStylesPage);
    auto* inheritWidgetSkin = new QCheckBox(tr("Usar a imagem da janela nos elementos automaticamente"), widgetStylesPage);
    inheritWidgetSkin->setChecked(m_working.widgetsInheritWindowSkin);
    inheritWidgetSkin->setToolTip(tr("Botões, janelas, listas e outros elementos podem usar automaticamente a mesma imagem de janela. Cada elemento ainda pode ter um estilo próprio."));
    themeForm->addRow(tr("Nome do tema:"), themeName); themeForm->addRow(inheritWidgetSkin);
    widgetStylesRoot->addLayout(themeForm);

    auto* nativeBox = new QGroupBox(tr("Componente selecionado"), widgetStylesPage);
    auto* nativeForm = new QFormLayout(nativeBox);
    auto* componentAssign = new QComboBox(nativeBox);
    auto* createComponentStyle = new QPushButton(tr("Criar estilo a partir do tema"), nativeBox);
    nativeForm->addRow(tr("Usar estilo:"), componentAssign);
    nativeForm->addRow(createComponentStyle);
    widgetStylesRoot->addWidget(nativeBox);

    auto* styleHeader = new QHBoxLayout;
    auto* styleCombo = new QComboBox(widgetStylesPage);
    auto* styleAdd = new QPushButton(tr("+ Estilo"), widgetStylesPage);
    auto* styleDelete = new QPushButton(tr("Excluir"), widgetStylesPage);
    styleHeader->addWidget(styleCombo,1); styleHeader->addWidget(styleAdd); styleHeader->addWidget(styleDelete);
    widgetStylesRoot->addLayout(styleHeader);

    auto* styleForm = new QFormLayout;
    auto* styleName = new QLineEdit(widgetStylesPage);
    auto* styleBackground = new QComboBox(widgetStylesPage);
    styleBackground->addItem(tr("Cor / painel"),QStringLiteral("color"));
    styleBackground->addItem(tr("Imagem global da janela (9-Slice)"),QStringLiteral("window-skin"));
    styleBackground->addItem(tr("Imagem própria (9-Slice)"),QStringLiteral("nine-slice"));
    styleBackground->addItem(tr("Sem fundo"),QStringLiteral("none"));
    auto* styleImageRow = new QWidget(widgetStylesPage); auto* styleImageLayout = new QHBoxLayout(styleImageRow); styleImageLayout->setContentsMargins(0,0,0,0);
    auto* styleImagePath = new QLineEdit(styleImageRow); auto* styleImageBrowse = new QPushButton(tr("…"),styleImageRow); styleImageBrowse->setFixedWidth(32); styleImageLayout->addWidget(styleImagePath,1);styleImageLayout->addWidget(styleImageBrowse);
    auto* styleSlicesRow = new QWidget(widgetStylesPage); auto* styleSlicesLayout = new QHBoxLayout(styleSlicesRow);styleSlicesLayout->setContentsMargins(0,0,0,0);
    auto makeStyleSlice=[styleSlicesRow,styleSlicesLayout]{auto*s=new QSpinBox(styleSlicesRow);s->setRange(0,512);s->setSuffix(QStringLiteral(" px"));styleSlicesLayout->addWidget(s);return s;};
    auto* styleSL=makeStyleSlice();auto*styleST=makeStyleSlice();auto*styleSR=makeStyleSlice();auto*styleSB=makeStyleSlice();
    auto* styleFill=new QPushButton(widgetStylesPage);auto*styleText=new QPushButton(widgetStylesPage);auto*styleAccent=new QPushButton(widgetStylesPage);auto*styleBorder=new QPushButton(widgetStylesPage);auto*styleInnerBorder=new QPushButton(widgetStylesPage);
    auto* styleBorderWidth=new QDoubleSpinBox(widgetStylesPage);styleBorderWidth->setRange(0,32);styleBorderWidth->setDecimals(1);
    auto* styleInnerBorderWidth=new QDoubleSpinBox(widgetStylesPage);styleInnerBorderWidth->setRange(0,32);styleInnerBorderWidth->setDecimals(1);
    auto* styleInnerInset=new QDoubleSpinBox(widgetStylesPage);styleInnerInset->setRange(0,128);styleInnerInset->setDecimals(1);
    auto* styleRadius=new QDoubleSpinBox(widgetStylesPage);styleRadius->setRange(0,256);styleRadius->setDecimals(1);
    auto* styleOpacity=new QSpinBox(widgetStylesPage);styleOpacity->setRange(0,100);styleOpacity->setSuffix(QStringLiteral(" %"));
    auto* stylePadX=new QSpinBox(widgetStylesPage);stylePadX->setRange(0,128);auto*stylePadY=new QSpinBox(widgetStylesPage);stylePadY->setRange(0,128);
    auto* styleFont=new QComboBox(widgetStylesPage);styleFont->addItem(tr("Usar aparência geral"),QString());for(const core::ProjectFont& pf:m_editor.projectFonts)if(styleFont->findData(pf.family)<0)styleFont->addItem(tr("Projeto — %1").arg(pf.family),pf.family);for(const QString& family:QFontDatabase::families())if(styleFont->findData(family)<0)styleFont->addItem(family,family);
    auto* styleFontSize=new QSpinBox(widgetStylesPage);styleFontSize->setRange(0,96);styleFontSize->setSpecialValueText(tr("Herdar"));styleFontSize->setSuffix(tr(" px"));
    styleForm->addRow(tr("Nome:"),styleName);styleForm->addRow(tr("Fundo:"),styleBackground);styleForm->addRow(tr("Imagem 9-Slice:"),styleImageRow);styleForm->addRow(tr("Margens 9-Slice (E/C/D/B):"),styleSlicesRow);
    styleForm->addRow(tr("Fundo adicional:"),styleFill);styleForm->addRow(tr("Texto:"),styleText);styleForm->addRow(tr("Destaque:"),styleAccent);styleForm->addRow(tr("Borda:"),styleBorder);styleForm->addRow(tr("Espessura da borda:"),styleBorderWidth);styleForm->addRow(tr("Borda interna:"),styleInnerBorder);styleForm->addRow(tr("Espessura interna:"),styleInnerBorderWidth);styleForm->addRow(tr("Recuo interno:"),styleInnerInset);styleForm->addRow(tr("Raio:"),styleRadius);styleForm->addRow(tr("Opacidade:"),styleOpacity);styleForm->addRow(tr("Espaçamento horizontal:"),stylePadX);styleForm->addRow(tr("Espaçamento vertical:"),stylePadY);styleForm->addRow(tr("Fonte:"),styleFont);styleForm->addRow(tr("Tamanho da fonte:"),styleFontSize);
    widgetStylesRoot->addLayout(styleForm);

    auto* stateBox = new QGroupBox(tr("Aparência por estado"), widgetStylesPage); auto* stateForm = new QFormLayout(stateBox);
    auto* styleState = new QComboBox(stateBox);for(const auto& pair: {qMakePair(tr("Normal"),QStringLiteral("normal")),qMakePair(tr("Ao apontar"),QStringLiteral("hover")),qMakePair(tr("Pressionado"),QStringLiteral("pressed")),qMakePair(tr("Com foco"),QStringLiteral("focused")),qMakePair(tr("Selecionado"),QStringLiteral("selected")),qMakePair(tr("Desativado"),QStringLiteral("disabled"))})styleState->addItem(pair.first,pair.second);
    auto* stateFill=new QPushButton(stateBox);auto*stateText=new QPushButton(stateBox);auto*stateAccent=new QPushButton(stateBox);auto*stateBorder=new QPushButton(stateBox);auto*stateInnerBorder=new QPushButton(stateBox);auto*stateOpacity=new QSpinBox(stateBox);stateOpacity->setRange(0,100);stateOpacity->setSuffix(QStringLiteral(" %"));
    auto* clearState = new QPushButton(tr("Remover ajustes deste estado"),stateBox);
    stateForm->addRow(tr("Estado:"),styleState);stateForm->addRow(tr("Fundo (opcional):"),stateFill);stateForm->addRow(tr("Texto (opcional):"),stateText);stateForm->addRow(tr("Destaque (opcional):"),stateAccent);stateForm->addRow(tr("Borda (opcional):"),stateBorder);stateForm->addRow(tr("Borda interna (opcional):"),stateInnerBorder);stateForm->addRow(tr("Opacidade:"),stateOpacity);stateForm->addRow(clearState);widgetStylesRoot->addWidget(stateBox);

    auto* typeBox = new QGroupBox(tr("Estilo padrão por tipo de elemento"), widgetStylesPage);auto*typeForm=new QFormLayout(typeBox);auto*typeCombo=new QComboBox(typeBox);
    const QStringList styleTypes={QStringLiteral("panel"),QStringLiteral("window"),QStringLiteral("card"),QStringLiteral("button"),QStringLiteral("dropdown"),QStringLiteral("text-input"),QStringLiteral("list"),QStringLiteral("sidebar"),QStringLiteral("context-menu"),QStringLiteral("data-table"),QStringLiteral("grid"),QStringLiteral("card-view"),QStringLiteral("inventory-grid"),QStringLiteral("inventory-slot"),QStringLiteral("item-slot"),QStringLiteral("equipment-slot"),QStringLiteral("skill-slot"),QStringLiteral("tooltip"),QStringLiteral("dialogue-bubble"),QStringLiteral("hint-bar"),QStringLiteral("checkbox"),QStringLiteral("radio"),QStringLiteral("toggle"),QStringLiteral("stepper"),QStringLiteral("slider"),QStringLiteral("scrollbar"),QStringLiteral("tab-bar"),QStringLiteral("segmented-control")};
    for(const QString&type:styleTypes)typeCombo->addItem(type,type);auto*typeStyle=new QComboBox(typeBox);typeForm->addRow(tr("Tipo:"),typeCombo);typeForm->addRow(tr("Estilo:"),typeStyle);widgetStylesRoot->addWidget(typeBox);
    auto* styleHint=new QLabel(tr("Use a aparência geral para manter tudo consistente. Quando um componente recebe um estilo próprio, ele mantém esse visual tanto no editor quanto durante o jogo."),widgetStylesPage);styleHint->setWordWrap(true);styleHint->setStyleSheet(QStringLiteral("color:#64748b;font-size:11px;"));widgetStylesRoot->addWidget(styleHint);widgetStylesRoot->addStretch(1);
    tabs->addTab(widgetStylesScroll,tr("Componentes e estilos"));

    const auto styleSyncing=std::make_shared<bool>(false);
    const auto paintStyleColor=[](QPushButton*b,const QColor&c,bool optional=false){if(optional&&!c.isValid()){b->setText(QObject::tr("Herdar"));b->setStyleSheet(QString());return;}const QColor shown=c.isValid()?c:QColor(Qt::white);b->setText(shown.name(QColor::HexArgb));b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;}").arg(shown.name(QColor::HexRgb),shown.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));};
    const auto refreshStyleCombo=[this,styleCombo,typeStyle,componentAssign,componentList,styleSyncing]{
        *styleSyncing=true;
        const QSignalBlocker blockStyle(styleCombo), blockType(typeStyle), blockComponent(componentAssign);
        const QString current=styleCombo->currentData().toString();
        styleCombo->clear();QStringList ids=m_working.styleClasses.keys();
        std::stable_sort(ids.begin(),ids.end(),[this](const QString&a,const QString&b){return m_working.styleClasses.value(a).name.localeAwareCompare(m_working.styleClasses.value(b).name)<0;});
        for(const QString&id:ids)styleCombo->addItem(m_working.styleClasses.value(id).name,id);
        int idx=styleCombo->findData(current);styleCombo->setCurrentIndex(idx>=0?idx:(styleCombo->count()?0:-1));
        const QString typeCurrent=typeStyle->currentData().toString();typeStyle->clear();typeStyle->addItem(tr("Automático"),QString());for(const QString&id:ids)typeStyle->addItem(m_working.styleClasses.value(id).name,id);idx=typeStyle->findData(typeCurrent);if(idx>=0)typeStyle->setCurrentIndex(idx);
        const QString componentId=componentList->currentItem()?componentList->currentItem()->data(Qt::UserRole).toString():QStringLiteral("theme");const QString mapped=m_working.nativeComponentStyles.value(componentId);componentAssign->clear();componentAssign->addItem(tr("Usar aparência geral"),QString());for(const QString&id:ids)componentAssign->addItem(m_working.styleClasses.value(id).name,id);componentAssign->setCurrentIndex(qMax(0,componentAssign->findData(mapped)));
        *styleSyncing=false;
    };
    const auto syncStyleEditor=[this,styleCombo,styleName,styleBackground,styleImagePath,styleSL,styleST,styleSR,styleSB,styleFill,styleText,styleAccent,styleBorder,styleInnerBorder,styleBorderWidth,styleInnerBorderWidth,styleInnerInset,styleRadius,styleOpacity,stylePadX,stylePadY,styleFont,styleFontSize,styleState,stateFill,stateText,stateAccent,stateBorder,stateInnerBorder,stateOpacity,typeCombo,typeStyle,styleSyncing,paintStyleColor]{
        *styleSyncing=true;auto it=m_working.styleClasses.find(styleCombo->currentData().toString());const bool has=it!=m_working.styleClasses.end();
        const QList<QWidget*> styleWidgets={styleName,styleBackground,styleImagePath,styleSL,styleST,styleSR,styleSB,styleFill,styleText,styleAccent,styleBorder,styleInnerBorder,styleBorderWidth,styleInnerBorderWidth,styleInnerInset,styleRadius,styleOpacity,stylePadX,stylePadY,styleFont,styleFontSize,styleState,stateFill,stateText,stateAccent,stateBorder,stateInnerBorder,stateOpacity};for(QWidget*w:styleWidgets)w->setEnabled(has);
        if(has){const auto&s=it.value();styleName->setText(s.name);styleBackground->setCurrentIndex(qMax(0,styleBackground->findData(s.backgroundMode)));styleImagePath->setText(s.imagePath);styleSL->setValue(s.slices.left());styleST->setValue(s.slices.top());styleSR->setValue(s.slices.right());styleSB->setValue(s.slices.bottom());paintStyleColor(styleFill,s.fillColor);paintStyleColor(styleText,s.textColor);paintStyleColor(styleAccent,s.accentColor);paintStyleColor(styleBorder,s.borderColor);paintStyleColor(styleInnerBorder,s.innerBorderColor);styleBorderWidth->setValue(s.borderWidth);styleInnerBorderWidth->setValue(s.innerBorderWidth);styleInnerInset->setValue(s.innerInset);styleRadius->setValue(s.radius);styleOpacity->setValue(qRound(s.opacity*100));stylePadX->setValue(s.paddingX);stylePadY->setValue(s.paddingY);styleFont->setCurrentIndex(qMax(0,styleFont->findData(s.fontFamily)));styleFontSize->setValue(s.fontSize);const QString sid=styleState->currentData().toString();const auto st=s.states.value(sid);paintStyleColor(stateFill,st.fillColor,true);paintStyleColor(stateText,st.textColor,true);paintStyleColor(stateAccent,st.accentColor,true);paintStyleColor(stateBorder,st.borderColor,true);paintStyleColor(stateInnerBorder,st.innerBorderColor,true);stateOpacity->setValue(qRound(st.opacityMultiplier*100));}
        const QString type=typeCombo->currentData().toString();const QString mapped=m_working.widgetTypeStyles.value(type);typeStyle->setCurrentIndex(qMax(0,typeStyle->findData(mapped)));*styleSyncing=false;
    };
    const auto editStyle=[this,styleCombo,styleSyncing,syncStyleEditor](const std::function<void(core::UiStyleClassSettings&)>&fn){if(*styleSyncing)return;auto it=m_working.styleClasses.find(styleCombo->currentData().toString());if(it==m_working.styleClasses.end())return;fn(it.value());syncStyleEditor();};
    connect(styleName,&QLineEdit::editingFinished,this,[styleName,styleCombo,editStyle,refreshStyleCombo,syncStyleEditor]{const QString id=styleCombo->currentData().toString();editStyle([styleName](core::UiStyleClassSettings&s){s.name=styleName->text().trimmed();if(s.name.isEmpty())s.name=QStringLiteral("Style");});refreshStyleCombo();styleCombo->setCurrentIndex(styleCombo->findData(id));syncStyleEditor();});
    connect(styleBackground,&QComboBox::currentIndexChanged,this,[styleBackground,editStyle](int){editStyle([styleBackground](core::UiStyleClassSettings&s){s.backgroundMode=styleBackground->currentData().toString();});});
    connect(styleImagePath,&QLineEdit::editingFinished,this,[this,styleImagePath,editStyle]{const QString stored=styleImagePath->text().trimmed();editStyle([this,stored](core::UiStyleClassSettings&s){s.imagePath=stored;QString abs=stored;if(!QFileInfo(abs).isAbsolute()&&!m_editor.projectRoot().isEmpty())abs=QDir(m_editor.projectRoot()).filePath(abs);s.image=QImage(abs);});});
    connect(styleImageBrowse,&QPushButton::clicked,this,[this,styleImagePath,editStyle]{const QString path=UniversalAssetPickerDialog::chooseOne(m_editor,this,QStringLiteral("ui.nine-slice"),QStringLiteral("image"),tr("Imagem 9-slice do estilo"));if(path.isEmpty())return;const QString stored=m_editor.projectRelativePath(path);styleImagePath->setText(stored);editStyle([stored,path](core::UiStyleClassSettings&s){s.imagePath=stored;s.image=QImage(path);s.backgroundMode=QStringLiteral("nine-slice");});});
    for(QSpinBox* spin:{styleSL,styleST,styleSR,styleSB})connect(spin,&QSpinBox::valueChanged,this,[styleSL,styleST,styleSR,styleSB,editStyle](int){editStyle([styleSL,styleST,styleSR,styleSB](core::UiStyleClassSettings&s){s.slices=QMargins(styleSL->value(),styleST->value(),styleSR->value(),styleSB->value());});});
    connect(styleBorderWidth,&QDoubleSpinBox::valueChanged,this,[editStyle](double v){editStyle([v](core::UiStyleClassSettings&s){s.borderWidth=v;});});connect(styleInnerBorderWidth,&QDoubleSpinBox::valueChanged,this,[editStyle](double v){editStyle([v](core::UiStyleClassSettings&s){s.innerBorderWidth=v;});});connect(styleInnerInset,&QDoubleSpinBox::valueChanged,this,[editStyle](double v){editStyle([v](core::UiStyleClassSettings&s){s.innerInset=v;});});connect(styleRadius,&QDoubleSpinBox::valueChanged,this,[editStyle](double v){editStyle([v](core::UiStyleClassSettings&s){s.radius=v;});});connect(styleOpacity,&QSpinBox::valueChanged,this,[editStyle](int v){editStyle([v](core::UiStyleClassSettings&s){s.opacity=v/100.0;});});connect(stylePadX,&QSpinBox::valueChanged,this,[editStyle](int v){editStyle([v](core::UiStyleClassSettings&s){s.paddingX=v;});});connect(stylePadY,&QSpinBox::valueChanged,this,[editStyle](int v){editStyle([v](core::UiStyleClassSettings&s){s.paddingY=v;});});connect(styleFont,&QComboBox::currentIndexChanged,this,[styleFont,editStyle](int){editStyle([styleFont](core::UiStyleClassSettings&s){s.fontFamily=styleFont->currentData().toString();});});connect(styleFontSize,&QSpinBox::valueChanged,this,[editStyle](int v){editStyle([v](core::UiStyleClassSettings&s){s.fontSize=v;});});
    const auto pickBase=[this,styleCombo,styleSyncing,syncStyleEditor](QPushButton*button,const QString&which){if(*styleSyncing)return;auto it=m_working.styleClasses.find(styleCombo->currentData().toString());if(it==m_working.styleClasses.end())return;QColor current=which==QLatin1String("fill")?it->fillColor:which==QLatin1String("text")?it->textColor:which==QLatin1String("accent")?it->accentColor:which==QLatin1String("inner")?it->innerBorderColor:it->borderColor;const QColor c=QColorDialog::getColor(current,button,tr("Cor do estilo"),QColorDialog::ShowAlphaChannel);if(!c.isValid())return;if(which==QLatin1String("fill"))it->fillColor=c;else if(which==QLatin1String("text"))it->textColor=c;else if(which==QLatin1String("accent"))it->accentColor=c;else if(which==QLatin1String("inner"))it->innerBorderColor=c;else it->borderColor=c;syncStyleEditor();};
    connect(styleFill,&QPushButton::clicked,this,[pickBase,styleFill]{pickBase(styleFill,QStringLiteral("fill"));});connect(styleText,&QPushButton::clicked,this,[pickBase,styleText]{pickBase(styleText,QStringLiteral("text"));});connect(styleAccent,&QPushButton::clicked,this,[pickBase,styleAccent]{pickBase(styleAccent,QStringLiteral("accent"));});connect(styleBorder,&QPushButton::clicked,this,[pickBase,styleBorder]{pickBase(styleBorder,QStringLiteral("border"));});connect(styleInnerBorder,&QPushButton::clicked,this,[pickBase,styleInnerBorder]{pickBase(styleInnerBorder,QStringLiteral("inner"));});
    const auto pickState=[this,styleCombo,styleState,styleSyncing,syncStyleEditor](QPushButton*button,const QString&which){if(*styleSyncing)return;auto it=m_working.styleClasses.find(styleCombo->currentData().toString());if(it==m_working.styleClasses.end())return;const QString sid=styleState->currentData().toString();auto st=it->states.value(sid);QColor current=which==QLatin1String("fill")?st.fillColor:which==QLatin1String("text")?st.textColor:which==QLatin1String("accent")?st.accentColor:which==QLatin1String("border")?st.borderColor:st.innerBorderColor;if(!current.isValid())current=which==QLatin1String("fill")?it->fillColor:which==QLatin1String("text")?it->textColor:which==QLatin1String("accent")?it->accentColor:which==QLatin1String("border")?it->borderColor:it->innerBorderColor;const QColor c=QColorDialog::getColor(current,button,tr("Override do estado"),QColorDialog::ShowAlphaChannel);if(!c.isValid())return;if(which==QLatin1String("fill"))st.fillColor=c;else if(which==QLatin1String("text"))st.textColor=c;else if(which==QLatin1String("accent"))st.accentColor=c;else if(which==QLatin1String("border"))st.borderColor=c;else st.innerBorderColor=c;it->states.insert(sid,st);syncStyleEditor();};
    connect(stateFill,&QPushButton::clicked,this,[pickState,stateFill]{pickState(stateFill,QStringLiteral("fill"));});connect(stateText,&QPushButton::clicked,this,[pickState,stateText]{pickState(stateText,QStringLiteral("text"));});connect(stateAccent,&QPushButton::clicked,this,[pickState,stateAccent]{pickState(stateAccent,QStringLiteral("accent"));});connect(stateBorder,&QPushButton::clicked,this,[pickState,stateBorder]{pickState(stateBorder,QStringLiteral("border"));});connect(stateInnerBorder,&QPushButton::clicked,this,[pickState,stateInnerBorder]{pickState(stateInnerBorder,QStringLiteral("inner"));});
    connect(stateOpacity,&QSpinBox::valueChanged,this,[this,styleCombo,styleState,styleSyncing,syncStyleEditor](int v){if(*styleSyncing)return;auto it=m_working.styleClasses.find(styleCombo->currentData().toString());if(it==m_working.styleClasses.end())return;auto st=it->states.value(styleState->currentData().toString());st.opacityMultiplier=v/100.0;it->states.insert(styleState->currentData().toString(),st);syncStyleEditor();});
    connect(clearState,&QPushButton::clicked,this,[this,styleCombo,styleState,syncStyleEditor]{auto it=m_working.styleClasses.find(styleCombo->currentData().toString());if(it==m_working.styleClasses.end())return;it->states.remove(styleState->currentData().toString());syncStyleEditor();});
    connect(typeStyle,&QComboBox::currentIndexChanged,this,[this,typeCombo,typeStyle,styleSyncing](int){if(*styleSyncing)return;const QString type=typeCombo->currentData().toString(),style=typeStyle->currentData().toString();if(style.isEmpty())m_working.widgetTypeStyles.remove(type);else m_working.widgetTypeStyles.insert(type,style);});
    connect(componentAssign,&QComboBox::currentIndexChanged,this,[this,componentAssign,componentList,styleCombo,styleSyncing](int){if(*styleSyncing||!componentList->currentItem())return;const QString component=componentList->currentItem()->data(Qt::UserRole).toString();if(component==QLatin1String("theme"))return;const QString id=componentAssign->currentData().toString();if(id.isEmpty())m_working.nativeComponentStyles.remove(component);else{m_working.nativeComponentStyles.insert(component,id);const int idx=styleCombo->findData(id);if(idx>=0)styleCombo->setCurrentIndex(idx);}});
    connect(componentList,&QListWidget::currentRowChanged,this,[refreshStyleCombo,tabs](int row){refreshStyleCombo();if(row>0)tabs->setCurrentIndex(tabs->count()-1);});
    connect(createComponentStyle,&QPushButton::clicked,this,[this,componentList,styleCombo,refreshStyleCombo,syncStyleEditor]{if(!componentList->currentItem())return;const QString component=componentList->currentItem()->data(Qt::UserRole).toString();if(component==QLatin1String("theme"))return;core::UiStyleClassSettings st;st.id=QStringLiteral("style.")+QUuid::createUuid().toString(QUuid::WithoutBraces);st.name=game::ui::nativeUiComponentLabel(component);st.backgroundMode=m_working.windowSkin.isNull()?QStringLiteral("color"):QStringLiteral("window-skin");st.fillColor=m_working.windowFill;st.textColor=m_working.textColor;st.accentColor=m_working.accentColor;st.borderColor=m_working.windowBorder;st.innerBorderColor=m_working.innerBorder;st.paddingX=m_working.paddingX;st.paddingY=m_working.paddingY;st.fontFamily=m_working.fontFamily;st.fontSize=m_working.fontSize;m_working.styleClasses.insert(st.id,st);m_working.nativeComponentStyles.insert(component,st.id);refreshStyleCombo();styleCombo->setCurrentIndex(styleCombo->findData(st.id));syncStyleEditor();});

    // Os callbacks vivem mais que o construtor. Por isso todos os widgets são
    // capturados por valor; não guardamos referências para variáveis locais.
    const auto sync=[this,sl,st,sr,sb,fontFamily,fontSize,padX,padY,opacity,openAnim,closeAnim,easing,duration,soundVolume,themeName,inheritWidgetSkin]{
        m_working.themeName=themeName->text().trimmed();if(m_working.themeName.isEmpty())m_working.themeName=QStringLiteral("LUDO Theme");m_working.widgetsInheritWindowSkin=inheritWidgetSkin->isChecked();
        m_working.windowSkinSlices=QMargins(sl->value(),st->value(),sr->value(),sb->value());
        m_working.fontFamily=fontFamily->currentData().toString();m_working.fontSize=fontSize->value();m_working.paddingX=padX->value();m_working.paddingY=padY->value();
        m_working.windowOpacity=opacity->value();m_working.openAnimation=openAnim->currentData().toString();
        m_working.closeAnimation=closeAnim->currentData().toString();m_working.animationEasing=easing->currentData().toString();m_working.animationMs=duration->value();
        m_working.soundVolume=soundVolume->value();
    };
    const auto paintGeneralColor=[](QPushButton* b,const QColor& value){
        b->setText(value.name(QColor::HexArgb));
        b->setStyleSheet(QStringLiteral("background:%1;color:%2")
                             .arg(value.name(), value.lightness()>128?QStringLiteral("#111"):QStringLiteral("#fff")));
    };
    const auto reloadControls=[this,skinLine,cursorLine,sl,st,sr,sb,
                               fillColorButton,borderColorButton,innerBorderColorButton,textColorButton,
                               selectedTextColorButton,accentColorButton,selectionColorButton,
                               fontFamily,fontSize,padX,padY,opacity,openAnim,closeAnim,easing,duration,
                               soundVolume,cursorSoundButton,confirmSoundButton,cancelSoundButton,
                               themeName,inheritWidgetSkin,refreshStyleCombo,syncStyleEditor,paintGeneralColor]{
        // Programmatic model -> controls synchronization must NOT write back to
        // the model through the same valueChanged signals. This was the source
        // of the "properties disappear until I touch something" symptom.
        const QSignalBlocker bSl(sl),bSt(st),bSr(sr),bSb(sb),bFont(fontFamily),bFontSize(fontSize),
                             bPadX(padX),bPadY(padY),bOpacity(opacity),bOpen(openAnim),bClose(closeAnim),
                             bEase(easing),bDuration(duration),bVolume(soundVolume),bTheme(themeName),bInherit(inheritWidgetSkin);
        skinLine->setText(m_working.windowSkinPath); cursorLine->setText(m_working.cursorPath);
        sl->setValue(m_working.windowSkinSlices.left()); st->setValue(m_working.windowSkinSlices.top());
        sr->setValue(m_working.windowSkinSlices.right()); sb->setValue(m_working.windowSkinSlices.bottom());
        paintGeneralColor(fillColorButton,m_working.windowFill); paintGeneralColor(borderColorButton,m_working.windowBorder);
        paintGeneralColor(innerBorderColorButton,m_working.innerBorder); paintGeneralColor(textColorButton,m_working.textColor);
        paintGeneralColor(selectedTextColorButton,m_working.selectedTextColor); paintGeneralColor(accentColorButton,m_working.accentColor);
        paintGeneralColor(selectionColorButton,m_working.selectionColor);
        int idx=fontFamily->findData(m_working.fontFamily);
        if(idx<0 && !m_working.fontFamily.isEmpty()){fontFamily->addItem(m_working.fontFamily,m_working.fontFamily);idx=fontFamily->findData(m_working.fontFamily);}
        fontFamily->setCurrentIndex(qMax(0,idx)); fontSize->setValue(m_working.fontSize);
        padX->setValue(m_working.paddingX); padY->setValue(m_working.paddingY); opacity->setValue(m_working.windowOpacity);
        openAnim->setCurrentIndex(qMax(0,openAnim->findData(m_working.openAnimation)));
        closeAnim->setCurrentIndex(qMax(0,closeAnim->findData(m_working.closeAnimation)));
        easing->setCurrentIndex(qMax(0,easing->findData(m_working.animationEasing))); duration->setValue(m_working.animationMs);
        soundVolume->setValue(m_working.soundVolume);
        cursorSoundButton->setText(m_working.cursorSePath.isEmpty()?tr("Escolher…"):m_working.cursorSePath);
        confirmSoundButton->setText(m_working.confirmSePath.isEmpty()?tr("Escolher…"):m_working.confirmSePath);
        cancelSoundButton->setText(m_working.cancelSePath.isEmpty()?tr("Escolher…"):m_working.cancelSePath);
        themeName->setText(m_working.themeName); inheritWidgetSkin->setChecked(m_working.widgetsInheritWindowSkin);
        refreshStyleCombo(); syncStyleEditor();
    };
    const auto refresh=[this,preview,sync,previewScene,previewState,componentList]{
        sync();
        QImage img(QSize(720,405),QImage::Format_ARGB32_Premultiplied);img.fill(QColor("#18202f"));
        QPainter p(&img);p.fillRect(QRect(0,258,720,147),QColor("#2f5639"));
        p.fillRect(QRect(0,0,720,54),QColor("#18263d"));
        game::ui::UiTheme theme=game::ui::UiTheme::fromSettings(m_working);game::ui::UiDrawList list;
        QFont fallback=font();if(!theme.fontFamily.isEmpty())fallback.setFamily(theme.fontFamily);fallback.setPixelSize(theme.fontSize);
        const QString selectedComponent=componentList->currentItem()?componentList->currentItem()->data(Qt::UserRole).toString():QStringLiteral("theme");
        const QString selectedState=previewState->currentData().toString();
        const auto stateFor=[&](const QString&id){return selectedComponent==id?selectedState:QStringLiteral("normal");};
        const auto styleFor=[&](const QString&id){return game::ui::resolveNativeStyle(theme,m_editor,id,stateFor(id),fallback);};
        const auto drawBox=[&](const QRectF&r,const QString&id,qreal op=1.0){const auto st=styleFor(id);game::ui::appendResolvedBackground(list,r,theme,st,op);return st;};
        const QString scene=previewScene->currentData().toString();
        if(scene==QLatin1String("message")){
            const QRectF box(28,270,664,110);const auto ms=drawBox(box,QStringLiteral("message"));
            const QRectF name(48,231,196,42);const auto ns=drawBox(name,QStringLiteral("name-box"));
            list.addText(name.adjusted(ns.paddingX,0,-ns.paddingX,0),tr("Ludovicense"),ns.font,ns.text,Qt::AlignLeft|Qt::AlignVCenter);
            list.addText(box.adjusted(ms.paddingX,ms.paddingY,-ms.paddingX,-ms.paddingY),tr("A interface mantém a mesma aparência durante a edição e no jogo."),ms.font,ms.text,int(Qt::AlignLeft|Qt::AlignVCenter)|int(Qt::TextWordWrap));
            list.addTriangle(QPointF(box.right()-20,box.bottom()-14),QSizeF(12,8),ms.accent,1.0,true);
        }else if(scene==QLatin1String("choices")){
            const QRectF box(330,105,340,205);const auto ws=drawBox(box,QStringLiteral("choices"));
            const QStringList labels={tr("Continuar"),tr("Configurações"),tr("Bloqueado")};
            for(int i=0;i<labels.size();++i){const QString state=i==0?QStringLiteral("selected"):(i==2?QStringLiteral("disabled"):QStringLiteral("normal"));const auto item=game::ui::resolveNativeStyle(theme,m_editor,QStringLiteral("choice-item"),selectedComponent==QLatin1String("choice-item")?selectedState:state,fallback);QRectF row(box.left()+ws.paddingX,box.top()+ws.paddingY+i*54,box.width()-ws.paddingX*2,46);game::ui::appendResolvedBackground(list,row,theme,item);list.addText(row.adjusted(16,0,-10,0),labels[i],item.font,item.text,Qt::AlignLeft|Qt::AlignVCenter,item.opacity);}
        }else if(scene==QLatin1String("menu")){
            const auto ms=drawBox(QRectF(35,72,650,300),QStringLiteral("menu"));
            drawBox(QRectF(55,95,235,250),QStringLiteral("menu"),.82);drawBox(QRectF(310,95,350,250),QStringLiteral("menu"),.72);
            const auto sel=game::ui::resolveNativeStyle(theme,m_editor,QStringLiteral("selection"),selectedComponent==QLatin1String("selection")?selectedState:QStringLiteral("selected"),fallback);QRectF row(66,112,212,42);game::ui::appendResolvedBackground(list,row,theme,sel);list.addText(row.adjusted(14,0,-8,0),tr("Itens"),sel.font,sel.text,Qt::AlignLeft|Qt::AlignVCenter);list.addText(QRectF(328,112,300,35),tr("Menu Principal"),ms.font,ms.accent,Qt::AlignLeft|Qt::AlignVCenter);list.addText(QRectF(328,154,300,120),tr("O menu pode usar a aparência geral ou ter um estilo próprio."),ms.font,ms.text,int(Qt::AlignLeft|Qt::AlignTop)|int(Qt::TextWordWrap));
        }else{
            const auto hs=drawBox(QRectF(28,75,310,92),QStringLiteral("hud"));list.addText(QRectF(48,92,270,28),tr("HP  450 / 500"),hs.font,hs.text,Qt::AlignLeft|Qt::AlignVCenter);list.addGauge(QRectF(48,126,260,18),.9,QColor(0,0,0,130),hs.accent,hs.panel.border,7);
            const auto modal=drawBox(QRectF(365,86,315,190),QStringLiteral("modal"));list.addText(QRectF(386,104,275,32),tr("Janela / Modal"),modal.font,modal.accent,Qt::AlignLeft|Qt::AlignVCenter);const auto sel=game::ui::resolveNativeStyle(theme,m_editor,QStringLiteral("selection"),selectedComponent==QLatin1String("selection")?selectedState:QStringLiteral("selected"),fallback);QRectF row(386,200,270,42);game::ui::appendResolvedBackground(list,row,theme,sel);list.addText(row,tr("Confirmar"),sel.font,sel.text,Qt::AlignCenter);
        }
        game::ui::UiPainterRenderer::render(p,list);p.end();
        const QSize rawTarget=preview->size()-QSize(8,8);const QSize target(qMax(32,rawTarget.width()),qMax(32,rawTarget.height()));
        preview->setPixmap(QPixmap::fromImage(img).scaled(target,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    };

    connect(loadThemeButton,&QPushButton::clicked,this,[this,reloadControls,refresh]{
        const QString path=QFileDialog::getOpenFileName(this,tr("Carregar tema da Interface In-Game"),m_editor.projectRoot(),tr("Tema LUDO (*.ludotheme)"));
        if(path.isEmpty())return;
        core::GameUiSettings loaded;
        QString error;
        if(!core::io::loadUiThemePackage(m_working,path,&loaded,&error)){
            QMessageBox::warning(this,tr("Carregar tema"),error);return;
        }
        m_working=loaded;
        reloadControls();
        refresh();
    });
    connect(saveThemeButton,&QPushButton::clicked,this,[this,sync]{
        sync();
        QString path=QFileDialog::getSaveFileName(this,tr("Salvar tema da Interface In-Game"),
            QDir(m_editor.projectRoot()).filePath(m_working.themeName+core::io::uiThemePackageExtension()),
            tr("Tema LUDO (*.ludotheme)"));
        if(path.isEmpty())return;
        if(!path.endsWith(core::io::uiThemePackageExtension(),Qt::CaseInsensitive))path+=core::io::uiThemePackageExtension();
        QString error;
        if(!core::io::saveUiThemePackage(m_working,path,&error))
            QMessageBox::warning(this,tr("Salvar tema"),error);
    });

    for(QSpinBox* spin:{sl,st,sr,sb,fontSize,padX,padY,opacity,duration,soundVolume})connect(spin,&QSpinBox::valueChanged,this,[refresh](int){refresh();});
    for(QComboBox* combo:{fontFamily,openAnim,closeAnim,easing})connect(combo,&QComboBox::currentIndexChanged,this,[refresh](int){refresh();});
    connect(previewScene,&QComboBox::currentIndexChanged,this,[refresh](int){refresh();});connect(previewState,&QComboBox::currentIndexChanged,this,[refresh](int){refresh();});connect(componentList,&QListWidget::currentRowChanged,this,[refresh](int){refresh();});
    for(QSpinBox* spin:{styleSL,styleST,styleSR,styleSB,styleOpacity,stylePadX,stylePadY,styleFontSize,stateOpacity})connect(spin,&QSpinBox::valueChanged,this,[refresh](int){refresh();});
    for(QDoubleSpinBox* spin:{styleBorderWidth,styleInnerBorderWidth,styleInnerInset,styleRadius})connect(spin,&QDoubleSpinBox::valueChanged,this,[refresh](double){refresh();});
    for(QComboBox* combo:{styleCombo,styleBackground,styleFont,styleState,componentAssign})connect(combo,&QComboBox::currentIndexChanged,this,[this,refresh](int){QTimer::singleShot(0,this,[refresh]{refresh();});});
    connect(styleName,&QLineEdit::editingFinished,this,[refresh]{refresh();});
    for(QPushButton* b:findChildren<QPushButton*>())connect(b,&QPushButton::clicked,this,[this,refresh]{QTimer::singleShot(0,this,refresh);});

    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);root->addWidget(box);
    connect(box,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(box,&QDialogButtonBox::accepted,this,[this,sync]{sync();m_editor.gameUi=m_working;m_editor.markDirty();accept();});
    // Inicializa controles de Style/Component diretamente do model. Antes, a
    // lista só era populada após um currentRowChanged/valueChanged, produzindo
    // o sintoma de propriedades "sumirem" até o usuário tocar em outra opção.
    reloadControls();
    refresh();
}

} // namespace ui
