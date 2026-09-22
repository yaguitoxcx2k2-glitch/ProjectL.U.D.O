#include "PropertiesPanel.h"
#include "UniversalAssetPicker.h"
#include "AssetBrowser.h"
#include "Icons.h"
#include "LayerFiltersDialog.h"
#include "core/Renderer.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QGroupBox>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

using namespace core;

namespace ui {

/// Secao compacta do Inspector. O cabecalho inteiro funciona como botao e a
/// seta comunica claramente se as opcoes estao abertas ou recolhidas.
/// O estado e lembrado por categoria, sem entrar no arquivo do projeto.
class CollapsibleSection final : public QWidget
{
public:
    CollapsibleSection(const QString& title, const QString& settingsKey,
                       bool expandedByDefault, QWidget* parent = nullptr)
        : QWidget(parent), m_title(title), m_settingsKey(settingsKey)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 3, 0, 3);
        root->setSpacing(0);
        m_header = new QToolButton(this);
        m_header->setObjectName(QStringLiteral("inspectorSectionHeader"));
        m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_header->setArrowType(Qt::RightArrow);
        m_header->setCheckable(true);
        m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_header->setStyleSheet(QStringLiteral(
            "QToolButton#inspectorSectionHeader { text-align: left; font-weight: 600; "
            "padding: 6px; border: 0; border-bottom: 1px solid rgba(255,255,255,32); }"
            "QToolButton#inspectorSectionHeader:hover { background: rgba(255,255,255,18); }"));
        root->addWidget(m_header);
        m_content = new QWidget(this);
        root->addWidget(m_content);

        const bool expanded = QSettings().value(m_settingsKey, expandedByDefault).toBool();
        connect(m_header, &QToolButton::toggled, this, [this](bool on) {
            applyExpanded(on);
            QSettings().setValue(m_settingsKey, on);
        });
        m_header->setChecked(expanded);
        applyExpanded(expanded);
    }

    QWidget* contentWidget() const { return m_content; }
    void setTitle(const QString& title) { m_title = title; updateHeader(); }

private:
    void applyExpanded(bool expanded)
    {
        m_content->setVisible(expanded);
        m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        updateHeader();
    }
    void updateHeader() { m_header->setText(m_title); }

    QString m_title;
    QString m_settingsKey;
    QToolButton* m_header = nullptr;
    QWidget* m_content = nullptr;
};

/// Envolve um formulario num QScrollArea para caber em painel estreito.
static QWidget* scrolled(QWidget* inner)
{
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(inner);
    return area;
}

/// Renderiza um Pattern como ele sera pintado no mapa. O preview usa uma
/// celula uniforme e nearest-neighbour, pois Stamp armazena coordenadas
/// logicas e pode referenciar mais de um tileset.
static QPixmap patternPreviewPixmap(const Editor& ed, const SavedStamp& saved,
                                    const QSize& boxSize, bool* fullyValid)
{
    if (fullyValid) *fullyValid = false;
    if (!saved.stamp.valid()) return {};

    const int boxW = qMax(96, boxSize.width());
    const int boxH = qMax(96, boxSize.height());
    QPixmap out(boxW, boxH);
    out.fill(Qt::transparent);

    constexpr int margin = 10;
    const int availableW = qMax(1, boxW - margin * 2);
    const int availableH = qMax(1, boxH - margin * 2);
    const int cell = qMax(3, qMin(64, qMin(availableW / qMax(1, saved.stamp.w),
                                          availableH / qMax(1, saved.stamp.h))));
    const int drawW = saved.stamp.w * cell;
    const int drawH = saved.stamp.h * cell;
    const int originX = (boxW - drawW) / 2;
    const int originY = (boxH - drawH) / 2;

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Fundo quadriculado leve: deixa buracos/areas transparentes do Pattern
    // legiveis sem impor uma cor que dependa do tema do Editor.
    for (int y = 0; y < saved.stamp.h; ++y) {
        for (int x = 0; x < saved.stamp.w; ++x) {
            const QColor bg = ((x + y) & 1) ? QColor(255,255,255,18)
                                             : QColor(0,0,0,20);
            p.fillRect(QRect(originX + x * cell, originY + y * cell, cell, cell), bg);
        }
    }

    bool valid = saved.stamp.tiles.size() == saved.stamp.offsets.size();
    const int count = qMin(saved.stamp.tiles.size(), saved.stamp.offsets.size());
    for (int i = 0; i < count; ++i) {
        const TileRef& t = saved.stamp.tiles[i];
        const QPoint off = saved.stamp.offsets[i];
        const Tileset* ts = ed.tilesetAt(t.tilesetIdx);
        if (!ts || off.x() < 0 || off.y() < 0 ||
            off.x() >= saved.stamp.w || off.y() >= saved.stamp.h ||
            t.tx < 0 || t.ty < 0 || t.tx >= ts->columns || t.ty >= ts->rows) {
            valid = false;
            continue;
        }
        p.drawPixmap(QRect(originX + off.x() * cell, originY + off.y() * cell, cell, cell),
                     pixmapCache().pixmap(ed, t.tilesetIdx), ts->tileRect(t.tx, t.ty));
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255,255,255,60), 1));
    p.drawRect(QRect(originX, originY, qMax(0, drawW - 1), qMax(0, drawH - 1)));
    if (!valid) {
        p.setPen(QPen(QColor(235,90,90,210), 2));
        p.drawLine(originX, originY, originX + drawW, originY + drawH);
        p.drawLine(originX + drawW, originY, originX, originY + drawH);
    }

    if (fullyValid) *fullyValid = valid;
    return out;
}

/// Tamanho fonte real do objeto antes da escala visual. Usa o primeiro tile
/// válido como referência; objetos sem tiles permanecem em 100% porque não
/// existe uma resolução nativa externa para comparar.
static QSizeF objectNativeSize(const Editor& ed, const MapObject& object)
{
    if (object.tiles.isEmpty()) return QSizeF(qMax(1.0, object.w), qMax(1.0, object.h));
    for (const TileRef& tile : object.tiles) {
        const Tileset* ts = ed.tilesetAt(tile.tilesetIdx);
        if (!ts || !ts->contains(tile.tx, tile.ty)) continue;
        return QSizeF(qMax(1, object.stampW) * qMax(1, ts->tilewidth),
                      qMax(1, object.stampH) * qMax(1, ts->tileheight));
    }
    return QSizeF(qMax(1.0, object.w), qMax(1.0, object.h));
}

PropertiesPanel::PropertiesPanel(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    // LUDO 3.22.1 — o inspetor usa uma Tab Bar visível em vez do antigo
    // dropdown "Propriedades de". As abas compactas tornam a troca de contexto
    // mais rápida; em painéis estreitos o próprio QTabBar oferece scroll.
    m_tabs = new QTabBar(this);
    m_tabs->setObjectName(QStringLiteral("propertiesTabBar"));
    m_tabs->setDrawBase(true);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setToolTip(tr("Escolha o grupo de propriedades que deseja editar."));
    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(2);
    tabRow->addWidget(m_tabs, 1);
    m_pin = new QToolButton(this);
    m_pin->setText(QStringLiteral("📌"));
    m_pin->setCheckable(true);
    m_pin->setChecked(ed.session.inspectorPinned);
    m_pin->setToolTip(tr("Fixar contexto do Inspector"));
    m_pin->setAccessibleName(tr("Fixar contexto do Inspector"));
    tabRow->addWidget(m_pin);
    root->addLayout(tabRow);

    m_stack = new QStackedWidget(this);
    struct Section { QWidget* page; QString title; QString tip; };
    const QVector<Section> sections = {
        { scrolled(buildMapTab()),     tr("Mapa"),      tr("Propriedades gerais do mapa") },
        { scrolled(buildLayerTab()),   tr("Camada"),    tr("Propriedades da camada selecionada") },
        { scrolled(buildTilesetTab()), tr("Tileset"),   tr("Tilesets e fontes de tiles") },
        { scrolled(buildObjectTab()),  tr("Objeto"),    tr("Objeto selecionado no mapa") },
        { scrolled(buildRandomTab()),  tr("Aleatório"), tr("Tiles aleatórios, dispersão e posicionamento livre") },
        { scrolled(buildPatternsTab()),tr("Padrões"),  tr("Biblioteca de padrões reutilizáveis") }
    };
    for (const Section& sec : sections) {
        m_stack->addWidget(sec.page);
        const int tab = m_tabs->addTab(sec.title);
        m_tabs->setTabToolTip(tab, sec.tip);
    }
    root->addWidget(m_stack, 1);
    connect(m_tabs, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_pin, &QToolButton::toggled, this, [this](bool on) {
        ed.session.inspectorPinned = on;
        if (!on) syncContextTab();
    });

    connect(&ed, &Editor::layersChanged,    this, &PropertiesPanel::scheduleRefresh);
    connect(&ed, &Editor::selectionChanged, this, &PropertiesPanel::scheduleRefresh);
    connect(&ed, &Editor::tilesetsChanged,  this, &PropertiesPanel::scheduleRefresh);
    connect(&ed, &Editor::docsChanged,      this, &PropertiesPanel::scheduleRefresh);
    // mapChanged representa mudança de CONTEÚDO do mapa (pintura, tiles,
    // regiões etc.). Nenhum campo do Inspector depende de cada pincelada;
    // reconstruir todas as páginas aqui tornava o editor pesado. Alterações
    // estruturais/propriedades já chegam por layers/docs/selection/tilesets.
    connect(&ed, &Editor::patternsChanged,  this, &PropertiesPanel::refreshPatterns);
    refresh();
}

// ------------------------------------------------------------------- Mapa
QWidget* PropertiesPanel::buildMapTab()
{
    auto* w = new QWidget;
    auto* page = new QVBoxLayout(w);
    page->setContentsMargins(8, 8, 8, 8);
    page->setSpacing(2);
    auto* generalSection = new CollapsibleSection(tr("Tamanho e fundo"),
        QStringLiteral("Inspector/Map/GeneralExpanded"), true, w);
    auto* form = new QFormLayout(generalSection->contentWidget());
    page->addWidget(generalSection);

    m_mapW = new QSpinBox(w);  m_mapW->setRange(1, 4096);
    m_mapH = new QSpinBox(w);  m_mapH->setRange(1, 4096);
    m_mapTW = new QSpinBox(w); m_mapTW->setRange(1, 512);
    m_mapTH = new QSpinBox(w); m_mapTH->setRange(1, 512);
    m_mapW->setToolTip(tr("Largura do mapa em tiles da grade base."));
    m_mapH->setToolTip(tr("Altura do mapa em tiles da grade base."));

    form->addRow(tr("Largura (tiles)"), m_mapW);
    form->addRow(tr("Altura (tiles)"), m_mapH);
    form->addRow(tr("Tile W (px)"), m_mapTW);
    form->addRow(tr("Tile H (px)"), m_mapTH);

    m_mapBg = new QPushButton(tr("Cor de fundo…"), w);
    form->addRow(QString(), m_mapBg);

    auto* panoramaGroup = new CollapsibleSection(tr("Panorama"),
        QStringLiteral("Inspector/Map/PanoramaExpanded"), false, w);
    auto* panoramaForm = new QFormLayout(panoramaGroup->contentWidget());
    m_panoramaPath = new QLineEdit(panoramaGroup->contentWidget());
    m_panoramaPath->setReadOnly(true);
    m_panoramaPath->setPlaceholderText(tr("— nenhum panorama —"));
    m_panoramaChoose = new QPushButton(tr("Escolher…"), panoramaGroup->contentWidget());
    m_panoramaRemove = new QPushButton(tr("Remover"), panoramaGroup->contentWidget());
    auto* panoramaFileRow = new QHBoxLayout;
    panoramaFileRow->addWidget(m_panoramaPath, 1);
    panoramaFileRow->addWidget(m_panoramaChoose);
    panoramaFileRow->addWidget(m_panoramaRemove);
    panoramaForm->addRow(tr("Imagem"), panoramaFileRow);
    m_panoramaVisible = new QCheckBox(tr("Mostrar panorama"), panoramaGroup->contentWidget());
    m_panoramaFit = new QCheckBox(tr("Ajustar ao tamanho do mapa"), panoramaGroup->contentWidget());
    m_panoramaRepeat = new QCheckBox(tr("Repetir imagem (tile)"), panoramaGroup->contentWidget());
    m_panoramaOpacity = new QSpinBox(panoramaGroup->contentWidget());
    m_panoramaOpacity->setRange(0, 255);
    panoramaForm->addRow(m_panoramaVisible);
    panoramaForm->addRow(tr("Opacidade"), m_panoramaOpacity);
    panoramaForm->addRow(m_panoramaFit);
    panoramaForm->addRow(m_panoramaRepeat);
    page->addWidget(panoramaGroup);

    auto* reflectionGroup = new CollapsibleSection(tr("Reflexos"),
        QStringLiteral("Inspector/Map/ReflectionsExpanded"), false, w);
    m_reflectionGroup = reflectionGroup;
    auto* reflectionForm = new QFormLayout(reflectionGroup->contentWidget());
    m_reflectionEnvironmentEnabled = new QCheckBox(tr("Ativar céu / ambiente refletido"), reflectionGroup->contentWidget());
    m_reflectionEnvironmentEnabled->setToolTip(tr("Configuração exclusiva deste mapa. Outros mapas podem usar outra imagem ou deixar o céu desativado."));

    m_reflectionEnvironmentPath = new QLineEdit(reflectionGroup->contentWidget());
    m_reflectionEnvironmentPath->setReadOnly(true);
    m_reflectionEnvironmentPath->setPlaceholderText(tr("— nenhuma imagem de céu / ambiente —"));
    m_reflectionEnvironmentChoose = new QPushButton(tr("Escolher…"), reflectionGroup->contentWidget());
    m_reflectionEnvironmentRemove = new QPushButton(tr("Remover"), reflectionGroup->contentWidget());
    auto* reflectionImageRow = new QHBoxLayout;
    reflectionImageRow->addWidget(m_reflectionEnvironmentPath, 1);
    reflectionImageRow->addWidget(m_reflectionEnvironmentChoose);
    reflectionImageRow->addWidget(m_reflectionEnvironmentRemove);

    m_reflectionEnvironmentOpacity = new QSpinBox(reflectionGroup->contentWidget());
    m_reflectionEnvironmentOpacity->setRange(0, 255);
    m_reflectionEnvironmentOpacity->setToolTip(tr("Intensidade da imagem do céu/ambiente dentro das superfícies refletivas deste mapa."));

    m_reflectionEnvironmentFit = new QComboBox(reflectionGroup->contentWidget());
    m_reflectionEnvironmentFit->addItem(tr("Cobrir sem deformar"), QStringLiteral("cover"));
    m_reflectionEnvironmentFit->addItem(tr("Esticar para preencher"), QStringLiteral("stretch"));

    m_reflectionEnvironmentFlipY = new QCheckBox(tr("Espelhar verticalmente"), reflectionGroup->contentWidget());
    m_reflectionEnvironmentFlipY->setToolTip(tr("Recomendado para céu e cenário: a imagem aparece invertida como um reflexo."));

    m_reflectionEnvironmentBlend = new QComboBox(reflectionGroup->contentWidget());
    m_reflectionEnvironmentBlend->addItem(tr("Normal"), QStringLiteral("normal"));
    m_reflectionEnvironmentBlend->addItem(tr("Tela / Screen"), QStringLiteral("screen"));
    m_reflectionEnvironmentBlend->addItem(tr("Aditivo"), QStringLiteral("add"));

    m_reflectionOffsetY = new QSpinBox(reflectionGroup->contentWidget());
    m_reflectionOffsetY->setRange(-96, 96);
    m_reflectionOffsetY->setSuffix(tr(" px"));
    m_reflectionOffsetY->setToolTip(tr("Move o reflexo para cima ou para baixo para alinhar os pés/objetos com o chão. Use quando o sprite não encostar exatamente na base visual."));

    reflectionForm->addRow(m_reflectionEnvironmentEnabled);
    reflectionForm->addRow(tr("Imagem"), reflectionImageRow);
    reflectionForm->addRow(tr("Opacidade"), m_reflectionEnvironmentOpacity);
    reflectionForm->addRow(tr("Ajuste"), m_reflectionEnvironmentFit);
    reflectionForm->addRow(m_reflectionEnvironmentFlipY);
    reflectionForm->addRow(tr("Mistura"), m_reflectionEnvironmentBlend);
    reflectionForm->addRow(tr("Ajuste vertical dos reflexos"), m_reflectionOffsetY);
    page->addWidget(reflectionGroup);

    auto* infoSection = new CollapsibleSection(tr("Informações"),
        QStringLiteral("Inspector/Map/InfoExpanded"), false, w);
    auto* infoLayout = new QVBoxLayout(infoSection->contentWidget());
    m_mapInfoLabel = new QLabel(infoSection->contentWidget());
    m_mapInfoLabel->setWordWrap(true);
    m_mapInfoLabel->setProperty("uiRole", QStringLiteral("hint"));
    infoLayout->addWidget(m_mapInfoLabel);
    page->addWidget(infoSection);
    page->addStretch(1);

    auto apply = [this] {
        if (m_updating) return;
        MapDoc* d = ed.doc();
        if (!d) return;
        const DocSnapshot before = ed.snapshotDoc();
        d->map.width = m_mapW->value();
        d->map.height = m_mapH->value();
        if (ed.rpgMakerEngine == core::RpgMakerEngine::MV) {
            d->map.tileWidth = 48;
            d->map.tileHeight = 48;
        } else {
            d->map.tileWidth = m_mapTW->value();
            d->map.tileHeight = m_mapTH->value();
        }
        ed.resyncLayerGrids();
        ed.pushDocHistory(before, tr("Alterar propriedades do mapa"));
        emit ed.mapChanged();
        refreshMap();
    };
    connect(m_mapW, &QSpinBox::editingFinished, this, apply);
    connect(m_mapH, &QSpinBox::editingFinished, this, apply);
    connect(m_mapTW, &QSpinBox::editingFinished, this, apply);
    connect(m_mapTH, &QSpinBox::editingFinished, this, apply);
    connect(m_mapBg, &QPushButton::clicked, this, [this] {
        MapDoc* d = ed.doc();
        if (!d) return;
        const QColor c = QColorDialog::getColor(d->map.background, this, tr("Cor de fundo do mapa"));
        if (!c.isValid()) return;
        const DocSnapshot before = ed.snapshotDoc();
        d->map.background = c;
        ed.pushDocHistory(before, tr("Alterar cor de fundo do mapa"));
        emit ed.mapChanged();
        refreshMap();
    });
    connect(m_panoramaChoose, &QPushButton::clicked, this, [this] {
        MapDoc* d = ed.doc();
        if (!d) return;
        const QString path = AssetBrowserDialog::chooseImage(ed, this, QStringLiteral("Pictures"));
        if (path.isEmpty()) return;
        QImage image(path);
        if (image.isNull()) {
            emit statusMessage(tr("Não foi possível carregar o panorama selecionado."));
            return;
        }
        const DocSnapshot before = ed.snapshotDoc();
        d->map.panoramaPath = ed.projectRelativePath(path);
        d->map.panorama = image;
        d->map.panoramaVisible = true;
        ed.pushDocHistory(before, tr("Definir panorama do mapa"));
        emit ed.mapChanged();
        refreshMap();
    });
    connect(m_panoramaRemove, &QPushButton::clicked, this, [this] {
        MapDoc* d = ed.doc();
        if (!d || (d->map.panorama.isNull() && d->map.panoramaPath.isEmpty())) return;
        const DocSnapshot before = ed.snapshotDoc();
        d->map.panoramaPath.clear();
        d->map.panorama = QImage();
        d->map.panoramaVisible = false;
        ed.pushDocHistory(before, tr("Remover panorama do mapa"));
        emit ed.mapChanged();
        refreshMap();
    });
    auto changePanorama = [this](const QString& label, std::function<void(MapInfo&)> change) {
        if (m_updating) return;
        MapDoc* d = ed.doc();
        if (!d) return;
        const DocSnapshot before = ed.snapshotDoc();
        change(d->map);
        ed.pushDocHistory(before, label);
        emit ed.mapChanged();
    };
    connect(m_panoramaVisible, &QCheckBox::toggled, this, [changePanorama](bool v) {
        changePanorama(QObject::tr("Visibilidade do panorama"), [v](MapInfo& m) { m.panoramaVisible = v; });
    });
    connect(m_panoramaOpacity, &QSpinBox::editingFinished, this, [this, changePanorama] {
        const int v = m_panoramaOpacity->value();
        changePanorama(tr("Opacidade do panorama"), [v](MapInfo& m) { m.panoramaOpacity = v; });
    });
    connect(m_panoramaFit, &QCheckBox::toggled, this, [changePanorama](bool v) {
        changePanorama(QObject::tr("Ajuste do panorama"), [v](MapInfo& m) { m.panoramaFit = v; });
    });
    connect(m_panoramaRepeat, &QCheckBox::toggled, this, [changePanorama](bool v) {
        changePanorama(QObject::tr("Repetição do panorama"), [v](MapInfo& m) { m.panoramaRepeat = v; });
    });

    auto changeReflectionEnvironment = [this](const QString& label, std::function<void(QJsonObject&)> change) {
        if (m_updating) return;
        MapDoc* d = ed.doc();
        if (!d) return;
        const DocSnapshot before = ed.snapshotDoc();
        QJsonObject environment = d->reflectionSettings.value(QStringLiteral("environment")).toObject();
        change(environment);
        d->reflectionSettings.insert(QStringLiteral("environment"), environment);
        ed.pushDocHistory(before, label);
        emit ed.mapChanged();
    };

    connect(m_reflectionEnvironmentChoose, &QPushButton::clicked, this, [this] {
        MapDoc* d = ed.doc();
        if (!d) return;
        const QJsonObject currentEnvironment = d->reflectionSettings.value(QStringLiteral("environment")).toObject();
        const QString currentPath = currentEnvironment.value(QStringLiteral("sourcePath")).toString().trimmed();
        const QString path = UniversalAssetPickerDialog::chooseOne(
            ed,this,QStringLiteral("reflection.environment"),QStringLiteral("image"),
            tr("Escolher céu / ambiente para o reflexo"));
        if (path.isEmpty()) return;

        const DocSnapshot before = ed.snapshotDoc();
        QJsonObject environment = currentEnvironment;
        environment.insert(QStringLiteral("enabled"), true);
        environment.insert(QStringLiteral("sourcePath"), QDir::cleanPath(path));
        if (!environment.contains(QStringLiteral("opacity"))) environment.insert(QStringLiteral("opacity"), 112);
        if (!environment.contains(QStringLiteral("fit"))) environment.insert(QStringLiteral("fit"), QStringLiteral("cover"));
        if (!environment.contains(QStringLiteral("flipY"))) environment.insert(QStringLiteral("flipY"), true);
        if (!environment.contains(QStringLiteral("blendMode"))) environment.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
        d->reflectionSettings.insert(QStringLiteral("environment"), environment);
        ed.pushDocHistory(before, tr("Definir céu / ambiente refletido"));
        emit ed.mapChanged();
        refreshMap();
    });

    connect(m_reflectionEnvironmentRemove, &QPushButton::clicked, this, [this] {
        MapDoc* d = ed.doc();
        if (!d) return;
        const QJsonObject currentEnvironment = d->reflectionSettings.value(QStringLiteral("environment")).toObject();
        if (currentEnvironment.value(QStringLiteral("sourcePath")).toString().trimmed().isEmpty() &&
            !currentEnvironment.value(QStringLiteral("enabled")).toBool(false)) return;

        const DocSnapshot before = ed.snapshotDoc();
        QJsonObject environment = currentEnvironment;
        environment.insert(QStringLiteral("enabled"), false);
        environment.insert(QStringLiteral("sourcePath"), QString());
        d->reflectionSettings.insert(QStringLiteral("environment"), environment);
        ed.pushDocHistory(before, tr("Remover céu / ambiente refletido"));
        emit ed.mapChanged();
        refreshMap();
    });

    connect(m_reflectionEnvironmentEnabled, &QCheckBox::toggled, this, [changeReflectionEnvironment](bool value) {
        changeReflectionEnvironment(QObject::tr("Ativar céu / ambiente refletido"),
                                    [value](QJsonObject& environment) { environment.insert(QStringLiteral("enabled"), value); });
    });
    connect(m_reflectionEnvironmentOpacity, &QSpinBox::editingFinished, this, [this, changeReflectionEnvironment] {
        const int value = m_reflectionEnvironmentOpacity->value();
        changeReflectionEnvironment(tr("Opacidade do céu / ambiente refletido"),
                                    [value](QJsonObject& environment) { environment.insert(QStringLiteral("opacity"), value); });
    });
    connect(m_reflectionEnvironmentFit, &QComboBox::currentIndexChanged, this, [this, changeReflectionEnvironment](int) {
        if (m_updating) return;
        const QString value = m_reflectionEnvironmentFit->currentData().toString();
        changeReflectionEnvironment(tr("Ajuste do céu / ambiente refletido"),
                                    [value](QJsonObject& environment) { environment.insert(QStringLiteral("fit"), value); });
    });
    connect(m_reflectionEnvironmentFlipY, &QCheckBox::toggled, this, [changeReflectionEnvironment](bool value) {
        changeReflectionEnvironment(QObject::tr("Espelhamento do céu / ambiente refletido"),
                                    [value](QJsonObject& environment) { environment.insert(QStringLiteral("flipY"), value); });
    });
    connect(m_reflectionEnvironmentBlend, &QComboBox::currentIndexChanged, this, [this, changeReflectionEnvironment](int) {
        if (m_updating) return;
        const QString value = m_reflectionEnvironmentBlend->currentData().toString();
        changeReflectionEnvironment(tr("Mistura do céu / ambiente refletido"),
                                    [value](QJsonObject& environment) { environment.insert(QStringLiteral("blendMode"), value); });
    });
    connect(m_reflectionOffsetY, &QSpinBox::editingFinished, this, [this] {
        if (m_updating) return;
        MapDoc* d = ed.doc();
        if (!d) return;
        const int value = qBound(-96, m_reflectionOffsetY->value(), 96);
        if (d->reflectionSettings.value(QStringLiteral("offsetY")).toInt(0) == value) return;
        const DocSnapshot before = ed.snapshotDoc();
        d->reflectionSettings.insert(QStringLiteral("offsetY"), value);
        ed.pushDocHistory(before, tr("Ajustar posição dos reflexos"));
        emit ed.mapChanged();
    });
    return w;
}

void PropertiesPanel::refreshMap()
{
    const MapDoc* d = ed.doc();
    if (!d) return;
    m_mapW->setValue(d->map.width);
    m_mapH->setValue(d->map.height);
    m_mapTW->setValue(d->map.tileWidth);
    m_mapTH->setValue(d->map.tileHeight);
    const bool fixedMvGrid = ed.rpgMakerEngine == core::RpgMakerEngine::MV;
    m_mapTW->setEnabled(!fixedMvGrid);
    m_mapTH->setEnabled(!fixedMvGrid);
    const QString gridTip = fixedMvGrid
        ? tr("O RPG Maker MV usa grade nativa fixa de 48×48 px.")
        : QString();
    m_mapTW->setToolTip(gridTip);
    m_mapTH->setToolTip(gridTip);
    m_mapBg->setStyleSheet(QStringLiteral("background:%1").arg(d->map.background.name()));
    const bool hasPanorama = !d->map.panorama.isNull();
    m_panoramaPath->setText(d->map.panoramaPath);
    m_panoramaVisible->setChecked(d->map.panoramaVisible);
    m_panoramaVisible->setEnabled(hasPanorama);
    m_panoramaOpacity->setValue(d->map.panoramaOpacity);
    m_panoramaOpacity->setEnabled(hasPanorama);
    m_panoramaFit->setChecked(d->map.panoramaFit);
    m_panoramaFit->setEnabled(hasPanorama);
    m_panoramaRepeat->setChecked(d->map.panoramaRepeat);
    m_panoramaRepeat->setEnabled(hasPanorama);
    m_panoramaRemove->setEnabled(hasPanorama || !d->map.panoramaPath.isEmpty());

    const bool supportsExtraReflection = ed.rpgMakerEngine == core::RpgMakerEngine::MZ;
    if (m_reflectionGroup) m_reflectionGroup->setVisible(supportsExtraReflection);

    const QJsonObject reflectionEnvironment = d->reflectionSettings.value(QStringLiteral("environment")).toObject();
    const bool environmentEnabled = reflectionEnvironment.value(QStringLiteral("enabled")).toBool(false);
    const QString environmentPath = reflectionEnvironment.value(QStringLiteral("sourcePath")).toString();
    m_reflectionEnvironmentEnabled->setChecked(environmentEnabled);
    m_reflectionEnvironmentPath->setText(environmentPath);
    m_reflectionEnvironmentOpacity->setValue(qBound(0, reflectionEnvironment.value(QStringLiteral("opacity")).toInt(112), 255));
    const QString environmentFit = reflectionEnvironment.value(QStringLiteral("fit")).toString(QStringLiteral("cover"));
    m_reflectionEnvironmentFit->setCurrentIndex(qMax(0, m_reflectionEnvironmentFit->findData(environmentFit)));
    m_reflectionEnvironmentFlipY->setChecked(reflectionEnvironment.value(QStringLiteral("flipY")).toBool(true));
    const QString environmentBlend = reflectionEnvironment.value(QStringLiteral("blendMode")).toString(QStringLiteral("normal"));
    m_reflectionEnvironmentBlend->setCurrentIndex(qMax(0, m_reflectionEnvironmentBlend->findData(environmentBlend)));

    m_reflectionOffsetY->setValue(qBound(-96, d->reflectionSettings.value(QStringLiteral("offsetY")).toInt(0), 96));

    const bool hasEnvironmentImage = !environmentPath.trimmed().isEmpty();
    m_reflectionEnvironmentRemove->setEnabled(hasEnvironmentImage);
    m_reflectionEnvironmentOpacity->setEnabled(environmentEnabled);
    m_reflectionEnvironmentFit->setEnabled(environmentEnabled);
    m_reflectionEnvironmentFlipY->setEnabled(environmentEnabled);
    m_reflectionEnvironmentBlend->setEnabled(environmentEnabled);

    const QString engineShort = core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper();
    m_mapInfoLabel->setText(tr("Tamanho em pixels: %1 × %2\nGrade: %8 × %9 px%10\nCamadas: %3\nTilesets: %4\nRegiões %5: %6 célula(s)%7")
                                .arg(d->map.pixelWidth()).arg(d->map.pixelHeight())
                                .arg(ed.flatLayers().size()).arg(ed.tilesets.size())
                                .arg(engineShort)
                                .arg(d->rpgMakerRegions.size())
                                .arg(d->rpgMakerRegionsAuthored ? QString() : tr(" (preservando %1)").arg(engineShort))
                                .arg(d->map.tileWidth).arg(d->map.tileHeight)
                                .arg(fixedMvGrid ? tr(" (fixa no MV)") : QString()));
}

// ----------------------------------------------------------------- Camada
QWidget* PropertiesPanel::buildLayerTab()
{
    auto* w = new QWidget;
    auto* page = new QVBoxLayout(w);
    page->setContentsMargins(8, 8, 8, 8);
    page->setSpacing(2);
    auto* generalSection = new CollapsibleSection(tr("Geral e aparência"),
        QStringLiteral("Inspector/Layer/GeneralExpanded"), true, w);
    auto* form = new QFormLayout(generalSection->contentWidget());

    m_layerName = new QLineEdit(w);
    m_layerType = new QLabel(w);
    m_layerOpacity = new QSlider(Qt::Horizontal, w);
    m_layerOpacity->setRange(0, 100);
    m_layerOpacityLabel = new QLabel(w);
    m_layerBlend = new QComboBox(w);
    m_layerBlend->addItem(tr("Normal"), QStringLiteral("source-over"));
    m_layerBlend->addItem(tr("Multiplicar — escurece"), QStringLiteral("multiply"));
    m_layerBlend->addItem(tr("Tela — clareia"), QStringLiteral("screen"));
    m_layerBlend->addItem(tr("Sobrepor — aumenta contraste"), QStringLiteral("overlay"));
    m_layerBlend->addItem(tr("Escurecer"), QStringLiteral("darken"));
    m_layerBlend->addItem(tr("Clarear"), QStringLiteral("lighten"));
    m_layerBlend->addItem(tr("Diferença"), QStringLiteral("difference"));
    m_layerBlend->addItem(tr("Exclusão"), QStringLiteral("exclusion"));
    m_layerBlend->addItem(tr("Luz forte"), QStringLiteral("hard-light"));
    m_layerBlend->addItem(tr("Luz suave"), QStringLiteral("soft-light"));
    m_layerBlend->addItem(tr("Clarear cores"), QStringLiteral("color-dodge"));
    m_layerBlend->addItem(tr("Escurecer cores"), QStringLiteral("color-burn"));
    m_layerOffX = new QSpinBox(w); m_layerOffX->setRange(-8192, 8192);
    m_layerOffY = new QSpinBox(w); m_layerOffY->setRange(-8192, 8192);
    m_layerVisible = new QCheckBox(tr("Visível"), w);
    m_layerVisible->setToolTip(tr("Mostra ou esconde a camada sem apagar seu conteúdo."));
    m_layerLocked = new QCheckBox(tr("Impedir alterações"), w);
    m_layerLocked->setToolTip(tr("Protege a camada contra pintura, movimentação e outras alterações acidentais."));
    m_layerMask = new QCheckBox(tr("Usar esta camada como recorte"), w);
    m_layerMask->setToolTip(tr("As camadas colocadas dentro dela só aparecem nas partes preenchidas desta camada. Útil para manter pinturas e texturas dentro de uma forma."));
    m_layerMaskBase = new QCheckBox(tr("Mostrar também a camada de recorte"), w);
    m_layerMaskBase->setToolTip(tr("Ligado: a própria camada usada como recorte continua visível. Desligado: ela serve apenas para limitar as camadas que estão dentro dela."));
    m_layerAbove = new QCheckBox(tr("★ Mostrar na frente do jogador"), w);
    m_layerAbove->setToolTip(tr("Faz esta camada aparecer na frente do personagem quando o mapa é exibido no jogo. Útil para telhados, copas de árvores e partes altas de objetos."));
    m_layerAlphaLock = new QCheckBox(tr("Proteger áreas vazias"), w);
    m_layerAlphaLock->setToolTip(tr("Pinte somente onde já existe conteúdo. As partes vazias ficam protegidas para o pincel não escapar do desenho ou dos tiles."));
    m_layerFiltersButton = new QPushButton(icons::get(QStringLiteral("layer-filters")), tr("Efeitos da camada…"), w);
    m_maskFiltersButton = new QPushButton(icons::get(QStringLiteral("mask-filters")), tr("Efeitos da máscara…"), w);

    form->addRow(tr("Nome"), m_layerName);
    form->addRow(tr("Tipo"), m_layerType);
    auto* opRow = new QHBoxLayout;
    opRow->addWidget(m_layerOpacity, 1);
    opRow->addWidget(m_layerOpacityLabel);
    form->addRow(tr("Opacidade"), opRow);
    form->addRow(tr("Como mistura com as camadas abaixo"), m_layerBlend);
    m_layerBlend->setToolTip(tr("Escolha como as cores desta camada se combinam com o que está por baixo. Normal mantém as cores originais; Multiplicar é útil para sombras e sujeira; Tela é útil para luz."));
    form->addRow(tr("Posição X"), m_layerOffX);
    form->addRow(tr("Posição Y"), m_layerOffY);
    m_layerOffX->setToolTip(tr("Move a camada para a esquerda ou para a direita, em pixels."));
    m_layerOffY->setToolTip(tr("Move a camada para cima ou para baixo, em pixels."));
    form->addRow(m_layerVisible);
    form->addRow(m_layerLocked);
    form->addRow(m_layerMask);
    form->addRow(m_layerMaskBase);
    form->addRow(m_layerAbove);
    form->addRow(m_layerAlphaLock);
    auto* filterButtons = new QHBoxLayout;
    filterButtons->addWidget(m_layerFiltersButton);
    filterButtons->addWidget(m_maskFiltersButton);
    form->addRow(tr("Efeitos"), filterButtons);
    page->addWidget(generalSection);

    // Image Layer — transformacoes ficam no mesmo Inspector da camada para
    // evitar uma ferramenta/modal isolada. Offset X/Y acima continua sendo a
    // posicao; aqui ficam escala, rotacao, flip, sampling e participacao no export.
    m_imageTransformGroup = new CollapsibleSection(tr("Transformar"),
        QStringLiteral("Inspector/Layer/TransformExpanded"), true, w);
    auto* imageForm = new QFormLayout(m_imageTransformGroup->contentWidget());
    m_imageSource = new QLabel(m_imageTransformGroup->contentWidget());
    m_imageSource->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_imageSource->setWordWrap(true);
    m_imageScaleX = new QDoubleSpinBox(m_imageTransformGroup->contentWidget());
    m_imageScaleY = new QDoubleSpinBox(m_imageTransformGroup->contentWidget());
    for (QDoubleSpinBox* spin : {m_imageScaleX, m_imageScaleY}) {
        spin->setRange(1.0, 10000.0);
        spin->setDecimals(2);
        spin->setSingleStep(5.0);
        spin->setSuffix(QStringLiteral(" %"));
    }
    m_imageRotation = new QDoubleSpinBox(m_imageTransformGroup->contentWidget());
    m_imageRotation->setRange(-3600.0, 3600.0);
    m_imageRotation->setDecimals(2);
    m_imageRotation->setSingleStep(1.0);
    m_imageRotation->setSuffix(QStringLiteral("°"));
    m_imageFilter = new QComboBox(m_imageTransformGroup->contentWidget());
    m_imageFilter->addItem(tr("Pixels nítidos — melhor para pixel art"), QStringLiteral("nearest"));
    m_imageFilter->addItem(tr("Suave — melhor para imagens em alta resolução"), QStringLiteral("bilinear"));
    m_imageFlipX = new QCheckBox(tr("Inverter horizontalmente"), m_imageTransformGroup->contentWidget());
    m_imageFlipY = new QCheckBox(tr("Inverter verticalmente"), m_imageTransformGroup->contentWidget());
    m_imageRepeatX = new QCheckBox(tr("Repetir horizontalmente"), m_imageTransformGroup->contentWidget());
    m_imageRepeatY = new QCheckBox(tr("Repetir verticalmente"), m_imageTransformGroup->contentWidget());
    m_imageRepeatX->setToolTip(tr("Repete a mesma imagem para a esquerda e para a direita. Útil para paredes, pisos, fundos e texturas contínuas."));
    m_imageRepeatY->setToolTip(tr("Repete a mesma imagem para cima e para baixo. Útil para paredes, pisos, fundos e texturas contínuas."));
    m_imageExport = new QCheckBox(tr("Exportar esta imagem no jogo"), m_imageTransformGroup->contentWidget());
    m_imageReplace = new QPushButton(tr("Substituir imagem…"), m_imageTransformGroup->contentWidget());
    m_imageReset = new QPushButton(tr("Restaurar padrão"), m_imageTransformGroup->contentWidget());
    imageForm->addRow(tr("Arquivo"), m_imageSource);
    imageForm->addRow(tr("Escala X"), m_imageScaleX);
    imageForm->addRow(tr("Escala Y"), m_imageScaleY);
    imageForm->addRow(tr("Rotação"), m_imageRotation);
    imageForm->addRow(tr("Ao redimensionar"), m_imageFilter);
    m_imageFilter->setToolTip(tr("Pixels nítidos evita borrões em pixel art. Suave reduz serrilhados em fotos e imagens de alta resolução."));
    imageForm->addRow(m_imageFlipX);
    imageForm->addRow(m_imageFlipY);
    imageForm->addRow(m_imageRepeatX);
    imageForm->addRow(m_imageRepeatY);
    imageForm->addRow(m_imageExport);
    auto* imageButtons = new QHBoxLayout;
    imageButtons->addWidget(m_imageReplace);
    imageButtons->addWidget(m_imageReset);
    imageForm->addRow(imageButtons);
    page->addWidget(m_imageTransformGroup);

    m_parallaxGroup=new CollapsibleSection(tr("Parallax e profundidade"),
        QStringLiteral("Inspector/Layer/ParallaxExpanded"),false,w);
    m_parallaxForm=new QFormLayout(m_parallaxGroup->contentWidget());
    m_parallaxEnabled=new QCheckBox(tr("Usar esta camada como cenário com profundidade"),m_parallaxGroup->contentWidget());
    m_parallaxEnabled->setToolTip(tr("Separa esta camada do cenário comum para que ela possa reagir à câmera, mover sozinha, repetir, animar ou receber efeitos no jogo."));
    m_parallaxFactorX=new QDoubleSpinBox(m_parallaxGroup->contentWidget());m_parallaxFactorY=new QDoubleSpinBox(m_parallaxGroup->contentWidget());
    m_parallaxRepeatX=new QCheckBox(tr("Repetir para os lados"),m_parallaxGroup->contentWidget());
    m_parallaxRepeatY=new QCheckBox(tr("Repetir para cima e para baixo"),m_parallaxGroup->contentWidget());
    m_parallaxForm->addRow(m_parallaxEnabled);
    m_parallaxForm->addRow(tr("Movimento com a câmera X"),m_parallaxFactorX);
    m_parallaxForm->addRow(tr("Movimento com a câmera Y"),m_parallaxFactorY);
    m_parallaxForm->addRow(m_parallaxRepeatX);m_parallaxForm->addRow(m_parallaxRepeatY);
    page->addWidget(m_parallaxGroup);

    m_parallaxMotionGroup=new CollapsibleSection(tr("Movimento"),
        QStringLiteral("Inspector/Layer/MotionExpanded"),false,w);
    m_parallaxMotionForm=new QFormLayout(m_parallaxMotionGroup->contentWidget());
    m_parallaxMotionPreset=new QComboBox(m_parallaxMotionGroup->contentWidget());
    m_parallaxMotionPreset->addItem(tr("Personalizado"),QStringLiteral("custom"));
    m_parallaxMotionPreset->addItem(tr("Nuvem lenta"),QStringLiteral("cloudSlow"));
    m_parallaxMotionPreset->addItem(tr("Nuvem rápida"),QStringLiteral("cloudFast"));
    m_parallaxMotionPreset->addItem(tr("Névoa flutuante"),QStringLiteral("mist"));
    m_parallaxMotionPreset->addItem(tr("Flutuação suave"),QStringLiteral("float"));
    m_parallaxSpeedX=new QDoubleSpinBox(m_parallaxMotionGroup->contentWidget());m_parallaxSpeedY=new QDoubleSpinBox(m_parallaxMotionGroup->contentWidget());
    m_parallaxOscillationX=new QDoubleSpinBox(m_parallaxMotionGroup->contentWidget());m_parallaxOscillationY=new QDoubleSpinBox(m_parallaxMotionGroup->contentWidget());
    m_parallaxOscillationSpeed=new QDoubleSpinBox(m_parallaxMotionGroup->contentWidget());
    for(auto* spin:{m_parallaxFactorX,m_parallaxFactorY}){spin->setRange(-4.0,4.0);spin->setDecimals(2);spin->setSingleStep(.1);}
    for(auto* spin:{m_parallaxSpeedX,m_parallaxSpeedY}){spin->setRange(-2000,2000);spin->setDecimals(1);spin->setSuffix(tr(" px/s"));}
    for(auto* spin:{m_parallaxOscillationX,m_parallaxOscillationY}){spin->setRange(0,4096);spin->setDecimals(1);spin->setSuffix(tr(" px"));}
    m_parallaxOscillationSpeed->setRange(0,20);m_parallaxOscillationSpeed->setDecimals(2);m_parallaxOscillationSpeed->setSuffix(tr(" ciclo/s"));
    m_parallaxSmoothMotion=new QCheckBox(tr("Movimento suave"),m_parallaxMotionGroup->contentWidget());
    m_parallaxSmoothMotion->setToolTip(tr("Evita pequenos saltos em movimentos lentos, como nuvens e névoa. Desative somente se quiser preservar o movimento rígido de pixel art."));
    m_parallaxMotionForm->addRow(tr("Movimento pronto"),m_parallaxMotionPreset);
    m_parallaxMotionForm->addRow(tr("Movimento automático X"),m_parallaxSpeedX);
    m_parallaxMotionForm->addRow(tr("Movimento automático Y"),m_parallaxSpeedY);
    m_parallaxMotionForm->addRow(tr("Balanço horizontal"),m_parallaxOscillationX);
    m_parallaxMotionForm->addRow(tr("Balanço vertical"),m_parallaxOscillationY);
    m_parallaxMotionForm->addRow(tr("Velocidade do balanço"),m_parallaxOscillationSpeed);
    m_parallaxMotionForm->addRow(m_parallaxSmoothMotion);
    page->addWidget(m_parallaxMotionGroup);

    m_parallaxAnimationGroup=new CollapsibleSection(tr("Animação"),
        QStringLiteral("Inspector/Layer/AnimationExpanded"),false,w);
    m_parallaxAnimationForm=new QFormLayout(m_parallaxAnimationGroup->contentWidget());
    m_parallaxAnimationEnabled=new QCheckBox(tr("A imagem possui vários quadros"),m_parallaxAnimationGroup->contentWidget());
    m_parallaxAnimationColumns=new QSpinBox(m_parallaxAnimationGroup->contentWidget());m_parallaxAnimationColumns->setRange(1,64);
    m_parallaxAnimationRows=new QSpinBox(m_parallaxAnimationGroup->contentWidget());m_parallaxAnimationRows->setRange(1,64);
    m_parallaxAnimationFrames=new QSpinBox(m_parallaxAnimationGroup->contentWidget());m_parallaxAnimationFrames->setRange(1,4096);
    m_parallaxAnimationFps=new QDoubleSpinBox(m_parallaxAnimationGroup->contentWidget());m_parallaxAnimationFps->setRange(.1,60);m_parallaxAnimationFps->setDecimals(1);m_parallaxAnimationFps->setSuffix(tr(" quadros/s"));
    m_parallaxAnimationPingPong=new QCheckBox(tr("Ir e voltar suavemente"),m_parallaxAnimationGroup->contentWidget());
    m_parallaxAnimationForm->addRow(m_parallaxAnimationEnabled);
    m_parallaxAnimationForm->addRow(tr("Colunas da imagem"),m_parallaxAnimationColumns);
    m_parallaxAnimationForm->addRow(tr("Linhas da imagem"),m_parallaxAnimationRows);
    m_parallaxAnimationForm->addRow(tr("Quantidade de quadros"),m_parallaxAnimationFrames);
    m_parallaxAnimationForm->addRow(tr("Velocidade da animação"),m_parallaxAnimationFps);
    m_parallaxAnimationForm->addRow(m_parallaxAnimationPingPong);
    page->addWidget(m_parallaxAnimationGroup);

    m_parallaxEffectsGroup=new CollapsibleSection(tr("Efeitos"),
        QStringLiteral("Inspector/Layer/EffectsExpanded"),false,w);
    m_parallaxEffectsForm=new QFormLayout(m_parallaxEffectsGroup->contentWidget());
    m_parallaxEffectPreset=new QComboBox(m_parallaxEffectsGroup->contentWidget());
    m_parallaxEffectPreset->addItem(tr("Sem efeito"),QStringLiteral("none"));
    m_parallaxEffectPreset->addItem(tr("Debaixo d’água"),QStringLiteral("underwater"));
    m_parallaxEffectPreset->addItem(tr("Névoa suave"),QStringLiteral("mist"));
    m_parallaxEffectPreset->addItem(tr("Sonho"),QStringLiteral("dream"));
    m_parallaxEffectPreset->addItem(tr("Calor"),QStringLiteral("heat"));
    m_parallaxEffectPreset->addItem(tr("Fantasma"),QStringLiteral("ghost"));
    m_parallaxEffectPreset->addItem(tr("Noite"),QStringLiteral("night"));
    m_parallaxEffectStrength=new QDoubleSpinBox(m_parallaxEffectsGroup->contentWidget());m_parallaxEffectStrength->setRange(0,100);m_parallaxEffectStrength->setDecimals(0);m_parallaxEffectStrength->setSuffix(QStringLiteral("%"));
    m_parallaxEffectSpeed=new QDoubleSpinBox(m_parallaxEffectsGroup->contentWidget());m_parallaxEffectSpeed->setRange(0,10);m_parallaxEffectSpeed->setDecimals(2);m_parallaxEffectSpeed->setSuffix(QStringLiteral("x"));
    m_parallaxEffectsForm->addRow(tr("Clima visual"),m_parallaxEffectPreset);
    m_parallaxEffectsForm->addRow(tr("Intensidade do efeito"),m_parallaxEffectStrength);
    m_parallaxEffectsForm->addRow(tr("Velocidade do efeito"),m_parallaxEffectSpeed);
    page->addWidget(m_parallaxEffectsGroup);

    m_reflectionLayerGroup=new CollapsibleSection(tr("Reflexo"),
        QStringLiteral("Inspector/Layer/ReflectionExpanded"),false,w);
    auto* reflectionLayerForm=new QFormLayout(m_reflectionLayerGroup->contentWidget());
    m_reflectionLayerPreset=new QComboBox(m_reflectionLayerGroup->contentWidget());
    m_reflectionLayerPreset->addItem(tr("Água calma"),QStringLiteral("still"));
    m_reflectionLayerPreset->addItem(tr("Poça"),QStringLiteral("puddle"));
    m_reflectionLayerPreset->addItem(tr("Espelho suave"),QStringLiteral("mirror"));
    m_reflectionLayerOpacity=new QSpinBox(m_reflectionLayerGroup->contentWidget());m_reflectionLayerOpacity->setRange(0,100);m_reflectionLayerOpacity->setSuffix("%");
    m_reflectionLayerBlur=new QSpinBox(m_reflectionLayerGroup->contentWidget());m_reflectionLayerBlur->setRange(0,24);m_reflectionLayerBlur->setSuffix(tr(" px"));
    m_reflectionLayerWave=new QSpinBox(m_reflectionLayerGroup->contentWidget());m_reflectionLayerWave->setRange(0,32);
    reflectionLayerForm->addRow(tr("Tipo"),m_reflectionLayerPreset);reflectionLayerForm->addRow(tr("Intensidade"),m_reflectionLayerOpacity);
    reflectionLayerForm->addRow(tr("Suavidade"),m_reflectionLayerBlur);reflectionLayerForm->addRow(tr("Ondulação"),m_reflectionLayerWave);
    page->addWidget(m_reflectionLayerGroup);
    page->addStretch(1);

    auto touch = [this](std::function<void(const LayerPtr&)> fn, const QString& label) {
        if (m_updating) return;
        const QVector<LayerPtr> layers = ed.selectedLayers();
        if (layers.isEmpty()) return;
        const DocSnapshot before = ed.snapshotDoc();
        for (const LayerPtr& layer : layers) if (layer && !layer->locked) fn(layer);
        ed.pushDocHistory(before, label);
        emit ed.mapChanged();
        emit ed.layersChanged();
    };
    connect(m_layerName, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        const LayerPtr layer = ed.selectedLayer();
        if (!layer || layer->locked || m_layerName->text().trimmed().isEmpty() ||
            layer->name == m_layerName->text().trimmed()) return;
        const DocSnapshot before = ed.snapshotDoc();
        layer->name = m_layerName->text().trimmed();
        ed.pushDocHistory(before, tr("Renomear camada"));
        emit ed.mapChanged(); emit ed.layersChanged();
    });
    connect(m_layerOpacity, &QSlider::valueChanged, this, [this](int v) {
        m_layerOpacityLabel->setText(QStringLiteral("%1%").arg(v));
    });
    connect(m_layerOpacity, &QSlider::sliderReleased, this, [this, touch] {
        const int v = m_layerOpacity->value();
        touch([v](const LayerPtr& l) { l->opacity = v / 100.0; }, tr("Alterar opacidade da camada"));
    });
    connect(m_layerBlend, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, touch](int index) {
        if (index < 0) return;
        const QString mode = m_layerBlend->itemData(index).toString();
        touch([mode](const LayerPtr& l) { l->blendMode = mode; }, tr("Alterar mistura da camada"));
    });
    connect(m_layerOffX, &QSpinBox::editingFinished, this, [this] {
        if (m_updating) return; const LayerPtr primary=ed.selectedLayer(); if(!primary)return;
        const int delta=m_layerOffX->value()-primary->offsetx;if(!delta)return;
        const DocSnapshot before=ed.snapshotDoc();for(const LayerPtr& l:ed.selectedLayers())if(l&&!l->locked&&l->type!=LayerType::Group)l->offsetx+=delta;
        ed.pushDocHistory(before,tr("Mover camadas na horizontal"));emit ed.mapChanged();emit ed.layersChanged();
    });
    connect(m_layerOffY, &QSpinBox::editingFinished, this, [this] {
        if (m_updating) return; const LayerPtr primary=ed.selectedLayer(); if(!primary)return;
        const int delta=m_layerOffY->value()-primary->offsety;if(!delta)return;
        const DocSnapshot before=ed.snapshotDoc();for(const LayerPtr& l:ed.selectedLayers())if(l&&!l->locked&&l->type!=LayerType::Group)l->offsety+=delta;
        ed.pushDocHistory(before,tr("Mover camadas na vertical"));emit ed.mapChanged();emit ed.layersChanged();
    });
    connect(m_layerVisible, &QCheckBox::toggled, this, [this, touch](bool v) {
        touch([v](const LayerPtr& l) { l->visible = v; }, tr("Visibilidade"));
    });
    connect(m_layerLocked, &QCheckBox::toggled, this, [this, touch](bool v) {
        touch([v](const LayerPtr& l) { l->locked = v; }, tr("Bloqueio"));
    });
    connect(m_layerMask, &QCheckBox::toggled, this, [this, touch](bool v) {
        touch([v](const LayerPtr& l) { l->isMask = v; }, tr("Usar como recorte"));
    });
    connect(m_layerMaskBase, &QCheckBox::toggled, this, [this, touch](bool v) {
        touch([v](const LayerPtr& l) { l->maskShowBase = v; }, tr("Mostrar camada de recorte"));
    });
    connect(m_layerAbove, &QCheckBox::toggled, this, [this, touch](bool v) {
        touch([v](const LayerPtr& l) { l->zMode = v ? QStringLiteral("above") : QStringLiteral("below"); },
              tr("Ordem Z"));
    });
    connect(m_layerAlphaLock, &QCheckBox::toggled, this, [this, touch](bool v) {
        touch([v](const LayerPtr& l) {
            if (l->type == LayerType::Image || l->type == LayerType::Tile) l->alphaLock = v;
        }, v ? tr("Proteger áreas vazias") : tr("Permitir pintar nas áreas vazias"));
    });
    connect(m_layerFiltersButton, &QPushButton::clicked, this, [this] {
        if (m_updating) return;
        const LayerPtr l = ed.selectedLayer();
        if (!l || (l->type != LayerType::Image && l->type != LayerType::Tile && l->type != LayerType::Group)) return;
        if (l->type == LayerType::Image && l->imageReferenceOnly) return;
        if (editLayerRasterFilters(ed, l, false, this))
            emit statusMessage((l->type == LayerType::Tile || l->type == LayerType::Group)
                                   ? tr("Sombra de contato atualizada.")
                                   : tr("Efeitos da camada atualizados."));
    });
    connect(m_maskFiltersButton, &QPushButton::clicked, this, [this] {
        if (m_updating) return;
        const LayerPtr l = ed.selectedLayer();
        if (!l || (l->type != LayerType::Image && l->type != LayerType::Tile) || l->imageMask.isNull()) return;
        if (editLayerRasterFilters(ed, l, true, this)) emit statusMessage(tr("Efeitos da máscara atualizados."));
    });
    connect(m_imageScaleX, &QDoubleSpinBox::editingFinished, this, [this, touch] {
        const double value = m_imageScaleX->value() / 100.0;
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image) l->imageScaleX = value; },
              tr("Escalar imagem em X"));
    });
    connect(m_imageScaleY, &QDoubleSpinBox::editingFinished, this, [this, touch] {
        const double value = m_imageScaleY->value() / 100.0;
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image) l->imageScaleY = value; },
              tr("Escalar imagem em Y"));
    });
    connect(m_imageRotation, &QDoubleSpinBox::editingFinished, this, [this, touch] {
        const double value = m_imageRotation->value();
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image) l->imageRotation = value; },
              tr("Rotacionar imagem"));
    });
    connect(m_imageFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, touch](int index) {
        const QString value = m_imageFilter->itemData(index).toString();
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image) l->imageFilter = value; },
              tr("Alterar filtro da imagem"));
    });
    connect(m_imageFlipX, &QCheckBox::toggled, this, [this, touch](bool value) {
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image) l->imageFlipX = value; },
              tr("Inverter imagem horizontalmente"));
    });
    connect(m_imageFlipY, &QCheckBox::toggled, this, [this, touch](bool value) {
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image) l->imageFlipY = value; },
              tr("Inverter imagem verticalmente"));
    });
    connect(m_imageRepeatX, &QCheckBox::toggled, this, [this, touch](bool value) {
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image && !l->imagePaintLayer) l->imageRepeatX = value; },
              tr("Repetir imagem horizontalmente"));
    });
    connect(m_imageRepeatY, &QCheckBox::toggled, this, [this, touch](bool value) {
        touch([value](const LayerPtr& l) { if (l->type == LayerType::Image && !l->imagePaintLayer) l->imageRepeatY = value; },
              tr("Repetir imagem verticalmente"));
    });
    connect(m_imageExport, &QCheckBox::toggled, this, [this, touch](bool exportImage) {
        touch([exportImage](const LayerPtr& l) {
            if (l->type == LayerType::Image) l->imageReferenceOnly = !exportImage;
        }, tr("Alterar exportação da imagem"));
    });
    connect(m_parallaxEnabled,&QCheckBox::toggled,this,[this,touch](bool value){touch([this,value](const LayerPtr& l){
        const bool compatible=ed.rpgMakerEngine==RpgMakerEngine::MZ||l->type==LayerType::Image;
        if(compatible&&!l->reflectionLayer&&!l->isMask){l->parallaxLayer=value;if(value&&l->type==LayerType::Image)l->imageReferenceOnly=false;}
    },tr("Alterar Camada Visual"));});
    auto connectParallax=[this,touch](QDoubleSpinBox* spin,double Layer::*field,const QString& label){connect(spin,&QDoubleSpinBox::editingFinished,this,[this,spin,field,label,touch]{const double value=spin->value();touch([this,field,value](const LayerPtr& l){if(ed.rpgMakerEngine==RpgMakerEngine::MZ||l->type==LayerType::Image)(l.data()->*field)=value;},label);});};
    connectParallax(m_parallaxFactorX,&Layer::parallaxFactorX,tr("Alterar profundidade horizontal"));
    connectParallax(m_parallaxFactorY,&Layer::parallaxFactorY,tr("Alterar profundidade vertical"));
    connectParallax(m_parallaxSpeedX,&Layer::parallaxSpeedX,tr("Alterar movimento horizontal"));
    connectParallax(m_parallaxSpeedY,&Layer::parallaxSpeedY,tr("Alterar movimento vertical"));
    connectParallax(m_parallaxOscillationX,&Layer::parallaxOscillationX,tr("Alterar balanço horizontal"));
    connectParallax(m_parallaxOscillationY,&Layer::parallaxOscillationY,tr("Alterar balanço vertical"));
    connectParallax(m_parallaxOscillationSpeed,&Layer::parallaxOscillationSpeed,tr("Alterar velocidade do balanço"));
    connectParallax(m_parallaxAnimationFps,&Layer::parallaxAnimationFps,tr("Alterar velocidade da animação"));
    connectParallax(m_parallaxEffectSpeed,&Layer::parallaxEffectSpeed,tr("Alterar velocidade do efeito"));
    connect(m_parallaxEffectStrength,&QDoubleSpinBox::editingFinished,this,[this,touch]{
        const double value=m_parallaxEffectStrength->value()/100.0;
        touch([value](const LayerPtr& l){l->parallaxEffectStrength=value;},tr("Alterar intensidade do efeito"));
    });
    connect(m_parallaxMotionPreset,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,touch](int i){
        if(i<0)return;const QString preset=m_parallaxMotionPreset->itemData(i).toString();
        touch([preset](const LayerPtr& l){
            l->parallaxMotionPreset=preset;
            if(preset==QLatin1String("cloudSlow")){l->parallaxFactorX=.18;l->parallaxFactorY=.12;l->parallaxSpeedX=10;l->parallaxSpeedY=0;l->parallaxOscillationX=0;l->parallaxOscillationY=3;l->parallaxOscillationSpeed=.08;l->parallaxRepeatX=true;if(l->type==LayerType::Image)l->imageRepeatX=true;}
            else if(preset==QLatin1String("cloudFast")){l->parallaxFactorX=.32;l->parallaxFactorY=.20;l->parallaxSpeedX=32;l->parallaxSpeedY=0;l->parallaxOscillationX=0;l->parallaxOscillationY=5;l->parallaxOscillationSpeed=.14;l->parallaxRepeatX=true;if(l->type==LayerType::Image)l->imageRepeatX=true;}
            else if(preset==QLatin1String("mist")){l->parallaxFactorX=.42;l->parallaxFactorY=.32;l->parallaxSpeedX=7;l->parallaxSpeedY=0;l->parallaxOscillationX=8;l->parallaxOscillationY=3;l->parallaxOscillationSpeed=.10;l->parallaxRepeatX=true;if(l->type==LayerType::Image)l->imageRepeatX=true;}
            else if(preset==QLatin1String("float")){l->parallaxSpeedX=0;l->parallaxSpeedY=0;l->parallaxOscillationX=4;l->parallaxOscillationY=8;l->parallaxOscillationSpeed=.18;}
        },tr("Aplicar movimento pronto"));
    });
    connect(m_parallaxRepeatX,&QCheckBox::toggled,this,[touch](bool value){touch([value](const LayerPtr& l){l->parallaxRepeatX=value;if(l->type==LayerType::Image)l->imageRepeatX=value;},QObject::tr("Repetir Camada Visual para os lados"));});
    connect(m_parallaxRepeatY,&QCheckBox::toggled,this,[touch](bool value){touch([value](const LayerPtr& l){l->parallaxRepeatY=value;if(l->type==LayerType::Image)l->imageRepeatY=value;},QObject::tr("Repetir Camada Visual verticalmente"));});
    connect(m_parallaxSmoothMotion,&QCheckBox::toggled,this,[touch](bool value){touch([value](const LayerPtr& l){l->parallaxSmoothMotion=value;},QObject::tr("Alterar suavidade do movimento"));});
    connect(m_parallaxAnimationEnabled,&QCheckBox::toggled,this,[touch](bool value){touch([value](const LayerPtr& l){if(l->type==LayerType::Image){l->parallaxAnimationEnabled=value;if(value&&l->parallaxAnimationFrames<=1)l->parallaxAnimationFrames=qMax(1,l->parallaxAnimationColumns*l->parallaxAnimationRows);}},QObject::tr("Alterar animação da Camada Visual"));});
    auto connectVisualInt=[touch](QSpinBox* spin,int Layer::*field,const QString& label){QObject::connect(spin,&QSpinBox::editingFinished,spin,[spin,field,label,touch]{const int value=spin->value();touch([field,value](const LayerPtr& l){if(l->type==LayerType::Image)(l.data()->*field)=value;},label);});};
    connect(m_parallaxAnimationColumns,&QSpinBox::editingFinished,this,[this,touch]{const int value=m_parallaxAnimationColumns->value();touch([value](const LayerPtr& l){if(l->type!=LayerType::Image)return;const int oldCapacity=qMax(1,l->parallaxAnimationColumns*l->parallaxAnimationRows);l->parallaxAnimationColumns=value;const int capacity=qMax(1,value*l->parallaxAnimationRows);if(l->parallaxAnimationFrames==oldCapacity||l->parallaxAnimationFrames>capacity)l->parallaxAnimationFrames=capacity;},tr("Alterar colunas da animação"));});
    connect(m_parallaxAnimationRows,&QSpinBox::editingFinished,this,[this,touch]{const int value=m_parallaxAnimationRows->value();touch([value](const LayerPtr& l){if(l->type!=LayerType::Image)return;const int oldCapacity=qMax(1,l->parallaxAnimationColumns*l->parallaxAnimationRows);l->parallaxAnimationRows=value;const int capacity=qMax(1,l->parallaxAnimationColumns*value);if(l->parallaxAnimationFrames==oldCapacity||l->parallaxAnimationFrames>capacity)l->parallaxAnimationFrames=capacity;},tr("Alterar linhas da animação"));});
    connectVisualInt(m_parallaxAnimationFrames,&Layer::parallaxAnimationFrames,tr("Alterar quantidade de quadros"));
    connect(m_parallaxAnimationPingPong,&QCheckBox::toggled,this,[touch](bool value){touch([value](const LayerPtr& l){if(l->type==LayerType::Image)l->parallaxAnimationPingPong=value;},QObject::tr("Alterar reprodução da animação"));});
    connect(m_parallaxEffectPreset,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,touch](int i){if(i<0)return;const QString value=m_parallaxEffectPreset->itemData(i).toString();touch([value](const LayerPtr& l){l->parallaxEffectPreset=value;},tr("Alterar clima visual"));});
    connect(m_reflectionLayerPreset,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,touch](int i){if(i<0)return;const QString value=m_reflectionLayerPreset->itemData(i).toString();touch([value](const LayerPtr& l){if(l->reflectionLayer)l->reflectionPreset=value;},tr("Alterar tipo de reflexo"));});
    connect(m_reflectionLayerOpacity,&QSpinBox::editingFinished,this,[this,touch]{const int value=m_reflectionLayerOpacity->value();touch([value](const LayerPtr& l){if(l->reflectionLayer)l->reflectionOpacity=value;},tr("Alterar intensidade do reflexo"));});
    connect(m_reflectionLayerBlur,&QSpinBox::editingFinished,this,[this,touch]{const int value=m_reflectionLayerBlur->value();touch([value](const LayerPtr& l){if(l->reflectionLayer)l->reflectionBlur=value;},tr("Alterar suavidade do reflexo"));});
    connect(m_reflectionLayerWave,&QSpinBox::editingFinished,this,[this,touch]{const int value=m_reflectionLayerWave->value();touch([value](const LayerPtr& l){if(l->reflectionLayer)l->reflectionWave=value;},tr("Alterar ondulação do reflexo"));});
    connect(m_imageReset, &QPushButton::clicked, this, [this, touch] {
        touch([](const LayerPtr& l) {
            if (l->type != LayerType::Image) return;
            l->offsetx = 0;
            l->offsety = 0;
            l->imageScaleX = 1.0;
            l->imageScaleY = 1.0;
            l->imageRotation = 0.0;
            l->imageFlipX = false;
            l->imageFlipY = false;
            l->imageRepeatX = false;
            l->imageRepeatY = false;
            l->imageFilter = QStringLiteral("nearest");
        }, tr("Resetar transformação da imagem"));
    });
    connect(m_imageReplace, &QPushButton::clicked, this, [this] {
        if (m_updating) return;
        LayerPtr l = ed.selectedLayer();
        if (!l || l->type != LayerType::Image) return;
        const QString path = AssetBrowserDialog::chooseImage(ed, this, QStringLiteral("Pictures"));
        if (path.isEmpty()) return;
        QImage image(path);
        if (image.isNull()) {
            emit statusMessage(tr("Não foi possível ler a imagem selecionada."));
            return;
        }
        const DocSnapshot before = ed.snapshotDoc();
        l->image = image;
        l->imagewidth = image.width();
        l->imageheight = image.height();
        const QString relative = ed.projectRelativePath(path);
        l->imagePath = relative.isEmpty() ? path : relative;
        ed.pushDocHistory(before, tr("Substituir imagem da camada"));
        emit ed.mapChanged();
        emit ed.layersChanged();
        emit statusMessage(tr("Imagem da camada substituída."));
    });
    return w;
}

void PropertiesPanel::refreshLayer()
{
    const LayerPtr l = ed.selectedLayer();
    const bool has = !l.isNull();
    const int selectionCount = ed.selectedLayers().size();
    const bool multiple = selectionCount > 1;
    m_layerName->setEnabled(has && !multiple);
    m_layerOpacity->setEnabled(has);
    m_layerBlend->setEnabled(has);
    if (!has) {
        m_layerName->clear();
        m_layerType->setText(tr("— nenhuma camada —"));
        if (m_imageTransformGroup) m_imageTransformGroup->setVisible(false);
        if (m_parallaxGroup) m_parallaxGroup->setVisible(false);
        if (m_parallaxMotionGroup) m_parallaxMotionGroup->setVisible(false);
        if (m_parallaxAnimationGroup) m_parallaxAnimationGroup->setVisible(false);
        if (m_parallaxEffectsGroup) m_parallaxEffectsGroup->setVisible(false);
        if (m_reflectionLayerGroup) m_reflectionLayerGroup->setVisible(false);
        if (m_layerAlphaLock) { m_layerAlphaLock->setChecked(false); m_layerAlphaLock->setEnabled(false); }
        if (m_layerFiltersButton) m_layerFiltersButton->setVisible(false);
        if (m_maskFiltersButton) m_maskFiltersButton->setVisible(false);
        return;
    }
    m_layerName->setText(l->name);
    QString type;
    switch (l->type) {
    case LayerType::Tile:   type = tr("Camada de tiles %1×%2 (%3×%4 células)")
                                       .arg(l->tileWidth).arg(l->tileHeight).arg(l->cols).arg(l->rows); break;
    case LayerType::Object: type = tr("Camada de objetos (%1)").arg(l->objects.size()); break;
    case LayerType::Image:
        type = l->reflectionLayer ? tr("Camada de Reflexo") : l->imagePaintLayer
            ? tr("Camada de pintura %1×%2").arg(l->imagewidth).arg(l->imageheight)
            : (l->imageReferenceOnly
               ? tr("Imagem de referência %1×%2").arg(l->imagewidth).arg(l->imageheight)
               : tr("Camada de imagem %1×%2").arg(l->imagewidth).arg(l->imageheight));
        break;
    case LayerType::Group:
        type = l->children.size() == 1
            ? tr("Pasta de camadas (1 camada)")
            : tr("Pasta de camadas (%1 camadas)").arg(l->children.size());
        break;
    }
    if (multiple)
        type += tr("\n%1 camadas selecionadas — mudanças compatíveis serão aplicadas a todas.").arg(selectionCount);
    m_layerType->setText(type);
    m_layerOpacity->setValue(int(l->opacity * 100));
    m_layerOpacityLabel->setText(QStringLiteral("%1%").arg(int(l->opacity * 100)));
    {
        int blendIndex = m_layerBlend->findData(l->blendMode);
        if (blendIndex < 0) blendIndex = m_layerBlend->findData(QStringLiteral("source-over"));
        m_layerBlend->setCurrentIndex(qMax(0, blendIndex));
    }
    m_layerOffX->setValue(l->offsetx);
    m_layerOffY->setValue(l->offsety);
    m_layerVisible->setChecked(l->visible);
    m_layerLocked->setChecked(l->locked);
    m_layerMask->setChecked(l->isMask);
    m_layerMaskBase->setChecked(l->maskShowBase);
    m_layerAbove->setChecked(l->zMode == QLatin1String("above"));
    m_layerAlphaLock->setChecked(l->alphaLock);
    m_layerMask->setEnabled(l->type == LayerType::Tile || l->type == LayerType::Image);
    m_layerMaskBase->setEnabled(l->isMask);
    m_layerAbove->setEnabled(l->type == LayerType::Tile || l->type == LayerType::Image);
    m_layerAlphaLock->setEnabled(l->type == LayerType::Tile || l->type == LayerType::Image);

    const bool imageLayer = l->type == LayerType::Image;
    const bool paintLayer = imageLayer && l->imagePaintLayer;
    const bool contentFilters = (imageLayer && !l->imageReferenceOnly) ||
                                l->type == LayerType::Tile || l->type == LayerType::Group;
    const bool rasterMaskFilters = (imageLayer || l->type == LayerType::Tile) && !l->imageMask.isNull();
    m_layerFiltersButton->setVisible(contentFilters);
    m_layerFiltersButton->setEnabled(contentFilters);
    if (l->type == LayerType::Tile || l->type == LayerType::Group) {
        m_layerFiltersButton->setIcon(icons::get(QStringLiteral("filter-contact-shadow")));
        m_layerFiltersButton->setText(l->imageFilters.isEmpty()
            ? tr("Sombra de contato…") : tr("Sombra de contato… (%1)").arg(l->imageFilters.size()));
        m_layerFiltersButton->setToolTip(l->type == LayerType::Group
            ? tr("Cria uma única sombra usando o resultado visual das camadas guardadas nesta pasta. A pasta continua sendo apenas um organizador; ela não vira uma camada de tiles.")
            : tr("Cria uma sombra curta nas bordas dos tiles sem transformar a camada em imagem nem impedir que você continue editando os tiles." ));
    } else {
        m_layerFiltersButton->setIcon(icons::get(QStringLiteral("layer-filters")));
        m_layerFiltersButton->setText(l->imageFilters.isEmpty()
            ? tr("Efeitos da camada…") : tr("Efeitos da camada… (%1)").arg(l->imageFilters.size()));
        m_layerFiltersButton->setToolTip(tr("Adicione desfoque, granulação e outros efeitos sem alterar permanentemente a imagem original."));
    }
    m_maskFiltersButton->setVisible(rasterMaskFilters);
    m_maskFiltersButton->setEnabled(rasterMaskFilters);
    m_maskFiltersButton->setText(l->maskFilters.isEmpty()
        ? tr("Efeitos da máscara…") : tr("Efeitos da máscara… (%1)").arg(l->maskFilters.size()));
    m_maskFiltersButton->setToolTip(tr("Ajuste apenas a máscara. Por exemplo: suavize as bordas ou crie recortes irregulares sem mexer no conteúdo da camada."));
    const bool groupLayer = l->type == LayerType::Group;
    m_layerOffX->setEnabled(has && !paintLayer && !groupLayer);
    m_layerOffY->setEnabled(has && !paintLayer && !groupLayer);
    m_layerBlend->setEnabled(!groupLayer);

    // Pasta é um organizador, não uma Tile Layer. Ao selecioná-la mostramos
    // somente propriedades que realmente fazem sentido para o conjunto:
    // nome, visibilidade, bloqueio, opacidade e Sombra de contato. Os controles
    // de posição, recorte, “acima do jogador”, proteção de áreas vazias e
    // mistura pertencem às camadas de conteúdo e somem completamente da UI.
    if (auto* layerForm = qobject_cast<QFormLayout*>(m_layerName->parentWidget()->layout())) {
        layerForm->setRowVisible(m_layerBlend, !groupLayer);
        layerForm->setRowVisible(m_layerOffX, !groupLayer);
        layerForm->setRowVisible(m_layerOffY, !groupLayer);
        layerForm->setRowVisible(m_layerMask, !groupLayer);
        layerForm->setRowVisible(m_layerMaskBase, !groupLayer);
        layerForm->setRowVisible(m_layerAbove, !groupLayer);
        layerForm->setRowVisible(m_layerAlphaLock, !groupLayer);
    }
    m_imageTransformGroup->setVisible(imageLayer && !multiple);
    const bool mzVisualLayer = ed.rpgMakerEngine == RpgMakerEngine::MZ &&
        !l->reflectionLayer && !l->isMask;
    const bool mvParallaxImage = ed.rpgMakerEngine == RpgMakerEngine::MV &&
        imageLayer && !l->reflectionLayer && !l->isMask;
    const bool showVisualLayer = mzVisualLayer || mvParallaxImage;
    const bool advancedMz = showVisualLayer && ed.rpgMakerEngine == RpgMakerEngine::MZ;
    const bool imageAnimation = advancedMz && imageLayer;
    m_parallaxGroup->setVisible(showVisualLayer);
    m_parallaxMotionGroup->setVisible(advancedMz);
    m_parallaxAnimationGroup->setVisible(imageAnimation);
    m_parallaxEffectsGroup->setVisible(advancedMz);
    m_reflectionLayerGroup->setVisible(ed.rpgMakerEngine == RpgMakerEngine::MZ &&
                                       imageLayer && l->reflectionLayer);
    if (showVisualLayer) {
        m_parallaxEnabled->setChecked(l->parallaxLayer);
        const int motionIndex = m_parallaxMotionPreset->findData(l->parallaxMotionPreset);
        m_parallaxMotionPreset->setCurrentIndex(qMax(0, motionIndex));
        m_parallaxFactorX->setValue(l->parallaxFactorX);
        m_parallaxFactorY->setValue(l->parallaxFactorY);
        m_parallaxSpeedX->setValue(l->parallaxSpeedX);
        m_parallaxSpeedY->setValue(l->parallaxSpeedY);
        m_parallaxOscillationX->setValue(l->parallaxOscillationX);
        m_parallaxOscillationY->setValue(l->parallaxOscillationY);
        m_parallaxOscillationSpeed->setValue(l->parallaxOscillationSpeed);
        m_parallaxRepeatX->setChecked(l->parallaxRepeatX || (imageLayer && l->imageRepeatX));
        m_parallaxRepeatY->setChecked(l->parallaxRepeatY || (imageLayer && l->imageRepeatY));
        m_parallaxSmoothMotion->setChecked(l->parallaxSmoothMotion);
        m_parallaxAnimationEnabled->setChecked(l->parallaxAnimationEnabled);
        m_parallaxAnimationColumns->setValue(qMax(1, l->parallaxAnimationColumns));
        m_parallaxAnimationRows->setValue(qMax(1, l->parallaxAnimationRows));
        m_parallaxAnimationFrames->setValue(qMax(1, l->parallaxAnimationFrames));
        m_parallaxAnimationFps->setValue(qMax(.1, l->parallaxAnimationFps));
        m_parallaxAnimationPingPong->setChecked(l->parallaxAnimationPingPong);
        const int effectIndex = m_parallaxEffectPreset->findData(l->parallaxEffectPreset);
        m_parallaxEffectPreset->setCurrentIndex(qMax(0, effectIndex));
        m_parallaxEffectStrength->setValue(qBound(0.0, l->parallaxEffectStrength * 100.0, 100.0));
        m_parallaxEffectSpeed->setValue(qMax(0.0, l->parallaxEffectSpeed));
        const bool animationEnabled = imageAnimation && l->parallaxAnimationEnabled;
        for (QWidget* control : {static_cast<QWidget*>(m_parallaxAnimationColumns),
                                 static_cast<QWidget*>(m_parallaxAnimationRows),
                                 static_cast<QWidget*>(m_parallaxAnimationFrames),
                                 static_cast<QWidget*>(m_parallaxAnimationFps),
                                 static_cast<QWidget*>(m_parallaxAnimationPingPong)})
            control->setEnabled(animationEnabled);
    }
    if (imageLayer) {
        m_imageSource->setText(paintLayer ? tr("(pintura criada dentro do mapa)")
                                          : (l->imagePath.isEmpty() ? tr("(incorporada no projeto)") : l->imagePath));
        m_imageSource->setToolTip(paintLayer ? tr("Esta pintura faz parte do próprio projeto e é salva junto com o mapa.") : l->imagePath);
        m_imageScaleX->setValue(qBound(1.0, l->imageScaleX * 100.0, 10000.0));
        m_imageScaleY->setValue(qBound(1.0, l->imageScaleY * 100.0, 10000.0));
        m_imageRotation->setValue(l->imageRotation);
        int filterIndex = m_imageFilter->findData(l->imageFilter);
        if (filterIndex < 0) filterIndex = m_imageFilter->findData(QStringLiteral("nearest"));
        m_imageFilter->setCurrentIndex(qMax(0, filterIndex));
        m_imageFlipX->setChecked(l->imageFlipX);
        m_imageFlipY->setChecked(l->imageFlipY);
        m_imageRepeatX->setChecked(l->imageRepeatX);
        m_imageRepeatY->setChecked(l->imageRepeatY);
        m_imageExport->setChecked(!l->imageReferenceOnly);
        const int reflectionPreset=m_reflectionLayerPreset->findData(l->reflectionPreset);m_reflectionLayerPreset->setCurrentIndex(qMax(0,reflectionPreset));
        m_reflectionLayerOpacity->setValue(l->reflectionOpacity);m_reflectionLayerBlur->setValue(l->reflectionBlur);m_reflectionLayerWave->setValue(l->reflectionWave);
        // Paint Layers precisam permanecer 1:1 com os pixels do mapa. Isso
        // mantém cursor, Undo/Redo e exportação alinhados ao que foi pintado.
        for (QWidget* control : {static_cast<QWidget*>(m_imageScaleX), static_cast<QWidget*>(m_imageScaleY),
                                 static_cast<QWidget*>(m_imageRotation), static_cast<QWidget*>(m_imageFlipX),
                                 static_cast<QWidget*>(m_imageFlipY), static_cast<QWidget*>(m_imageRepeatX),
                                 static_cast<QWidget*>(m_imageRepeatY), static_cast<QWidget*>(m_imageReplace),
                                 static_cast<QWidget*>(m_imageReset)})
            control->setEnabled(!paintLayer);
        m_imageExport->setEnabled(!paintLayer);
        m_imageFilter->setEnabled(true);
    }
}

// ---------------------------------------------------------------- Tileset
QWidget* PropertiesPanel::buildTilesetTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(8, 8, 8, 8);

    auto* generalSection = new CollapsibleSection(tr("Geral"),
        QStringLiteral("Inspector/Tileset/GeneralExpanded"), true, w);
    auto* form = new QFormLayout(generalSection->contentWidget());
    m_tsName = new QLineEdit(generalSection->contentWidget());
    form->addRow(tr("Nome"), m_tsName);
    v->addWidget(generalSection);

    auto* infoSection = new CollapsibleSection(tr("Informações"),
        QStringLiteral("Inspector/Tileset/InfoExpanded"), false, w);
    auto* infoLayout = new QVBoxLayout(infoSection->contentWidget());
    m_tsInfo = new QLabel(infoSection->contentWidget());
    m_tsInfo->setWordWrap(true);
    m_tsInfo->setProperty("uiRole", QStringLiteral("hint"));
    infoLayout->addWidget(m_tsInfo);
    v->addWidget(infoSection);

    auto* srcBox = new CollapsibleSection(tr("Imagens combinadas"),
        QStringLiteral("Inspector/Tileset/SourcesExpanded"), false, w);
    auto* sv = new QVBoxLayout(srcBox->contentWidget());
    m_tsSources = new QListWidget(srcBox->contentWidget());
    m_tsSources->setMaximumHeight(150);
    sv->addWidget(m_tsSources);
    v->addWidget(srcBox);
    v->addStretch(1);

    connect(m_tsName, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        if (Tileset* ts = ed.tilesetAt(ed.session.activeTilesetIdx)) {
            ts->name = m_tsName->text();
            ed.markDirty();
            emit ed.tilesetsChanged();
        }
    });
    return w;
}

void PropertiesPanel::refreshTileset()
{
    const Tileset* ts = ed.tilesetAt(ed.session.activeTilesetIdx);
    if (!ts) {
        m_tsName->clear();
        m_tsInfo->setText(tr("Nenhum tileset selecionado."));
        m_tsSources->clear();
        return;
    }
    m_tsName->setText(ts->name);
    m_tsInfo->setText(tr("Imagem: %1 × %2 px\nTile: %3 × %4 px · espaçamento %5 · margem %6\n"
                         "Grade: %7 × %8 = %9 tiles\nfirstgid: %10%11")
                          .arg(ts->imagewidth).arg(ts->imageheight)
                          .arg(ts->tilewidth).arg(ts->tileheight).arg(ts->spacing).arg(ts->margin)
                          .arg(ts->columns).arg(ts->rows).arg(ts->tilecount)
                          .arg(ts->firstgid)
                          .arg(ts->isVX512 ? tr("\n⚠ Atlas 512×512 (padrão atlas 512×512)") : QString()));
    m_tsSources->clear();
    for (const CombinedSource& s : ts->combinedSources)
        m_tsSources->addItem(tr("%1 — bloco (%2,%3) %4×%5 tiles")
                                 .arg(s.name).arg(s.x).arg(s.y).arg(s.cols).arg(s.rows));
    if (ts->combinedSources.isEmpty()) m_tsSources->addItem(tr("(tileset simples)"));
}

// ----------------------------------------------------------------- Objeto
QWidget* PropertiesPanel::buildObjectTab()
{
    auto* w = new QWidget;
    auto* page = new QVBoxLayout(w);
    page->setContentsMargins(8, 8, 8, 8);
    page->setSpacing(2);
    auto* transformSection = new CollapsibleSection(tr("Transformar"),
        QStringLiteral("Inspector/Object/TransformExpanded"), true, w);
    auto* form = new QFormLayout(transformSection->contentWidget());
    page->addWidget(transformSection);
    page->addStretch(1);
    m_objName = new QLineEdit(w);
    m_objX = new QDoubleSpinBox(w); m_objX->setRange(-100000, 100000);
    m_objY = new QDoubleSpinBox(w); m_objY->setRange(-100000, 100000);
    m_objW = new QDoubleSpinBox(w); m_objW->setRange(1, 100000);
    m_objH = new QDoubleSpinBox(w); m_objH->setRange(1, 100000);
    m_objScaleX = new QDoubleSpinBox(w);
    m_objScaleY = new QDoubleSpinBox(w);
    for (QDoubleSpinBox* scale : {m_objScaleX, m_objScaleY}) {
        scale->setRange(1.0, 3200.0);
        scale->setDecimals(1);
        scale->setSingleStep(5.0);
        scale->setSuffix(QStringLiteral("%"));
    }
    m_objScaleFilter = new QComboBox(w);
    m_objScaleFilter->addItem(tr("xBR (pixel art)"), QStringLiteral("xbr"));
    m_objScaleFilter->addItem(tr("Nearest Neighbor"), QStringLiteral("nearest"));
    m_objScaleFilter->addItem(tr("Suave / bilinear"), QStringLiteral("smooth"));
    m_objScaleFilter->setToolTip(tr("xBR suaviza diagonais durante ampliações de pixel art sem borrar como bilinear."));
    m_objDepth = new QSpinBox(w);
    m_objDepth->setRange(0, 0);
    m_objDepth->setToolTip(tr("Ordem dentro da camada de objetos. 0 = mais ao fundo; valores maiores = mais à frente."));
    m_objRotation = new QDoubleSpinBox(w);
    m_objRotation->setRange(-360.0, 360.0);
    m_objRotation->setDecimals(1);
    m_objRotation->setSingleStep(1.0);
    m_objRotation->setSuffix(QStringLiteral("°"));
    m_objRotation->setWrapping(true);
    m_objRotation->setToolTip(tr("Rotação não destrutiva ao redor do centro do objeto."));
    m_objRotationFilter = new QComboBox(w);
    m_objRotationFilter->addItem(tr("RotSprite (pixel art)"), QStringLiteral("rotsprite"));
    m_objRotationFilter->addItem(tr("Nearest Neighbor"), QStringLiteral("nearest"));
    m_objRotationFilter->addItem(tr("Suave / bilinear"), QStringLiteral("smooth"));
    m_objRotationFilter->setToolTip(tr("RotSprite é o padrão recomendado para pixel art e reduz deformações em ângulos livres."));
    form->addRow(tr("Nome"), m_objName);
    form->addRow(tr("X"), m_objX);
    form->addRow(tr("Y"), m_objY);
    form->addRow(tr("Largura"), m_objW);
    form->addRow(tr("Altura"), m_objH);
    form->addRow(tr("Escala X"), m_objScaleX);
    form->addRow(tr("Escala Y"), m_objScaleY);
    form->addRow(tr("Filtro de escala"), m_objScaleFilter);
    form->addRow(tr("Profundidade"), m_objDepth);
    form->addRow(tr("Rotação"), m_objRotation);
    form->addRow(tr("Filtro de rotação"), m_objRotationFilter);

    auto* rotationRow = new QHBoxLayout;
    auto* rotateLeft = new QPushButton(tr("↶ 90°"), w);
    auto* rotationReset = new QPushButton(tr("Resetar"), w);
    auto* rotateRight = new QPushButton(tr("90° ↷"), w);
    rotateLeft->setToolTip(tr("Girar 90° para a esquerda"));
    rotationReset->setToolTip(tr("Voltar a rotação para 0°"));
    rotateRight->setToolTip(tr("Girar 90° para a direita"));
    rotationRow->addWidget(rotateLeft);
    rotationRow->addWidget(rotationReset);
    rotationRow->addWidget(rotateRight);
    form->addRow(tr("Atalhos"), rotationRow);

    auto* depthRow = new QHBoxLayout;
    auto* depthBack = new QPushButton(tr("Fundo"), w);
    auto* depthDown = new QPushButton(tr("↓"), w);
    auto* depthUp = new QPushButton(tr("↑"), w);
    auto* depthFront = new QPushButton(tr("Frente"), w);
    depthBack->setToolTip(tr("Enviar objeto para o fundo da camada"));
    depthDown->setToolTip(tr("Recuar uma posição"));
    depthUp->setToolTip(tr("Avançar uma posição"));
    depthFront->setToolTip(tr("Trazer objeto para a frente da camada"));
    depthRow->addWidget(depthBack);
    depthRow->addWidget(depthDown);
    depthRow->addWidget(depthUp);
    depthRow->addWidget(depthFront);
    form->addRow(tr("Ordem"), depthRow);

    m_objInfo = new QLabel(w);
    m_objInfo->setWordWrap(true);
    m_objInfo->setProperty("uiRole", QStringLiteral("hint"));
    form->addRow(m_objInfo);

    auto* sizeRow = new QHBoxLayout;
    auto* gridBtn = new QPushButton(tr("Ajustar à grade"), w);
    gridBtn->setToolTip(tr("Tamanho equivalente ao de uma camada de tiles: %1×%2 px por tile "
                           "do carimbo. Use para corrigir objetos antigos que ficaram menores.")
                            .arg(ed.mapInfo().tileWidth).arg(ed.mapInfo().tileHeight));
    auto* nativeBtn = new QPushButton(tr("Tamanho do tileset"), w);
    nativeBtn->setToolTip(tr("Tamanho em pixels reais do tileset de origem, sem escalonar."));
    sizeRow->addWidget(gridBtn);
    sizeRow->addWidget(nativeBtn);
    form->addRow(sizeRow);

    connect(gridBtn, &QPushButton::clicked, this, [this] {
        MapObject* o = selectedObject();
        if (!o) return;
        const DocSnapshot before = ed.snapshotDoc();
        o->w = qMax(1, o->stampW) * ed.mapInfo().tileWidth;
        o->h = qMax(1, o->stampH) * ed.mapInfo().tileHeight;
        ed.pushDocHistory(before, tr("Ajustar objeto à grade"));
        emit ed.mapChanged();
        refreshObject();
    });
    connect(nativeBtn, &QPushButton::clicked, this, [this] {
        MapObject* o = selectedObject();
        if (!o || o->tiles.isEmpty()) return;
        const Tileset* ts = ed.tilesetAt(o->tiles.first().tilesetIdx);
        if (!ts) return;
        const DocSnapshot before = ed.snapshotDoc();
        o->w = qMax(1, o->stampW) * ts->tilewidth;
        o->h = qMax(1, o->stampH) * ts->tileheight;
        ed.pushDocHistory(before, tr("Ajustar objeto ao tileset"));
        emit ed.mapChanged();
        refreshObject();
    });

    auto apply = [this] {
        if (m_updating) return;
        MapObject* o = selectedObject();
        if (!o) return;
        const DocSnapshot before = ed.snapshotDoc();
        o->name = m_objName->text();
        o->x = m_objX->value();
        o->y = m_objY->value();
        o->w = m_objW->value();
        o->h = m_objH->value();
        o->rotation = m_objRotation->value();
        o->rotationFilter = m_objRotationFilter->currentData().toString();
        o->scaleFilter = m_objScaleFilter->currentData().toString();
        ed.pushDocHistory(before, tr("Alterar propriedades do objeto"));
        emit ed.mapChanged();
    };
    connect(m_objName, &QLineEdit::editingFinished, this, apply);
    connect(m_objX, &QDoubleSpinBox::editingFinished, this, apply);
    connect(m_objY, &QDoubleSpinBox::editingFinished, this, apply);
    connect(m_objW, &QDoubleSpinBox::editingFinished, this, apply);
    connect(m_objH, &QDoubleSpinBox::editingFinished, this, apply);
    connect(m_objRotation, &QDoubleSpinBox::editingFinished, this, apply);
    connect(m_objRotationFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, apply](int) {
        if (!m_updating) apply();
    });
    connect(m_objScaleFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, apply](int) {
        if (!m_updating) apply();
    });

    auto applyScale = [this](bool horizontal) {
        if (m_updating) return;
        MapObject* o = selectedObject();
        if (!o) return;
        const QSizeF native = objectNativeSize(ed, *o);
        const DocSnapshot before = ed.snapshotDoc();
        if (horizontal) o->w = qMax(1.0, native.width() * m_objScaleX->value() / 100.0);
        else o->h = qMax(1.0, native.height() * m_objScaleY->value() / 100.0);
        ed.pushDocHistory(before, tr("Escalar objeto"));
        emit ed.mapChanged();
        refreshObject();
    };
    connect(m_objScaleX, &QDoubleSpinBox::editingFinished, this, [applyScale] { applyScale(true); });
    connect(m_objScaleY, &QDoubleSpinBox::editingFinished, this, [applyScale] { applyScale(false); });

    auto moveDepth = [this](int requested) {
        if (m_updating) return;
        const LayerPtr layer = ed.activeLayer();
        MapObject* selected = selectedObject();
        if (!layer || layer->type != LayerType::Object || !selected) return;
        int from = -1;
        for (int i = 0; i < layer->objects.size(); ++i)
            if (layer->objects[i].id == selected->id) { from = i; break; }
        if (from < 0 || layer->objects.isEmpty()) return;
        const int target = qBound(0, requested, layer->objects.size() - 1);
        if (target == from) { refreshObject(); return; }
        const DocSnapshot before = ed.snapshotDoc();
        layer->objects.move(from, target);
        ed.pushDocHistory(before, tr("Alterar profundidade do objeto"));
        emit ed.mapChanged();
        emit ed.selectionChanged();
        refreshObject();
    };
    connect(m_objDepth, &QSpinBox::editingFinished, this, [this, moveDepth] { moveDepth(m_objDepth->value()); });
    connect(depthBack, &QPushButton::clicked, this, [moveDepth] { moveDepth(0); });
    connect(depthDown, &QPushButton::clicked, this, [this, moveDepth] { moveDepth(m_objDepth->value() - 1); });
    connect(depthUp, &QPushButton::clicked, this, [this, moveDepth] { moveDepth(m_objDepth->value() + 1); });
    connect(depthFront, &QPushButton::clicked, this, [this, moveDepth] {
        const LayerPtr layer = ed.activeLayer();
        if (layer) moveDepth(qMax(0, layer->objects.size() - 1));
    });

    auto rotateBy = [this](double degrees, const QString& label) {
        MapObject* o = selectedObject();
        if (!o) return;
        const DocSnapshot before = ed.snapshotDoc();
        o->rotation = std::fmod(o->rotation + degrees, 360.0);
        if (o->rotation > 180.0) o->rotation -= 360.0;
        if (o->rotation <= -180.0) o->rotation += 360.0;
        ed.pushDocHistory(before, label);
        emit ed.mapChanged();
        refreshObject();
    };
    connect(rotateLeft, &QPushButton::clicked, this, [rotateBy, this] { rotateBy(-90.0, tr("Girar objeto 90° à esquerda")); });
    connect(rotateRight, &QPushButton::clicked, this, [rotateBy, this] { rotateBy(90.0, tr("Girar objeto 90° à direita")); });
    connect(rotationReset, &QPushButton::clicked, this, [this] {
        MapObject* o = selectedObject();
        if (!o || std::abs(o->rotation) < 0.000001) return;
        const DocSnapshot before = ed.snapshotDoc();
        o->rotation = 0.0;
        ed.pushDocHistory(before, tr("Resetar rotação do objeto"));
        emit ed.mapChanged();
        refreshObject();
    });
    return w;
}

MapObject* PropertiesPanel::selectedObject()
{
    LayerPtr l = ed.activeLayer();
    if (!l || l->type != LayerType::Object) return nullptr;
    for (MapObject& o : l->objects)
        if (o.id == ed.session.selectedObjectId) return &o;
    return nullptr;
}

void PropertiesPanel::refreshObject()
{
    MapObject* o = selectedObject();
    const bool has = o != nullptr;
    m_objName->setEnabled(has);
    m_objX->setEnabled(has); m_objY->setEnabled(has);
    m_objW->setEnabled(has); m_objH->setEnabled(has);
    m_objScaleX->setEnabled(has); m_objScaleY->setEnabled(has);
    m_objScaleFilter->setEnabled(has); m_objDepth->setEnabled(has);
    m_objRotation->setEnabled(has);
    m_objRotationFilter->setEnabled(has);
    if (!has) {
        m_objName->clear();
        m_objScaleX->setValue(100.0); m_objScaleY->setValue(100.0);
        m_objScaleFilter->setCurrentIndex(0); m_objDepth->setRange(0, 0); m_objDepth->setValue(0);
        m_objRotation->setValue(0.0);
        m_objRotationFilter->setCurrentIndex(0);
        m_objInfo->setText(tr("Nenhum objeto selecionado.\nUse a ferramenta Selecionar (V) numa camada de objetos."));
        return;
    }
    m_objName->setText(o->name);
    m_objX->setValue(o->x);
    m_objY->setValue(o->y);
    m_objW->setValue(o->w);
    m_objH->setValue(o->h);
    const QSizeF native = objectNativeSize(ed, *o);
    m_objScaleX->setValue(native.width() > 0.0 ? o->w * 100.0 / native.width() : 100.0);
    m_objScaleY->setValue(native.height() > 0.0 ? o->h * 100.0 / native.height() : 100.0);
    const int scaleFilterIndex = m_objScaleFilter->findData(o->scaleFilter);
    m_objScaleFilter->setCurrentIndex(scaleFilterIndex >= 0 ? scaleFilterIndex : 1);
    const LayerPtr layer = ed.activeLayer();
    int depth = 0;
    if (layer && layer->type == LayerType::Object) {
        m_objDepth->setRange(0, qMax(0, layer->objects.size() - 1));
        for (int i = 0; i < layer->objects.size(); ++i)
            if (layer->objects[i].id == o->id) { depth = i; break; }
    }
    m_objDepth->setValue(depth);
    m_objRotation->setValue(o->rotation);
    const int filterIndex = m_objRotationFilter->findData(o->rotationFilter);
    m_objRotationFilter->setCurrentIndex(filterIndex >= 0 ? filterIndex : 0);
    m_objInfo->setText(tr("Tiles no objeto: %1 (%2×%3)\nSelecionados: %4\nProfundidade: %5 (0 = fundo)\n"
                          "Arraste a alça circular para girar. Shift encaixa em 15°. [ e ] giram ±15°.")
                           .arg(o->tiles.size()).arg(o->stampW).arg(o->stampH)
                           .arg(ed.session.selectedObjectIds.size()).arg(depth));
}

// -------------------------------------------------------------- Aleatorio
QWidget* PropertiesPanel::buildRandomTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(8, 8, 8, 8);

    auto* poolSection = new CollapsibleSection(tr("Conjunto de tiles"),
        QStringLiteral("Inspector/Random/PoolExpanded"), true, w);
    auto* poolLayout = new QVBoxLayout(poolSection->contentWidget());
    auto* hint = new QLabel(tr("Segure <b>Ctrl</b> e clique (ou arraste) na paleta para adicionar "
                               "tiles ao pool. Com o modo Aleatório ligado, o pincel sorteia entre eles "
                               "respeitando a probabilidade de cada tile."), poolSection->contentWidget());
    hint->setWordWrap(true);
    hint->setProperty("uiRole", QStringLiteral("hint"));
    poolLayout->addWidget(hint);

    m_poolList = new QListWidget(poolSection->contentWidget());
    poolLayout->addWidget(m_poolList, 1);

    auto* btns = new QHBoxLayout;
    auto* clearBtn = new QPushButton(tr("Limpar conjunto"), poolSection->contentWidget());
    btns->addWidget(clearBtn);
    btns->addStretch(1);
    poolLayout->addLayout(btns);
    v->addWidget(poolSection);
    connect(clearBtn, &QPushButton::clicked, this, [this] { ed.clearRandomPool(); refreshRandom(); });

    auto* probBox = new CollapsibleSection(tr("Probabilidade do tile selecionado"),
        QStringLiteral("Inspector/Random/ProbabilityExpanded"), false, w);
    auto* pf = new QFormLayout(probBox->contentWidget());
    m_probTarget = new QLabel(probBox->contentWidget());
    m_probSpin = new QDoubleSpinBox(probBox->contentWidget());
    m_probSpin->setRange(0.0, 100.0);
    m_probSpin->setSingleStep(0.25);
    m_probSpin->setDecimals(2);
    m_probSpin->setToolTip(tr("Peso relativo no sorteio (igual ao Probability do Tiled). 1 = normal."));
    pf->addRow(tr("Tile"), m_probTarget);
    pf->addRow(tr("Peso"), m_probSpin);
    v->addWidget(probBox);

    connect(m_probSpin, &QDoubleSpinBox::editingFinished, this, [this] {
        if (m_updating || !ed.session.tsSel.valid()) return;
        ed.setTileProb(ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y, m_probSpin->value());
        emit statusMessage(tr("Probabilidade do tile (%1,%2) = %3")
                               .arg(ed.session.tsSel.x).arg(ed.session.tsSel.y).arg(m_probSpin->value()));
    });

    auto* scatterBox = new CollapsibleSection(tr("Dispersão"),
        QStringLiteral("Inspector/Random/ScatterExpanded"), false, w);
    auto* sf = new QFormLayout(scatterBox->contentWidget());
    m_scatterEnabled = new QCheckBox(tr("Usar densidade de dispersão"), scatterBox->contentWidget());
    m_scatterPercent = new QSpinBox(scatterBox->contentWidget());
    m_scatterPercent->setRange(0, 100);
    m_scatterPercent->setSuffix(QStringLiteral("%"));
    m_scatterPercent->setToolTip(tr("Chance de cada ponto do pincel receber um tile do conjunto. Não altera os pesos relativos dos tiles."));
    m_gridFree = new QCheckBox(tr("Posicionamento livre (fora da grade)"), scatterBox->contentWidget());
    m_gridFree->setToolTip(tr("Espalha o conjunto fora da grade em uma camada de objetos. Ideal para folhas, pedras, sujeira e decoração orgânica."));
    m_gridFreeJitter = new QSpinBox(scatterBox->contentWidget());
    m_gridFreeJitter->setRange(0, 100);
    m_gridFreeJitter->setSuffix(QStringLiteral("% tile"));
    m_gridFreeSpacing = new QSpinBox(scatterBox->contentWidget());
    m_gridFreeSpacing->setRange(1, 512);
    m_gridFreeSpacing->setSuffix(QStringLiteral(" px"));
    sf->addRow(m_scatterEnabled);
    sf->addRow(tr("Densidade:"), m_scatterPercent);
    sf->addRow(m_gridFree);
    sf->addRow(tr("Variação de posição:"), m_gridFreeJitter);
    sf->addRow(tr("Espaçamento mínimo:"), m_gridFreeSpacing);
    v->addWidget(scatterBox);

    connect(m_scatterEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return; ed.session.randomScattering = on; refreshRandom();
    });
    connect(m_scatterPercent, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_updating) ed.session.randomScatterPercent = v;
    });
    connect(m_gridFree, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return; ed.session.randomGridFree = on; refreshRandom();
        if (on) emit statusMessage(tr("Posicionamento livre ativo: o LUDO usa ou cria uma camada de objetos para a dispersão."));
    });
    connect(m_gridFreeJitter, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_updating) ed.session.randomGridFreeJitter = v;
    });
    connect(m_gridFreeSpacing, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_updating) ed.session.randomGridFreeSpacing = v;
    });
    return w;
}

QWidget* PropertiesPanel::buildPatternsTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(8, 8, 8, 8);
    auto* librarySection = new CollapsibleSection(tr("Biblioteca de padrões"),
        QStringLiteral("Inspector/Patterns/LibraryExpanded"), true, w);
    auto* libraryLayout = new QVBoxLayout(librarySection->contentWidget());

    auto* hint = new QLabel(tr("Padrões guardam composições reutilizáveis. Capture uma área com "
                               "<b>botão direito e arraste</b> ou selecione tiles na paleta. "
                               "<b>Clique em um padrão para selecioná-lo e já pintar no mapa.</b>"), librarySection->contentWidget());
    hint->setWordWrap(true);
    hint->setProperty("uiRole", QStringLiteral("hint"));
    libraryLayout->addWidget(hint);

    // Biblioteca à esquerda, preview persistente à direita. A seleção é a ação:
    // não há botão "Usar", seguindo o fluxo de paletas/patterns de editores de mapa.
    auto* browser = new QHBoxLayout;
    browser->setSpacing(8);

    m_patternList = new QListWidget(librarySection->contentWidget());
    m_patternList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_patternList->setMinimumWidth(130);
    m_patternList->setToolTip(tr("Clique para selecionar o padrão. As setas ↑/↓ também trocam o padrão ativo."));
    browser->addWidget(m_patternList, 3);

    auto* previewSide = new QWidget(librarySection->contentWidget());
    previewSide->setMinimumWidth(150);
    previewSide->setMaximumWidth(220);
    auto* previewLayout = new QVBoxLayout(previewSide);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(6);

    m_patternPreview = new QLabel(previewSide);
    m_patternPreview->setAlignment(Qt::AlignCenter);
    m_patternPreview->setMinimumSize(144, 144);
    m_patternPreview->setMaximumHeight(190);
    m_patternPreview->setFrameShape(QFrame::StyledPanel);
    m_patternPreview->setText(tr("Selecione um padrão"));
    m_patternPreview->setToolTip(tr("Prévia do padrão selecionado"));
    previewLayout->addWidget(m_patternPreview, 0, Qt::AlignTop);

    m_patternMeta = new QLabel(previewSide);
    m_patternMeta->setWordWrap(true);
    m_patternMeta->setProperty("uiRole", QStringLiteral("hint"));
    m_patternMeta->setText(tr("A prévia aparecerá aqui."));
    previewLayout->addWidget(m_patternMeta);
    previewLayout->addStretch(1);
    browser->addWidget(previewSide, 2);
    libraryLayout->addLayout(browser, 1);

    auto* row = new QHBoxLayout;
    auto* save = new QPushButton(tr("Salvar seleção…"), librarySection->contentWidget());
    m_patternRename = new QPushButton(tr("Renomear…"), librarySection->contentWidget());
    m_patternDelete = new QPushButton(tr("Excluir"), librarySection->contentWidget());
    row->addWidget(save);
    row->addStretch(1);
    row->addWidget(m_patternRename);
    row->addWidget(m_patternDelete);
    libraryLayout->addLayout(row);
    v->addWidget(librarySection);
    v->addStretch(1);

    connect(save, &QPushButton::clicked, this, [this] {
        const Stamp stamp = ed.currentStamp();
        if (!stamp.valid() && !(ed.activeLayer() && ed.activeLayer()->type == LayerType::Object)) {
            emit statusMessage(tr("Selecione tiles ou objetos antes de salvar um padrão."));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Novo padrão"), tr("Nome:"),
                                                    QLineEdit::Normal,
                                                    tr("Padrão %1").arg(ed.savedStamps.size() + 1), &ok).trimmed();
        if (!ok || name.isEmpty()) return;

        const QString newId = ed.savePattern(name, stamp);
        if (newId.isEmpty()) { emit statusMessage(tr("Não foi possível transformar a seleção em um padrão.")); return; }
        // patternsChanged() já reconstruiu a lista de forma síncrona. Selecionar
        // o item recém-criado e ativá-lo explicitamente evita depender de
        // currentItemChanged quando ele for o primeiro item da biblioteca.
        for (int row = 0; row < m_patternList->count(); ++row) {
            QListWidgetItem* item = m_patternList->item(row);
            if (item && item->data(Qt::UserRole).toString() == newId) {
                const QSignalBlocker blocker(m_patternList);
                m_patternList->setCurrentRow(row);
                break;
            }
        }
        updatePatternPreview();
        activateSelectedPattern(false);
        emit statusMessage(tr("Padrão '%1' salvo e selecionado.").arg(name));
    });

    connect(m_patternList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        activateSelectedPattern(true);
    });
    connect(m_patternList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) {
        updatePatternPreview();
        activateSelectedPattern(true);
    });

    connect(m_patternRename, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = m_patternList ? m_patternList->currentItem() : nullptr;
        if (!item) return;
        bool ok = false;
        const QString renamed = QInputDialog::getText(this, tr("Renomear padrão"), tr("Nome:"),
                                                       QLineEdit::Normal, item->text(), &ok).trimmed();
        if (!ok || renamed.isEmpty() || renamed == item->text()) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (ed.renamePattern(id, renamed)) {
            emit statusMessage(tr("Padrão renomeado para '%1'.").arg(renamed));
            refreshPatterns();
        }
    });

    connect(m_patternDelete, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = m_patternList ? m_patternList->currentItem() : nullptr;
        if (!item) return;
        const int oldRow = m_patternList->currentRow();
        const QString id = item->data(Qt::UserRole).toString();
        const QString name = item->text();
        ed.removePattern(id); // patternsChanged() atualiza lista + preview

        if (m_patternList->count() > 0) {
            const int nextRow = qBound(0, oldRow, m_patternList->count() - 1);
            {
                const QSignalBlocker blocker(m_patternList);
                m_patternList->setCurrentRow(nextRow);
            }
            updatePatternPreview();
            activateSelectedPattern(false);
        } else {
            ed.session.customStamp.clear();
            emit ed.selectionChanged();
            updatePatternPreview();
        }
        emit statusMessage(tr("Padrão '%1' excluído.").arg(name));
    });
    return w;
}

void PropertiesPanel::refreshRandom()
{
    m_poolList->clear();
    for (const RandomEntry& e : ed.randomPool) {
        const Tileset* ts = ed.tilesetAt(e.tilesetIdx);
        m_poolList->addItem(tr("%1 — (%2,%3) %4×%5 · tiles: %6")
                                .arg(ts ? ts->name : tr("tileset ?"))
                                .arg(e.x).arg(e.y).arg(e.w).arg(e.h).arg(e.tiles.size()));
    }
    if (ed.randomPool.isEmpty()) m_poolList->addItem(tr("(conjunto vazio)"));
    if (ed.session.tsSel.valid()) {
        m_probTarget->setText(tr("(%1, %2)").arg(ed.session.tsSel.x).arg(ed.session.tsSel.y));
        m_probSpin->setValue(ed.tileProb(ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y));
        m_probSpin->setEnabled(true);
    } else {
        m_probTarget->setText(tr("—"));
        m_probSpin->setEnabled(false);
    }
    if (m_scatterEnabled) m_scatterEnabled->setChecked(ed.session.randomScattering);
    if (m_scatterPercent) { m_scatterPercent->setValue(ed.session.randomScatterPercent); m_scatterPercent->setEnabled(ed.session.randomScattering); }
    if (m_gridFree) m_gridFree->setChecked(ed.session.randomGridFree);
    if (m_gridFreeJitter) { m_gridFreeJitter->setValue(ed.session.randomGridFreeJitter); m_gridFreeJitter->setEnabled(ed.session.randomGridFree); }
    if (m_gridFreeSpacing) { m_gridFreeSpacing->setValue(ed.session.randomGridFreeSpacing); m_gridFreeSpacing->setEnabled(ed.session.randomGridFree); }
}

void PropertiesPanel::refreshPatterns()
{
    if (!m_patternList || m_patternSelectionSyncing) return;
    bool sameItems = m_patternList->count() == ed.savedStamps.size();
    if (sameItems) for (int i = 0; i < ed.savedStamps.size(); ++i) {
        const auto* item = m_patternList->item(i);
        if (item->data(Qt::UserRole).toString() != ed.savedStamps[i].id || item->text() != ed.savedStamps[i].name) { sameItems = false; break; }
    }
    if (sameItems) { updatePatternPreview(); return; }

    const QString selectedId = m_patternList->currentItem()
        ? m_patternList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    // Rebuild de lista não é uma intenção do usuário. Bloquear os sinais evita
    // que um refresh de tileset/mapa troque o Pattern ativo acidentalmente.
    const QSignalBlocker blocker(m_patternList);
    m_patternSelectionSyncing = true;
    m_patternList->clear();

    int selectRow = -1;
    for (const SavedStamp& saved : ed.savedStamps) {
        auto* item = new QListWidgetItem(saved.name, m_patternList);
        item->setData(Qt::UserRole, saved.id);
        item->setToolTip(tr("%1 × %2 · %3 tile(s)\nClique para selecionar e pintar.")
                             .arg(saved.stamp.w).arg(saved.stamp.h).arg(saved.stamp.tiles.size()));
        if (saved.id == selectedId) selectRow = m_patternList->count() - 1;
    }

    if (selectRow >= 0) m_patternList->setCurrentRow(selectRow);
    m_patternSelectionSyncing = false;

    const bool has = m_patternList->currentItem() != nullptr;
    if (m_patternRename) m_patternRename->setEnabled(has);
    if (m_patternDelete) m_patternDelete->setEnabled(has);
    updatePatternPreview();
}

void PropertiesPanel::updatePatternPreview()
{
    if (!m_patternPreview || !m_patternMeta || !m_patternList) return;

    QListWidgetItem* item = m_patternList->currentItem();
    if (!item) {
        m_patternPreview->setPixmap(QPixmap());
        m_patternPreview->setText(tr("Nenhum padrão"));
        m_patternMeta->setText(tr("Salve uma seleção para começar sua biblioteca."));
        return;
    }

    const QString id = item->data(Qt::UserRole).toString();
    const SavedStamp* selected = nullptr;
    for (const SavedStamp& saved : ed.savedStamps) {
        if (saved.id == id) { selected = &saved; break; }
    }
    if (!selected) {
        m_patternPreview->setPixmap(QPixmap());
        m_patternPreview->setText(tr("Padrão indisponível"));
        m_patternMeta->setText(QString());
        return;
    }

    bool fullyValid = false;
    const QSize previewSize(qMax(144, m_patternPreview->width()),
                            qMax(144, m_patternPreview->height()));
    const QPixmap preview = patternPreviewPixmap(ed, *selected, previewSize, &fullyValid);
    m_patternPreview->setText(QString());
    m_patternPreview->setPixmap(preview);
    m_patternMeta->setText(fullyValid
        ? tr("<b>%1</b><br>%2 × %3 · %4 tile(s)<br><span style='color:#8fd18f'>Selecionado para pintar</span>")
              .arg(selected->name.toHtmlEscaped())
              .arg(selected->stamp.w).arg(selected->stamp.h).arg(selected->stamp.tiles.size())
        : tr("<b>%1</b><br>%2 × %3 · %4 tile(s)<br><span style='color:#e57373'>Referência de tile ausente</span>")
              .arg(selected->name.toHtmlEscaped())
              .arg(selected->stamp.w).arg(selected->stamp.h).arg(selected->stamp.tiles.size()));
}

void PropertiesPanel::activateSelectedPattern(bool announce)
{
    if (!m_patternList || m_patternSelectionSyncing || m_updating) return;
    QListWidgetItem* item = m_patternList->currentItem();
    if (!item) return;

    // Copiar antes de applyPattern(): selectionChanged() é síncrono e pode
    // reconstruir a lista durante o refresh do Inspector.
    const QString id = item->data(Qt::UserRole).toString();
    const QString name = item->text();

    m_patternSelectionSyncing = true;
    const bool applied = ed.applyPattern(id);
    m_patternSelectionSyncing = false;

    if (applied) {
        // applyPattern muda o contexto de autoria para Mapa; mantemos a aba
        // Patterns visível para que trocar de padrão continue sendo instantâneo.
        constexpr int patternsTab = 5;
        if (m_tabs && m_tabs->count() > patternsTab) {
            m_tabs->setCurrentIndex(patternsTab);
            if (m_stack) m_stack->setCurrentIndex(patternsTab);
        }
        if (announce)
            emit statusMessage(tr("Padrão '%1' selecionado — clique no mapa para pintar.").arg(name));
    } else if (announce) {
        emit statusMessage(tr("O padrão '%1' usa tiles que não estão mais disponíveis.").arg(name));
    }
    updatePatternPreview();
}

// ------------------------------------------------------------------- Wang
void PropertiesPanel::syncContextTab()
{
    if (!m_tabs || ed.session.inspectorPinned) return;
    int target = 0;
    if (ed.session.authoringContext == AuthoringContext::Pattern) target = 5;
    else if (!ed.session.selectedObjectId.isEmpty() || !ed.session.selectedObjectIds.isEmpty()) target = 3;
    else {
        switch (ed.session.authoringContext) {
        case AuthoringContext::Layer:   target = 1; break;
        case AuthoringContext::Tileset: target = 2; break;
        case AuthoringContext::Object:  target = 3; break;
        case AuthoringContext::Region:  target = 0; break; // Regiões pertencem ao mapa RPG Maker
        case AuthoringContext::Map:
        default:                        target = 0; break;
        }
    }
    if (m_tabs->currentIndex() != target) m_tabs->setCurrentIndex(target);
}

void PropertiesPanel::scheduleRefresh()
{
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshQueued = false;
        if (m_updating) { scheduleRefresh(); return; }
        refresh();
    });
}

void PropertiesPanel::refresh()
{
    if (m_updating) { scheduleRefresh(); return; }
    m_refreshQueued = false;
    m_updating = true;
    refreshMap();
    refreshLayer();
    refreshTileset();
    refreshObject();
    refreshRandom();
    refreshPatterns();
    m_updating = false;
    syncContextTab();
}

} // namespace ui
