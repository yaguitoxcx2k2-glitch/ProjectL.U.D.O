#include "TilesetManagerDialog.h"
#include "TilesetView.h"
#include "WangEditorWidget.h"

#include "AssetBrowser.h"
#include "MapAuthoringDialogs.h"
#include "Icons.h"
#include "core/Editor.h"
#include "core/TilesetOps.h"
#include "core/TilesetCatalog.h"
#include "core/AutoTileTables.h"
#include "core/ProjectReferenceIndex.h"
#include "core/ProjectIO.h"
#include "core/Renderer.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QButtonGroup>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace ui {
using namespace core;

namespace {

constexpr int kPortableTilesetTextureLimit = 4096;
constexpr int kTilesetNodeKindRole = Qt::UserRole + 1;
constexpr int kTilesetLogicalNode = 0;
constexpr int kTilesetPageNode = 1;

bool enforcePortableTilesetSize(QWidget* parent, Tileset* ts)
{
    if (!ts || ts->image.isNull()) return true;
    if (ts->image.width() <= kPortableTilesetTextureLimit && ts->image.height() <= kPortableTilesetTextureLimit) return true;

    QMessageBox box(QMessageBox::Warning, QObject::tr("Tileset acima do limite portátil"),
        QObject::tr("O tileset possui %1×%2 px. A LUDO usa %3×%3 px como limite portátil seguro para garantir compatibilidade entre GPUs.\n\nVocê pode cortar somente a área excedente da direita/baixo, manter o original por sua conta e risco ou cancelar.")
            .arg(ts->image.width()).arg(ts->image.height()).arg(kPortableTilesetTextureLimit),
        QMessageBox::NoButton, parent);
    QPushButton* crop = box.addButton(QObject::tr("Cortar para o limite"), QMessageBox::AcceptRole);
    QPushButton* keep = box.addButton(QObject::tr("Manter original"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == keep) return true;
    if (box.clickedButton() != crop) return false;

    const int usableW = qMax(ts->tilewidth, (kPortableTilesetTextureLimit / qMax(1, ts->tilewidth)) * ts->tilewidth);
    const int usableH = qMax(ts->tileheight, (kPortableTilesetTextureLimit / qMax(1, ts->tileheight)) * ts->tileheight);
    ts->image = ts->image.copy(0, 0, qMin(usableW, ts->image.width()), qMin(usableH, ts->image.height()));
    ts->recomputeGrid();
    // Metadados que ficaram fora da área cortada não podem continuar apontando
    // para tiles inexistentes. Reutiliza a própria geometria física do atlas.
    auto outsideGrid = [ts](const QString& key) {
        const QStringList parts = key.split(QLatin1Char(':'));
        if (parts.size() != 2) return true;
        bool okX = false, okY = false;
        const int tx = parts.at(0).toInt(&okX), ty = parts.at(1).toInt(&okY);
        return !okX || !okY || tx < 0 || ty < 0 || tx >= ts->columns || ty >= ts->rows;
    };
    for (auto it = ts->tilePriorities.begin(); it != ts->tilePriorities.end();)
        if (outsideGrid(it.key())) it = ts->tilePriorities.erase(it); else ++it;
    for (auto it = ts->tileCollisionMasks.begin(); it != ts->tileCollisionMasks.end();)
        if (outsideGrid(it.key())) it = ts->tileCollisionMasks.erase(it); else ++it;
    for (auto it = ts->tileProbabilities.begin(); it != ts->tileProbabilities.end();)
        if (outsideGrid(it.key())) it = ts->tileProbabilities.erase(it); else ++it;
    for (auto it = ts->tileResourceEffects.begin(); it != ts->tileResourceEffects.end();)
        if (outsideGrid(it.key())) it = ts->tileResourceEffects.erase(it); else ++it;
    QVector<AnimatedAutotile> kept;
    for (const AnimatedAutotile& a : ts->animatedAutotiles) {
        bool fits = true;
        for (const QPoint& o : a.frameOrigins) if (o.x() + a.cols > ts->columns || o.y() + a.rows > ts->rows) { fits = false; break; }
        if (fits) kept.push_back(a);
    }
    ts->animatedAutotiles = kept;
    return true;
}

QImage compactAnimatedPalettePreview(const Tileset& ts)
{
    if (ts.image.isNull() || ts.animatedAutotiles.isEmpty()) return ts.image;
    QVector<QPoint> visible;
    visible.reserve(ts.tilecount);
    for(int ty=0;ty<ts.rows;++ty)for(int tx=0;tx<ts.columns;++tx){
        int frame=-1;animatedAutotileAt(ts,tx,ty,true,&frame,nullptr);
        if(frame<=0)visible.push_back(QPoint(tx,ty));
    }
    if(visible.size()>=ts.tilecount)return ts.image;
    const int columns=qMax(1,ts.columns);
    const int rows=qMax(1,(visible.size()+columns-1)/columns);
    const int width=ts.margin*2+columns*ts.tilewidth+qMax(0,columns-1)*ts.spacing;
    const int height=ts.margin*2+rows*ts.tileheight+qMax(0,rows-1)*ts.spacing;
    QImage out(width,height,QImage::Format_ARGB32_Premultiplied);out.fill(Qt::transparent);
    QPainter painter(&out);painter.setRenderHint(QPainter::SmoothPixmapTransform,false);
    for(int i=0;i<visible.size();++i){
        const QPoint srcTile=visible.at(i);const int vx=i%columns,vy=i/columns;
        const QRect dst(ts.margin+vx*(ts.tilewidth+ts.spacing),ts.margin+vy*(ts.tileheight+ts.spacing),ts.tilewidth,ts.tileheight);
        painter.drawImage(dst,ts.image,ts.tileRect(srcTile.x(),srcTile.y()));
    }
    return out;
}


QImage autotileResourcePreview(const Editor& ed, int tilesetIdx,
                               const TilesetAutotile& autotile)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || ts->image.isNull()) return QImage();
    const QVector<QPoint> tiles = tilesetAutotileTiles(ed, tilesetIdx, autotile);
    const QRect bounds = tilesetAutotileBounds(ed, tilesetIdx, autotile);
    if (tiles.isEmpty() || bounds.isEmpty()) return QImage();

    const int width = bounds.width() * ts->tilewidth;
    const int height = bounds.height() * ts->tileheight;
    QImage out(qMax(1, width), qMax(1, height), QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(24, 24, 24));
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const QPoint& tile : tiles) {
        const QRect dst((tile.x() - bounds.left()) * ts->tilewidth,
                        (tile.y() - bounds.top()) * ts->tileheight,
                        ts->tilewidth, ts->tileheight);
        painter.drawImage(dst, ts->image, ts->tileRect(tile.x(), tile.y()));
        painter.setPen(QColor(255, 255, 255, 45));
        painter.drawRect(dst.adjusted(0, 0, -1, -1));
    }
    return out;
}

QString autotileKindLabel(const TilesetAutotile& autotile)
{
    QStringList kinds;
    if (autotile.hasTerrain()) kinds << QObject::tr("Conexões do Autotile");
    if (autotile.animated()) kinds << QObject::tr("Animado");
    if (autotile.hasRegion() && !autotile.hasTerrain()) kinds << QObject::tr("Região física");
    return kinds.isEmpty() ? QObject::tr("Autotile") : kinds.join(QStringLiteral(" + "));
}

void collectLayerUsage(const QVector<LayerPtr>& layers, int tilesetIdx, bool* used)
{
    if (!used || *used) return;
    for (const LayerPtr& layer : layers) {
        if (!layer) continue;
        if (layer->type == LayerType::Tile) {
            for (const auto& row : layer->data2D)
                for (const Cell& cell : row)
                    for (const TileRef& tile : cell)
                        if (tile.tilesetIdx == tilesetIdx) { *used = true; return; }
        } else if (layer->type == LayerType::Object) {
            for (const MapObject& object : layer->objects)
                for (const TileRef& tile : object.tiles)
                    if (tile.tilesetIdx == tilesetIdx) { *used = true; return; }
        }
        if (!layer->children.isEmpty()) collectLayerUsage(layer->children, tilesetIdx, used);
        if (*used) return;
    }
}

bool hasVisiblePixels(const QImage& source, const QRect& rect)
{
    const QRect clipped=rect.intersected(source.rect());
    if(clipped.isEmpty())return false;
    const QImage img=source.convertToFormat(QImage::Format_ARGB32);
    for(int y=clipped.top();y<=clipped.bottom();++y){
        const QRgb* line=reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for(int x=clipped.left();x<=clipped.right();++x)if(qAlpha(line[x])>=8)return true;
    }
    return false;
}

void collectRemovedTileUsage(const QVector<LayerPtr>& layers,int tilesetIdx,int cols,int rows,QStringList* out)
{
    if(!out)return;
    for(const LayerPtr& layer:layers){
        if(!layer)continue;
        if(layer->type==LayerType::Tile){
            for(const auto& line:layer->data2D)for(const Cell& cell:line)for(const TileRef& t:cell)
                if(t.tilesetIdx==tilesetIdx&&(t.tx>=cols||t.ty>=rows)){out->push_back(QObject::tr("tile pintado em camada %1").arg(layer->name));return;}
        }else if(layer->type==LayerType::Object){
            for(const MapObject& obj:layer->objects)for(const TileRef& t:obj.tiles)
                if(t.tilesetIdx==tilesetIdx&&(t.tx>=cols||t.ty>=rows)){out->push_back(QObject::tr("objeto em camada %1").arg(layer->name));return;}
        }
        collectRemovedTileUsage(layer->children,tilesetIdx,cols,rows,out);
        if(out->size()>12)return;
    }
}

QStringList referencesOutsideCrop(const Editor& ed,int tilesetIdx,int cols,int rows)
{
    QStringList out;
    for(const MapDoc& doc:ed.docs){
        const int before=out.size();
        collectRemovedTileUsage(doc.layers,tilesetIdx,cols,rows,&out);
        if(out.size()>before&&out.last().startsWith(QStringLiteral("tile pintado")))
            out.last()+=QObject::tr(" · mapa %1").arg(doc.name);
        if(out.size()>12)break;
    }
    return out;
}


} // namespace

TilesetManagerDialog::TilesetManagerDialog(Editor& editorRef, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Gerenciador de Tilesets 2.0"));
    resize(1280, 760);
    setMinimumSize(980, 620);

    auto* outer = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Gerencie Tilesets e Autotiles do projeto."), this);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);

    // ---------------------------------------------------------------- Biblioteca
    auto* library = new QWidget(this);
    library->setMinimumWidth(250);
    library->setMaximumWidth(330);
    auto* libraryLayout = new QVBoxLayout(library);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    auto* libraryTitle = new QLabel(tr("<b>Biblioteca</b>"), library);
    libraryLayout->addWidget(libraryTitle);

    libraryLayout->addWidget(new QLabel(tr("<b>Tilesets</b>"), library));
    m_list = new QTreeWidget(library);
    m_list->setObjectName(QStringLiteral("tilesetLibrary"));
    m_list->setAccessibleName(tr("Biblioteca de tilesets e páginas"));
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(true);
    m_list->setItemsExpandable(true);
    m_list->setExpandsOnDoubleClick(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setMinimumHeight(150);
    libraryLayout->addWidget(m_list, 1);

    // Ações visíveis = tarefas frequentes. O usuário escolhe o Tileset na
    // árvore e usa uma única porta para adicionar imagens. Manutenção de página
    // fica em "Mais ações".
    auto* create = new QPushButton(icons::get(QStringLiteral("tileset-new")), tr("Novo"), library);
    create->setToolTip(tr("Cria um novo Tileset. Se o nome já existir, o editor pergunta o que fazer em vez de juntar páginas silenciosamente."));
    auto* addImage = new QPushButton(icons::get(QStringLiteral("import")), tr("Adicionar imagem"), library);
    addImage->setToolTip(tr("Adiciona uma imagem ao Tileset selecionado, como nova página ou dentro de uma página existente."));
    auto* rename = new QPushButton(tr("Renomear"), library);
    rename->setToolTip(tr("Renomeia o Tileset inteiro, incluindo todas as páginas."));
    auto* remove = new QPushButton(icons::get(QStringLiteral("remove")), tr("Excluir"), library);
    remove->setToolTip(tr("Exclui o Tileset inteiro e todas as suas páginas."));
    auto* more = new QPushButton(tr("⋯  Mais ações"), library);

    auto* moreMenu = new QMenu(more);
    QAction* duplicateAction = moreMenu->addAction(icons::get(QStringLiteral("duplicate")), tr("Duplicar Tileset"));
    moreMenu->addSeparator();
    QAction* updatePageAction = moreMenu->addAction(icons::get(QStringLiteral("refresh")), tr("Atualizar imagem de origem…"));
    updatePageAction->setToolTip(tr("Substitui uma imagem já vinculada à página selecionada sem mudar a posição dos tiles."));
    QAction* deletePageAction = moreMenu->addAction(icons::get(QStringLiteral("remove")), tr("Excluir página atual"));
    moreMenu->addSeparator();
    QAction* reduceAction = moreMenu->addAction(tr("Ajustar página ao conteúdo…"));
    reduceAction->setToolTip(tr("Remove apenas colunas da direita e linhas de baixo que não são necessárias."));
    more->setMenu(moreMenu);

    auto* libraryRow1 = new QHBoxLayout;
    libraryRow1->addWidget(create);
    libraryRow1->addWidget(addImage);
    libraryLayout->addLayout(libraryRow1);
    auto* libraryRow2 = new QHBoxLayout;
    libraryRow2->addWidget(rename);
    libraryRow2->addWidget(remove);
    libraryLayout->addLayout(libraryRow2);
    libraryLayout->addWidget(more);

    connect(moreMenu, &QMenu::aboutToShow, this, [this, duplicateAction, updatePageAction, deletePageAction, reduceAction] {
        const int row = selectedTilesetIndex();
        const Tileset* ts = ed.tilesetAt(row);
        const bool valid = ts != nullptr;
        const bool pageNode = selectedTilesetIsPage();
        duplicateAction->setEnabled(valid);
        updatePageAction->setEnabled(valid && pageNode);
        reduceAction->setEnabled(valid && pageNode);
        deletePageAction->setEnabled(valid && pageNode && core::tilesetPageIndices(ed, row).size() > 1);
    });

    auto* autotileTitle = new QLabel(tr("<b>Autotiles do projeto</b>"), library);
    autotileTitle->setToolTip(tr("Autotiles disponíveis no projeto."));
    libraryLayout->addWidget(autotileTitle);
    m_autotileList = new QListWidget(library);
    m_autotileList->setObjectName(QStringLiteral("autotileResourceLibrary"));
    m_autotileList->setAccessibleName(tr("Autotiles do projeto"));
    m_autotileList->setMinimumHeight(135);
    m_autotileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    libraryLayout->addWidget(m_autotileList, 1);

    // Contexto de Tilesets: toda ação é aplicada ao conjunto lógico, mesmo
    // quando o usuário seleciona uma página filha.
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_list->selectedItems().isEmpty()) return;
        QMenu menu(m_list);
        QAction* categorize = menu.addAction(tr("Definir categoria dos selecionados…"));
        QSet<QString> groups;
        bool allShown = true;
        for (QTreeWidgetItem* item : m_list->selectedItems()) {
            const Tileset* ts = ed.tilesetAt(item->data(0, Qt::UserRole).toInt());
            if (!ts) continue;
            groups.insert(core::tilesetPageGroupKey(*ts));
            if (!ts->paletteVisible) allShown = false;
        }
        menu.addSeparator();
        QAction* paletteAction = menu.addAction(allShown
            ? tr("Ocultar da paleta principal")
            : tr("Mostrar na paleta principal"));
        paletteAction->setToolTip(allShown
            ? tr("Mantém o Tileset no projeto e neste Gerenciador, mas o remove da lista usada para pintar mapas.")
            : tr("Volta a disponibilizar o Tileset na paleta usada para pintar mapas."));

        QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == paletteAction) {
            const bool show = !allShown;
            for (Tileset& ts : ed.tilesets)
                if (groups.contains(core::tilesetPageGroupKey(ts))) ts.paletteVisible = show;
            ed.markDirty();
            emit ed.tilesetsChanged();
            refresh();
            return;
        }
        if (chosen != categorize) return;

        QStringList categories;
        for (const Tileset& ts : ed.tilesets)
            if (!ts.category.isEmpty() && !categories.contains(ts.category)) categories << ts.category;
        for (const TilesetAutotile& a : ed.autotiles)
            if (!a.category.isEmpty() && !categories.contains(a.category)) categories << a.category;
        categories.sort(Qt::CaseInsensitive);
        categories.prepend(QString());
        bool ok = false;
        const QString category = QInputDialog::getItem(this, tr("Organizar categoria"),
            tr("Categoria para %1 Tileset(s) (vazio remove):").arg(groups.size()), categories, 0, true, &ok).trimmed().left(128);
        if (!ok) return;
        for (Tileset& ts : ed.tilesets)
            if (groups.contains(core::tilesetPageGroupKey(ts))) ts.category = category;
        ed.markDirty();
        emit ed.tilesetsChanged();
        emit ed.wangChanged();
        refresh();
    });

    // Autotiles continuam em lista simples; aqui a categoria pertence ao recurso
    // individual, não a um conjunto de páginas.
    m_autotileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_autotileList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (m_autotileList->selectedItems().isEmpty()) return;
        QMenu menu(m_autotileList);
        QAction* categorize = menu.addAction(tr("Definir categoria dos selecionados…"));
        QAction* chosen = menu.exec(m_autotileList->viewport()->mapToGlobal(pos));
        if (chosen != categorize) return;

        QStringList ids, categories;
        for (QListWidgetItem* item : m_autotileList->selectedItems()) ids << item->data(Qt::UserRole).toString();
        for (const Tileset& ts : ed.tilesets)
            if (!ts.category.isEmpty() && !categories.contains(ts.category)) categories << ts.category;
        for (const TilesetAutotile& a : ed.autotiles)
            if (!a.category.isEmpty() && !categories.contains(a.category)) categories << a.category;
        categories.sort(Qt::CaseInsensitive);
        categories.prepend(QString());
        bool ok = false;
        const QString category = QInputDialog::getItem(this, tr("Organizar categoria"),
            tr("Categoria para %1 recursos (vazio remove):").arg(ids.size()), categories, 0, true, &ok).trimmed().left(128);
        if (!ok) return;
        for (TilesetAutotile& a : ed.autotiles) if (ids.contains(a.id)) a.category = category;
        ed.markDirty();
        emit ed.tilesetsChanged();
        emit ed.wangChanged();
        refresh();
    });


    auto* autotileImport = new QPushButton(icons::get(QStringLiteral("import")), tr("Importar"), library);
    auto* autotileImportMenu = new QMenu(autotileImport);
    QAction* importAutotileAction = autotileImportMenu->addAction(tr("Importar Autotile…"));
    QAction* importA1Action = autotileImportMenu->addAction(tr("Importar A1 animado…"));
    importA1Action->setToolTip(tr("Cria autotiles animados a partir de uma folha A1 de 16×12 células."));
    autotileImport->setMenu(autotileImportMenu);

    m_renameAutotile = new QPushButton(tr("Renomear"), library);
    m_deleteAutotile = new QPushButton(icons::get(QStringLiteral("remove")), tr("Excluir"), library);
    auto* autotileMore = new QPushButton(tr("⋯"), library);
    autotileMore->setToolTip(tr("Mais ações do Autotile selecionado."));
    auto* autotileMoreMenu = new QMenu(autotileMore);
    m_editAutotileAnimationAction = autotileMoreMenu->addAction(icons::get(QStringLiteral("effects")), tr("Editar animação…"));
    m_editAutotileAnimationAction->setToolTip(tr("Ajusta a velocidade e o modo de reprodução da animação deste Autotile."));
    autotileMore->setMenu(autotileMoreMenu);

    auto* autotileRow1 = new QHBoxLayout;
    autotileRow1->addWidget(autotileImport);
    autotileRow1->addWidget(m_renameAutotile);
    libraryLayout->addLayout(autotileRow1);
    auto* autotileRow2 = new QHBoxLayout;
    autotileRow2->addWidget(m_deleteAutotile);
    autotileRow2->addWidget(autotileMore);
    libraryLayout->addLayout(autotileRow2);
    body->addWidget(library);

    // ---------------------------------------------------------------- Área de trabalho
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("tilesetWorkspaceTabs"));
    m_tabs->setAccessibleName(tr("Área de trabalho do Gerenciador de Tilesets"));
    body->addWidget(m_tabs, 1);

    auto populateReflectionPresets = [this](QComboBox* combo) {
        combo->addItem(tr("Sem reflexo"), QString());
        combo->addItem(tr("Água parada"), QStringLiteral("still"));
        combo->addItem(tr("Água suja"), QStringLiteral("dirty"));
        combo->addItem(tr("Água agitada"), QStringLiteral("rough"));
        combo->addItem(tr("Poça"), QStringLiteral("puddle"));
        combo->addItem(tr("Piso molhado"), QStringLiteral("wet"));
        combo->addItem(tr("Espelho / superfície limpa"), QStringLiteral("mirror"));
    };

    auto* previewTab = new QWidget(m_tabs);
    auto* previewLayout = new QVBoxLayout(previewTab);
    previewLayout->setContentsMargins(6, 6, 6, 6);
    auto* previewHint = new QLabel(tr(
        "<b>Escala 1:1</b> — selecione um Tileset ou Autotile e use as barras de rolagem para navegar."), previewTab);
    previewHint->setWordWrap(true);
    previewLayout->addWidget(previewHint);
    auto* previewScroll = new QScrollArea(previewTab);
    previewScroll->setObjectName(QStringLiteral("tilesetOneToOnePreview"));
    previewScroll->setAccessibleName(tr("Prévia do tileset em escala 1 para 1"));
    previewScroll->setWidgetResizable(false);
    previewScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_preview = new QLabel(previewScroll);
    m_preview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_preview->setMinimumSize(1, 1);
    m_preview->setStyleSheet(QStringLiteral("background:#181818"));
    previewScroll->setWidget(m_preview);
    previewLayout->addWidget(previewScroll, 1);
    m_tabs->addTab(previewTab, tr("Prévia 1:1"));

    // A prévia do Autotile usa a própria aba Prévia 1:1. Isso evita duas
    // visualizações concorrentes e mantém o fluxo: selecionar -> inspecionar -> editar.

    // ------------------------------------------------ Conexões do Autotile contextual ao Autotile
    auto* terrainTab = new QWidget(m_tabs);
    auto* terrainLayout = new QHBoxLayout(terrainTab);
    terrainLayout->setContentsMargins(6, 6, 6, 6);

    auto* terrainLeft = new QWidget(terrainTab);
    auto* terrainLeftLayout = new QVBoxLayout(terrainLeft);
    terrainLeftLayout->setContentsMargins(0, 0, 0, 0);
    auto* terrainHint = new QLabel(tr("Configure como este Autotile se conecta aos vizinhos."), terrainLeft);
    terrainHint->setWordWrap(true);
    terrainLeftLayout->addWidget(terrainHint);
    auto* terrainScroll = new QScrollArea(terrainLeft);
    terrainScroll->setObjectName(QStringLiteral("autotileTerrainPalette"));
    terrainScroll->setAccessibleName(tr("Paleta de conexões do Autotile selecionado"));
    terrainScroll->setWidgetResizable(false);
    m_terrainView = new TilesetView(ed, terrainScroll);
    m_terrainView->setObjectName(QStringLiteral("autotileTerrainView"));
    m_terrainView->setWangOverlay(false);
    terrainScroll->setWidget(m_terrainView);
    terrainLeftLayout->addWidget(terrainScroll, 1);
    m_terrainStatus = new QLabel(tr("Selecione um Autotile para escolher como ele se conecta aos tiles vizinhos."), terrainLeft);
    m_terrainStatus->setObjectName(QStringLiteral("autotileTerrainStatus"));
    m_terrainStatus->setWordWrap(true);
    terrainLeftLayout->addWidget(m_terrainStatus);
    terrainLayout->addWidget(terrainLeft, 1);

    auto* terrainEditorScroll = new QScrollArea(terrainTab);
    terrainEditorScroll->setWidgetResizable(true);
    terrainEditorScroll->setMinimumWidth(330);
    terrainEditorScroll->setMaximumWidth(420);
    m_terrainEditor = new WangEditorWidget(ed, terrainEditorScroll);
    terrainEditorScroll->setWidget(m_terrainEditor);
    terrainLayout->addWidget(terrainEditorScroll);
    m_terrainTabIndex = m_tabs->addTab(terrainTab, tr("Conexões"));
    m_tabs->setTabEnabled(m_terrainTabIndex, false);
    m_tabs->setTabVisible(m_terrainTabIndex, false);

    connect(m_terrainEditor, &WangEditorWidget::editModeChanged, this, [this](bool on) {
        if (m_terrainView) m_terrainView->setWangOverlay(on);
    });
    connect(m_terrainEditor, &WangEditorWidget::statusMessage, this, [this](const QString& text) {
        if (m_terrainStatus) m_terrainStatus->setText(text);
    });
    connect(m_terrainView, &TilesetView::wangTileClicked, this,
            [this](int tilesetIdx, int tx, int ty, const QString& position) {
        if (m_terrainEditor) m_terrainEditor->applyLabel(tilesetIdx, tx, ty, position);
    });

    // ------------------------------------------------ Prioridade + colisão no mesmo contexto
    auto* priorityCollisionTab = new QWidget(m_tabs);
    auto* priorityCollisionLayout = new QHBoxLayout(priorityCollisionTab);
    priorityCollisionLayout->setContentsMargins(6, 6, 6, 6);

    auto* priorityScroll = new QScrollArea(priorityCollisionTab);
    priorityScroll->setWidgetResizable(false);
    m_priorityView = new TilesetView(ed, priorityScroll);
    m_priorityView->setObjectName(QStringLiteral("tilesetPriorityCollisionView"));
    m_priorityView->setPriorityMode(true);
    m_priorityView->setCollisionMode(false);
    m_priorityView->setPriorityPaintValue(-1);
    priorityScroll->setWidget(m_priorityView);
    priorityCollisionLayout->addWidget(priorityScroll, 1);

    auto* tools = new QWidget(priorityCollisionTab);
    tools->setMinimumWidth(235);
    tools->setMaximumWidth(300);
    auto* toolsLayout = new QVBoxLayout(tools);
    toolsLayout->setContentsMargins(6, 0, 0, 0);
    auto* modeTitle = new QLabel(tr("<b>Editar</b>"), tools);
    toolsLayout->addWidget(modeTitle);
    m_priorityCollisionMode = new QComboBox(tools);
    m_priorityCollisionMode->setObjectName(QStringLiteral("priorityCollisionMode"));
    m_priorityCollisionMode->setAccessibleName(tr("Modo de edição de prioridade ou colisão"));
    m_priorityCollisionMode->addItem(tr("Prioridade"), 0);
    m_priorityCollisionMode->addItem(tr("Colisão detalhada"), 1);
    toolsLayout->addWidget(m_priorityCollisionMode);

    m_priorityControls = new QWidget(tools);
    auto* priorityTools = new QVBoxLayout(m_priorityControls);
    priorityTools->setContentsMargins(0, 6, 0, 0);
    auto* priorityHelp = new QLabel(tr("Escolha a prioridade e pinte os tiles."), m_priorityControls);
    priorityHelp->setWordWrap(true);
    priorityTools->addWidget(priorityHelp);
    auto* priorityButtons = new QButtonGroup(m_priorityControls);
    priorityButtons->setExclusive(true);
    auto* cycle = new QPushButton(tr("Ciclar no clique"), m_priorityControls);
    cycle->setCheckable(true);
    cycle->setChecked(true);
    priorityButtons->addButton(cycle, -1);
    priorityTools->addWidget(cycle);
    for (int value = 0; value <= 5; ++value) {
        auto* button = new QPushButton(tr("Prioridade %1").arg(value), m_priorityControls);
        button->setCheckable(true);
        priorityButtons->addButton(button, value);
        priorityTools->addWidget(button);
    }
    auto* bottomUp = new QPushButton(tr("Distribuir de baixo para cima"), m_priorityControls);
    bottomUp->setToolTip(tr("Na seleção atual, aplica 1 na linha mais baixa, 2 na linha acima e assim por diante até 5."));
    priorityTools->addWidget(bottomUp);
    toolsLayout->addWidget(m_priorityControls);

    m_collisionControls = new QWidget(tools);
    auto* collisionTools = new QVBoxLayout(m_collisionControls);
    collisionTools->setContentsMargins(0, 6, 0, 0);
    auto* collisionHelp = new QLabel(tr("Clique no centro para bloquear tudo ou nas bordas para editar cada lado."), m_collisionControls);
    collisionHelp->setWordWrap(true);
    collisionTools->addWidget(collisionHelp);
    auto* collisionButtons = new QButtonGroup(m_collisionControls);
    collisionButtons->setExclusive(true);
    auto addCollisionButton = [this, collisionButtons, collisionTools](const QString& text, int mask, bool checked = false) {
        auto* button = new QPushButton(text, m_collisionControls);
        button->setCheckable(true);
        button->setChecked(checked);
        collisionButtons->addButton(button, mask);
        collisionTools->addWidget(button);
        return button;
    };
    addCollisionButton(tr("Editar lados pelo clique"), -1, true);
    addCollisionButton(tr("Livre"), 0);
    addCollisionButton(tr("Bloqueado inteiro"), Editor::SideAll);
    addCollisionButton(tr("Somente topo"), Editor::SideTop);
    addCollisionButton(tr("Somente direita"), Editor::SideRight);
    addCollisionButton(tr("Somente baixo"), Editor::SideBottom);
    addCollisionButton(tr("Somente esquerda"), Editor::SideLeft);
    toolsLayout->addWidget(m_collisionControls);
    m_collisionControls->hide();

    m_priorityStatus = new QLabel(tr("Clique num tile para começar."), tools);
    m_priorityStatus->setObjectName(QStringLiteral("priorityCollisionStatus"));
    m_priorityStatus->setWordWrap(true);
    toolsLayout->addWidget(m_priorityStatus);
    toolsLayout->addStretch(1);
    priorityCollisionLayout->addWidget(tools);
    m_tabs->addTab(priorityCollisionTab, tr("Prioridade e Colisão"));

    connect(priorityButtons, &QButtonGroup::idClicked, this, [this](int value) {
        if (m_priorityView) m_priorityView->setPriorityPaintValue(value);
    });
    connect(bottomUp, &QPushButton::clicked, this, [this] {
        if (m_priorityView) m_priorityView->applyPriorityBottomUpToSelection();
    });
    connect(collisionButtons, &QButtonGroup::idClicked, this, [this](int mask) {
        if (m_priorityView) m_priorityView->setCollisionPaintMask(mask);
    });
    connect(m_priorityCollisionMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool collision = index == 1;
        if (m_priorityControls) m_priorityControls->setVisible(!collision);
        if (m_collisionControls) m_collisionControls->setVisible(collision);
        if (m_priorityView) {
            m_priorityView->setPriorityMode(!collision);
            m_priorityView->setCollisionMode(collision);
        }
        if (m_priorityStatus)
            m_priorityStatus->setText(collision
                ? tr("Centro alterna o tile inteiro; bordas editam lados individualmente.")
                : tr("Escolha uma prioridade ou use o ciclo."));
    });
    connect(m_priorityView, &TilesetView::statusMessage, this, [this](const QString& text) {
        if (m_priorityStatus) m_priorityStatus->setText(text);
    });

    // ------------------------------------------------ Efeitos por Tile / Autotile
    auto* effectsTab = new QWidget(m_tabs);
    auto* effectsLayout = new QHBoxLayout(effectsTab);
    effectsLayout->setContentsMargins(6, 6, 6, 6);
    auto* effectsScroll = new QScrollArea(effectsTab);
    effectsScroll->setWidgetResizable(false);
    m_effectsView = new TilesetView(ed, effectsScroll);
    m_effectsView->setObjectName(QStringLiteral("tilesetEffectsView"));
    m_effectsView->setHideAutotileTiles(true);
    m_effectsView->setReflectionOverlay(true);
    effectsScroll->setWidget(m_effectsView);
    effectsLayout->addWidget(effectsScroll, 1);

    auto* effectsTools = new QWidget(effectsTab);
    effectsTools->setMinimumWidth(250); effectsTools->setMaximumWidth(330);
    auto* effectsToolsLayout = new QVBoxLayout(effectsTools);
    effectsToolsLayout->setContentsMargins(6,0,0,0);
    auto* effectsTitle = new QLabel(tr("<b>Efeitos do tile</b>"), effectsTools);
    effectsToolsLayout->addWidget(effectsTitle);
    auto* effectsHint = new QLabel(tr("Selecione um ou mais tiles comuns e escolha o reflexo. O efeito acompanha apenas as partes visíveis do desenho. Autotiles são configurados no painel à direita."), effectsTools);
    effectsHint->setWordWrap(true); effectsToolsLayout->addWidget(effectsHint);
    effectsToolsLayout->addWidget(new QLabel(tr("Reflexo"), effectsTools));
    m_tileReflectionPreset = new QComboBox(effectsTools);
    populateReflectionPresets(m_tileReflectionPreset);
    effectsToolsLayout->addWidget(m_tileReflectionPreset);
    effectsToolsLayout->addWidget(new QLabel(tr("Opacidade do reflexo"), effectsTools));
    m_tileReflectionOpacity = new QSpinBox(effectsTools);
    m_tileReflectionOpacity->setObjectName(QStringLiteral("tileReflectionOpacity"));
    m_tileReflectionOpacity->setRange(-1, 100);
    m_tileReflectionOpacity->setSpecialValueText(tr("Valor original"));
    m_tileReflectionOpacity->setSuffix(tr("%"));
    m_tileReflectionOpacity->setValue(-1);
    m_tileReflectionOpacity->setToolTip(tr("Valor original mantém a intensidade definida pela superfície. Um valor de 0 a 100% controla o quanto aparece refletido neste tile: céu, personagens, objetos e luzes."));
    effectsToolsLayout->addWidget(m_tileReflectionOpacity);
    effectsToolsLayout->addWidget(new QLabel(tr("Suavidade do reflexo"), effectsTools));
    m_tileReflectionBlurMode = new QComboBox(effectsTools);
    m_tileReflectionBlurMode->setObjectName(QStringLiteral("tileReflectionBlurMode"));
    m_tileReflectionBlurMode->addItem(tr("Desativado"), QStringLiteral("none"));
    m_tileReflectionBlurMode->addItem(tr("Horizontal"), QStringLiteral("horizontal"));
    m_tileReflectionBlurMode->addItem(tr("Vertical"), QStringLiteral("vertical"));
    m_tileReflectionBlurMode->addItem(tr("Horizontal + Vertical"), QStringLiteral("both"));
    m_tileReflectionBlurMode->setToolTip(tr("Suaviza o reflexo depois da ondulação. Use valores baixos para água e piso molhado e valores maiores quando quiser um reflexo mais difuso."));
    effectsToolsLayout->addWidget(m_tileReflectionBlurMode);
    effectsToolsLayout->addWidget(new QLabel(tr("Quanto suavizar"), effectsTools));
    m_tileReflectionBlurStrength = new QSpinBox(effectsTools);
    m_tileReflectionBlurStrength->setObjectName(QStringLiteral("tileReflectionBlurStrength"));
    m_tileReflectionBlurStrength->setRange(0, 24);
    m_tileReflectionBlurStrength->setSuffix(tr(" px"));
    m_tileReflectionBlurStrength->setValue(0);
    m_tileReflectionBlurStrength->setToolTip(tr("0 mantém o reflexo nítido. Valores baixos, entre 1 e 4 px, costumam funcionar bem para água e piso molhado."));
    effectsToolsLayout->addWidget(m_tileReflectionBlurStrength);
    auto* environmentHint = new QLabel(tr("Céu/ambiente refletido é uma configuração do mapa atual: aba Mapa → Reflexos."), effectsTools);
    environmentHint->setWordWrap(true);
    environmentHint->setProperty("uiRole", QStringLiteral("hint"));
    effectsToolsLayout->addWidget(environmentHint);
    m_effectsStatus = new QLabel(tr("Selecione um tile para configurar."), effectsTools);
    m_effectsStatus->setWordWrap(true); effectsToolsLayout->addWidget(m_effectsStatus);
    effectsToolsLayout->addStretch(1);
    effectsLayout->addWidget(effectsTools);
    if (ed.rpgMakerEngine == core::RpgMakerEngine::MZ)
        m_tabs->addTab(effectsTab, tr("Efeitos"));
    else
        effectsTab->hide();

    auto refreshTileEffectSelection = [this] {
        if (!m_effectsView || !m_tileReflectionPreset || !m_tileReflectionOpacity || !m_tileReflectionBlurMode || !m_tileReflectionBlurStrength) return;
        const int tsIdx = m_effectsView->displayedTilesetIndex();
        const QVector<QPoint> tiles = m_effectsView->selectedTiles();
        QSignalBlocker presetBlocker(m_tileReflectionPreset);
        QSignalBlocker opacityBlocker(m_tileReflectionOpacity);
        QSignalBlocker blurModeBlocker(m_tileReflectionBlurMode);
        QSignalBlocker blurStrengthBlocker(m_tileReflectionBlurStrength);
        const bool valid = tsIdx >= 0 && !tiles.isEmpty();
        m_tileReflectionPreset->setEnabled(valid);
        if (!valid) {
            m_tileReflectionPreset->setCurrentIndex(0);
            m_tileReflectionOpacity->setEnabled(false);
            m_tileReflectionOpacity->setValue(-1);
            m_tileReflectionBlurMode->setEnabled(false);
            m_tileReflectionBlurMode->setCurrentIndex(0);
            m_tileReflectionBlurStrength->setEnabled(false);
            m_tileReflectionBlurStrength->setValue(0);
            if(m_effectsStatus)m_effectsStatus->setText(tr("Selecione um tile comum para configurar."));
            return;
        }
        QString commonPreset;
        int commonOpacity = -1;
        QString commonBlurMode = QStringLiteral("none");
        int commonBlurStrength = 0;
        bool first = true, mixedPreset = false, mixedOpacity = false, mixedBlurMode = false, mixedBlurStrength = false;
        for (const QPoint& tile : tiles) {
            const QString preset = ed.tileReflectionPreset(tsIdx, tile.x(), tile.y());
            const int opacity = ed.tileReflectionOpacityPercent(tsIdx, tile.x(), tile.y());
            const QString blurMode = ed.tileReflectionBlurMode(tsIdx, tile.x(), tile.y());
            const int blurStrength = ed.tileReflectionBlurStrength(tsIdx, tile.x(), tile.y());
            if (first) { commonPreset = preset; commonOpacity = opacity; commonBlurMode = blurMode; commonBlurStrength = blurStrength; first = false; }
            else {
                if (commonPreset != preset) mixedPreset = true;
                if (commonOpacity != opacity) mixedOpacity = true;
                if (commonBlurMode != blurMode) mixedBlurMode = true;
                if (commonBlurStrength != blurStrength) mixedBlurStrength = true;
            }
        }
        int idx = mixedPreset ? 0 : m_tileReflectionPreset->findData(commonPreset);
        m_tileReflectionPreset->setCurrentIndex(idx >= 0 ? idx : 0);
        const bool opacityEnabled = !mixedPreset && !commonPreset.isEmpty();
        m_tileReflectionOpacity->setEnabled(opacityEnabled);
        m_tileReflectionOpacity->setValue(mixedOpacity ? -1 : commonOpacity);
        m_tileReflectionBlurMode->setEnabled(opacityEnabled);
        const int blurIndex = mixedBlurMode ? 0 : m_tileReflectionBlurMode->findData(commonBlurMode);
        m_tileReflectionBlurMode->setCurrentIndex(blurIndex >= 0 ? blurIndex : 0);
        m_tileReflectionBlurStrength->setEnabled(opacityEnabled && !mixedBlurMode && commonBlurMode != QLatin1String("none"));
        m_tileReflectionBlurStrength->setValue(mixedBlurStrength ? 0 : commonBlurStrength);
        if (m_effectsStatus) {
            if (mixedPreset) m_effectsStatus->setText(tr("%1 tiles selecionados · configurações diferentes.").arg(tiles.size()));
            else if (commonPreset.isEmpty()) m_effectsStatus->setText(tr("%1 tile(s) selecionado(s) · Reflexo desativado.").arg(tiles.size()));
            else if (mixedOpacity || mixedBlurMode || mixedBlurStrength) m_effectsStatus->setText(tr("%1 tiles · Reflexo: %2 · propriedades diferentes.").arg(tiles.size()).arg(m_tileReflectionPreset->currentText()));
            else m_effectsStatus->setText(tr("%1 tile(s) · Reflexo: %2 · Opacidade: %3 · Suavidade: %4").arg(tiles.size()).arg(m_tileReflectionPreset->currentText()).arg(commonOpacity < 0 ? tr("valor original") : tr("%1%").arg(commonOpacity)).arg(commonBlurMode == QLatin1String("none") || commonBlurStrength <= 0 ? tr("desativado") : tr("%1 · %2 px").arg(m_tileReflectionBlurMode->currentText()).arg(commonBlurStrength)));
        }
    };
    connect(&ed, &Editor::selectionChanged, this, refreshTileEffectSelection);
    connect(&ed, &Editor::tilesetsChanged, this, refreshTileEffectSelection);
    connect(m_effectsView, &TilesetView::statusMessage, this, [this](const QString& text){ if(m_effectsStatus)m_effectsStatus->setText(text); });
    connect(m_tileReflectionPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, refreshTileEffectSelection](int index) {
        if (!m_tileReflectionPreset || !m_tileReflectionPreset->isEnabled() || index < 0 || !m_effectsView) return;
        const int tsIdx = m_effectsView->displayedTilesetIndex();
        const QVector<QPoint> tiles = m_effectsView->selectedTiles();
        if (tsIdx < 0 || tiles.isEmpty()) return;
        const QString preset = m_tileReflectionPreset->itemData(index).toString();
        for (const QPoint& tile : tiles) ed.setTileReflectionPreset(tsIdx, tile.x(), tile.y(), preset);
        m_effectsView->update(); refreshTileEffectSelection();
    });
    connect(m_tileReflectionOpacity, qOverload<int>(&QSpinBox::valueChanged), this, [this, refreshTileEffectSelection](int value) {
        if (!m_tileReflectionOpacity || !m_tileReflectionOpacity->isEnabled() || !m_effectsView) return;
        const int tsIdx = m_effectsView->displayedTilesetIndex();
        const QVector<QPoint> tiles = m_effectsView->selectedTiles();
        if (tsIdx < 0 || tiles.isEmpty()) return;
        for (const QPoint& tile : tiles) ed.setTileReflectionOpacityPercent(tsIdx, tile.x(), tile.y(), value);
        m_effectsView->update(); refreshTileEffectSelection();
    });
    connect(m_tileReflectionBlurMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, refreshTileEffectSelection](int index) {
        if (!m_tileReflectionBlurMode || !m_tileReflectionBlurMode->isEnabled() || index < 0 || !m_effectsView) return;
        const int tsIdx = m_effectsView->displayedTilesetIndex();
        const QVector<QPoint> tiles = m_effectsView->selectedTiles();
        if (tsIdx < 0 || tiles.isEmpty()) return;
        const QString mode = m_tileReflectionBlurMode->itemData(index).toString();
        int strength = m_tileReflectionBlurStrength ? m_tileReflectionBlurStrength->value() : 0;
        if (mode != QLatin1String("none") && strength <= 0) strength = 2;
        for (const QPoint& tile : tiles) ed.setTileReflectionBlur(tsIdx, tile.x(), tile.y(), mode, strength);
        m_effectsView->update(); refreshTileEffectSelection();
    });
    connect(m_tileReflectionBlurStrength, qOverload<int>(&QSpinBox::valueChanged), this, [this, refreshTileEffectSelection](int value) {
        if (!m_tileReflectionBlurStrength || !m_effectsView) return;
        const int tsIdx = m_effectsView->displayedTilesetIndex();
        const QVector<QPoint> tiles = m_effectsView->selectedTiles();
        if (tsIdx < 0 || tiles.isEmpty()) return;
        QString mode = m_tileReflectionBlurMode ? m_tileReflectionBlurMode->currentData().toString() : QStringLiteral("none");
        if (value <= 0) mode = QStringLiteral("none");
        for (const QPoint& tile : tiles) ed.setTileReflectionBlur(tsIdx, tile.x(), tile.y(), mode, value);
        m_effectsView->update(); refreshTileEffectSelection();
    });

    // ---------------------------------------------------------------- Propriedades
    auto* properties = new QWidget(this);
    properties->setMinimumWidth(245);
    properties->setMaximumWidth(330);
    auto* propertiesLayout = new QVBoxLayout(properties);
    propertiesLayout->setContentsMargins(0, 0, 0, 0);
    propertiesLayout->addWidget(new QLabel(tr("<b>Propriedades</b>"), properties));
    m_info = new QLabel(properties);
    m_info->setObjectName(QStringLiteral("tilesetProperties"));
    m_info->setAccessibleName(tr("Propriedades do tileset selecionado"));
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    propertiesLayout->addWidget(m_info);
    auto* authorityNote = new QLabel(tr(
        "Selecione um Autotile para ajustar suas propriedades."), properties);
    authorityNote->setWordWrap(true);
    propertiesLayout->addWidget(authorityNote);
    propertiesLayout->addWidget(new QLabel(tr("<b>Autotile selecionado</b>"), properties));
    propertiesLayout->addWidget(new QLabel(tr("Categoria"), properties));
    m_autotileCategory = new QLineEdit(properties);
    m_autotileCategory->setObjectName(QStringLiteral("autotileCategoryEditor"));
    m_autotileCategory->setAccessibleName(tr("Categoria do Autotile"));
    m_autotileCategory->setPlaceholderText(tr("Ex.: Água, Natureza, Caminhos, Interiores"));
    m_autotileCategory->setMaxLength(128);
    m_autotileCategory->setToolTip(tr("Usada para organizar e filtrar Autotiles."));
    propertiesLayout->addWidget(m_autotileCategory);

    m_autotilePriority = new QComboBox(properties);
    m_autotilePriority->setObjectName(QStringLiteral("autotilePriorityEditor"));
    m_autotilePriority->addItem(tr("Prioridade 0"), 0);
    for (int value = 1; value <= 5; ++value)
        m_autotilePriority->addItem(tr("Prioridade %1").arg(value), value);
    propertiesLayout->addWidget(new QLabel(tr("Prioridade"), properties));
    propertiesLayout->addWidget(m_autotilePriority);

    m_autotileCollision = new QComboBox(properties);
    m_autotileCollision->setObjectName(QStringLiteral("autotileCollisionEditor"));
    m_autotileCollision->addItem(tr("Livre"), 0);
    m_autotileCollision->addItem(tr("Bloqueado"), int(Editor::SideAll));
    m_autotileCollision->addItem(tr("Bloquear topo"), int(Editor::SideTop));
    m_autotileCollision->addItem(tr("Bloquear direita"), int(Editor::SideRight));
    m_autotileCollision->addItem(tr("Bloquear baixo"), int(Editor::SideBottom));
    m_autotileCollision->addItem(tr("Bloquear esquerda"), int(Editor::SideLeft));
    propertiesLayout->addWidget(new QLabel(tr("Colisão"), properties));
    propertiesLayout->addWidget(m_autotileCollision);

    auto* autotileReflectionLabel = new QLabel(tr("Reflexo"), properties);
    propertiesLayout->addWidget(autotileReflectionLabel);
    m_autotileReflectionPreset = new QComboBox(properties);
    m_autotileReflectionPreset->setObjectName(QStringLiteral("autotileReflectionPreset"));
    populateReflectionPresets(m_autotileReflectionPreset);
    m_autotileReflectionPreset->setToolTip(tr("Aplica o mesmo reflexo a todas as partes deste Autotile, incluindo centro, bordas, cantos e quadros da animação."));
    propertiesLayout->addWidget(m_autotileReflectionPreset);
    auto* autotileReflectionOpacityLabel = new QLabel(tr("Opacidade do reflexo"), properties);
    propertiesLayout->addWidget(autotileReflectionOpacityLabel);
    m_autotileReflectionOpacity = new QSpinBox(properties);
    m_autotileReflectionOpacity->setObjectName(QStringLiteral("autotileReflectionOpacity"));
    m_autotileReflectionOpacity->setRange(-1, 100);
    m_autotileReflectionOpacity->setSpecialValueText(tr("Valor original"));
    m_autotileReflectionOpacity->setSuffix(tr("%"));
    m_autotileReflectionOpacity->setValue(-1);
    m_autotileReflectionOpacity->setToolTip(tr("Controla a intensidade de tudo que este Autotile reflete. Valor original mantém a configuração da superfície; 0% oculta e 100% mantém o reflexo completo."));
    propertiesLayout->addWidget(m_autotileReflectionOpacity);
    auto* autotileReflectionBlurLabel = new QLabel(tr("Suavidade do reflexo"), properties);
    propertiesLayout->addWidget(autotileReflectionBlurLabel);
    m_autotileReflectionBlurMode = new QComboBox(properties);
    m_autotileReflectionBlurMode->setObjectName(QStringLiteral("autotileReflectionBlurMode"));
    m_autotileReflectionBlurMode->addItem(tr("Desativado"), QStringLiteral("none"));
    m_autotileReflectionBlurMode->addItem(tr("Horizontal"), QStringLiteral("horizontal"));
    m_autotileReflectionBlurMode->addItem(tr("Vertical"), QStringLiteral("vertical"));
    m_autotileReflectionBlurMode->addItem(tr("Horizontal + Vertical"), QStringLiteral("both"));
    propertiesLayout->addWidget(m_autotileReflectionBlurMode);
    auto* autotileReflectionBlurStrengthLabel = new QLabel(tr("Quanto suavizar"), properties);
    propertiesLayout->addWidget(autotileReflectionBlurStrengthLabel);
    m_autotileReflectionBlurStrength = new QSpinBox(properties);
    m_autotileReflectionBlurStrength->setObjectName(QStringLiteral("autotileReflectionBlurStrength"));
    m_autotileReflectionBlurStrength->setRange(0, 24);
    m_autotileReflectionBlurStrength->setSuffix(tr(" px"));
    m_autotileReflectionBlurStrength->setToolTip(tr("Suaviza o reflexo de todas as partes deste Autotile. O resultado acompanha a ondulação automaticamente."));
    propertiesLayout->addWidget(m_autotileReflectionBlurStrength);

    const bool supportsExtraReflection = ed.rpgMakerEngine == core::RpgMakerEngine::MZ;
    for (QWidget* widget : {static_cast<QWidget*>(autotileReflectionLabel),
                            static_cast<QWidget*>(m_autotileReflectionPreset),
                            static_cast<QWidget*>(autotileReflectionOpacityLabel),
                            static_cast<QWidget*>(m_autotileReflectionOpacity),
                            static_cast<QWidget*>(autotileReflectionBlurLabel),
                            static_cast<QWidget*>(m_autotileReflectionBlurMode),
                            static_cast<QWidget*>(autotileReflectionBlurStrengthLabel),
                            static_cast<QWidget*>(m_autotileReflectionBlurStrength)})
        widget->setVisible(supportsExtraReflection);

    m_autotileDetails = new QLabel(properties);
    m_autotileDetails->setObjectName(QStringLiteral("autotileResourceDetails"));
    m_autotileDetails->setWordWrap(true);
    m_autotileDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    propertiesLayout->addWidget(m_autotileDetails);

    m_autotileBoundary = new QCheckBox(tr("Continuar o Autotile além das bordas do mapa"), properties);
    m_autotileBoundary->setObjectName(QStringLiteral("autotileBoundaryExtension"));
    m_autotileBoundary->setAccessibleName(tr("Continuar Autotile nas bordas do mapa"));
    m_autotileBoundary->setToolTip(tr("Faz o Autotile se comportar como se o mesmo terreno continuasse para fora do mapa. Isso evita bordas abertas nas extremidades."));
    propertiesLayout->addWidget(m_autotileBoundary);
    m_openTerrainEditor = new QPushButton(tr("Configurar conexões…"), properties);
    m_openTerrainEditor->setObjectName(QStringLiteral("openAutotileTerrainEditor"));
    m_openTerrainEditor->setAccessibleName(tr("Configurar como o Autotile se conecta aos vizinhos"));
    propertiesLayout->addWidget(m_openTerrainEditor);
    propertiesLayout->addStretch(1);
    body->addWidget(properties);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem*, QTreeWidgetItem*) {
        if (m_autotileList) {
            QSignalBlocker blocker(m_autotileList);
            m_autotileList->setCurrentRow(-1);
        }
        const int tilesetIdx = selectedTilesetIndex();
        if (tilesetIdx >= 0) ed.session.activeTilesetIdx = tilesetIdx;
        if (m_priorityView) { m_priorityView->updateGeometry(); m_priorityView->resize(m_priorityView->sizeHint()); m_priorityView->update(); }
        if (m_terrainView) { m_terrainView->updateGeometry(); m_terrainView->resize(m_terrainView->sizeHint()); m_terrainView->update(); }
        if (m_effectsView) { m_effectsView->updateGeometry(); m_effectsView->resize(m_effectsView->sizeHint()); m_effectsView->update(); }
        // A biblioteca de Autotiles é global e não acompanha esta árvore.
        refreshAutotilePanel();
        refreshPreview();
    });
    connect(m_autotileList, &QListWidget::currentRowChanged, this, [this](int) {
        refreshAutotilePanel();
        refreshPreview();
    });
    connect(m_autotileList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        if (m_tabs) m_tabs->setCurrentIndex(0);
        refreshPreview();
    });
    connect(m_autotileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        renameAutotile();
    });
    connect(m_openTerrainEditor, &QPushButton::clicked, this, [this] {
        if (m_tabs && m_terrainTabIndex >= 0 && m_tabs->isTabEnabled(m_terrainTabIndex))
            m_tabs->setCurrentIndex(m_terrainTabIndex);
    });
    connect(m_autotileCategory, &QLineEdit::editingFinished, this, [this] {
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        if (id.isEmpty() || ownerIdx < 0 || !m_autotileCategory) return;
        const TilesetAutotile* current = tilesetAutotileById(ed, ownerIdx, id);
        const QString category = m_autotileCategory->text().trimmed().left(128);
        if (!current || current->category == category) return;
        if (setTilesetAutotileCategory(ed, ownerIdx, id, category)) {
            emit ed.tilesetsChanged();
            refreshAutotiles(id);
        }
    });
    auto applyAutotileMetadata = [this](int kind, const QVariant& value) {
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        Tileset* ts = ed.tilesetAt(ownerIdx);
        const TilesetAutotile* autotile = ownerIdx >= 0 ? tilesetAutotileById(ed, ownerIdx, id) : nullptr;
        if (!ts || !autotile) return;
        const QVector<QPoint> tiles = tilesetAutotileTiles(ed, ownerIdx, *autotile);
        bool changed = false;
        for (const QPoint& tile : tiles) {
            if (!ts->contains(tile.x(), tile.y())) continue;
            if (kind == 0) {
                const int next = qBound(0, value.toInt(), 5);
                if (ts->tilePriority(tile.x(), tile.y()) != next) {
                    ts->setTilePriority(tile.x(), tile.y(), next);
                    const QString key = tileKey(ownerIdx, tile.x(), tile.y());
                    if (next > 0) ed.starTiles.insert(key, true); else ed.starTiles.remove(key);
                    changed = true;
                }
            } else if (kind == 1) {
                const int next = value.toInt() & int(Editor::SideAll);
                if (ts->tileCollisionMask(tile.x(), tile.y()) != next) { ts->setTileCollisionMask(tile.x(), tile.y(), next); changed = true; }
            }
        }
        if (!changed) return;
        ed.markDirty();
        emit ed.tilesetsChanged();
        emit ed.mapChanged();
        refreshAutotilePanel();
        refreshPreview();
    };
    connect(m_autotilePriority, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, applyAutotileMetadata](int index) {
        if (!m_autotilePriority || !m_autotilePriority->isEnabled() || index < 0) return;
        applyAutotileMetadata(0, m_autotilePriority->itemData(index));
    });
    connect(m_autotileCollision, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, applyAutotileMetadata](int index) {
        if (!m_autotileCollision || !m_autotileCollision->isEnabled() || index < 0) return;
        applyAutotileMetadata(1, m_autotileCollision->itemData(index));
    });
    connect(m_autotileReflectionPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_autotileReflectionPreset || !m_autotileReflectionPreset->isEnabled() || index < 0) return;
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        if (id.isEmpty() || ownerIdx < 0) return;
        TilesetAutotile* autotile = tilesetAutotileById(ed, ownerIdx, id);
        if (!autotile) return;
        const QString preset = m_autotileReflectionPreset->itemData(index).toString();
        if (resourceEffectPreset(autotile->resourceEffects, QStringLiteral("reflection")) == preset) return;
        setResourceEffectPreset(autotile->resourceEffects, QStringLiteral("reflection"), preset);
        ed.markDirty(); emit ed.tilesetsChanged(); emit ed.mapChanged(); refreshAutotilePanel(); refreshPreview();
    });
    connect(m_autotileReflectionOpacity, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_autotileReflectionOpacity || !m_autotileReflectionOpacity->isEnabled()) return;
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        if (id.isEmpty() || ownerIdx < 0) return;
        TilesetAutotile* autotile = tilesetAutotileById(ed, ownerIdx, id);
        if (!autotile || resourceEffectPreset(autotile->resourceEffects, QStringLiteral("reflection")).isEmpty()) return;
        const int normalized = value < 0 ? -1 : qBound(0, value, 100);
        if (resourceEffectOpacityPercent(autotile->resourceEffects, QStringLiteral("reflection")) == normalized) return;
        setResourceEffectOpacityPercent(autotile->resourceEffects, QStringLiteral("reflection"), normalized);
        ed.markDirty(); emit ed.tilesetsChanged(); emit ed.mapChanged(); refreshAutotilePanel(); refreshPreview();
    });
    connect(m_autotileReflectionBlurMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_autotileReflectionBlurMode || !m_autotileReflectionBlurMode->isEnabled() || index < 0) return;
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        if (id.isEmpty() || ownerIdx < 0) return;
        TilesetAutotile* autotile = tilesetAutotileById(ed, ownerIdx, id);
        if (!autotile || resourceEffectPreset(autotile->resourceEffects, QStringLiteral("reflection")).isEmpty()) return;
        const QString mode = m_autotileReflectionBlurMode->itemData(index).toString();
        int strength = m_autotileReflectionBlurStrength ? m_autotileReflectionBlurStrength->value() : 0;
        if (mode != QLatin1String("none") && strength <= 0) strength = 2;
        setResourceEffectBlur(autotile->resourceEffects, QStringLiteral("reflection"), mode, strength);
        ed.markDirty(); emit ed.tilesetsChanged(); emit ed.mapChanged(); refreshAutotilePanel(); refreshPreview();
    });
    connect(m_autotileReflectionBlurStrength, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_autotileReflectionBlurStrength || !m_autotileReflectionBlurMode || !m_autotileReflectionBlurMode->isEnabled()) return;
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        if (id.isEmpty() || ownerIdx < 0) return;
        TilesetAutotile* autotile = tilesetAutotileById(ed, ownerIdx, id);
        if (!autotile || resourceEffectPreset(autotile->resourceEffects, QStringLiteral("reflection")).isEmpty()) return;
        QString mode = m_autotileReflectionBlurMode->currentData().toString();
        if (value <= 0) mode = QStringLiteral("none");
        setResourceEffectBlur(autotile->resourceEffects, QStringLiteral("reflection"), mode, value);
        ed.markDirty(); emit ed.tilesetsChanged(); emit ed.mapChanged(); refreshAutotilePanel(); refreshPreview();
    });
    connect(m_autotileBoundary, &QCheckBox::toggled, this, [this](bool checked) {
        const QString id = selectedAutotileId();
        const int ownerIdx = selectedAutotileOwnerIdx();
        if (id.isEmpty() || ownerIdx < 0) return;
        const TilesetAutotile* current = tilesetAutotileById(ed, ownerIdx, id);
        if (!current || current->extendAtMapBoundary == checked) return;
        if (setTilesetAutotileBoundaryExtension(ed, ownerIdx, id, checked)) {
            emit ed.tilesetsChanged();
            refreshAutotiles(id);
        }
    });
    connect(&ed, &Editor::tilesetsChanged, this, [this] {
        if (m_priorityView) { m_priorityView->updateGeometry(); m_priorityView->resize(m_priorityView->sizeHint()); m_priorityView->update(); }
        if (m_terrainView) { m_terrainView->updateGeometry(); m_terrainView->resize(m_terrainView->sizeHint()); m_terrainView->update(); }
        if (m_effectsView) { m_effectsView->updateGeometry(); m_effectsView->resize(m_effectsView->sizeHint()); m_effectsView->update(); }
        const QString keep = selectedAutotileId();
        refreshAutotiles(keep);
        refreshPreview();
    });
    connect(&ed, &Editor::wangChanged, this, [this] {
        const QString keep = selectedAutotileId();
        refreshAutotiles(keep);
        refreshPreview();
    });

    connect(create, &QPushButton::clicked, this, [this] { createTileset(); });
    connect(addImage, &QPushButton::clicked, this, [this] { importIntoTileset(selectedTilesetIndex()); });
    connect(rename, &QPushButton::clicked, this, [this] { renameTileset(); });
    connect(remove, &QPushButton::clicked, this, [this] { deleteTileset(); });
    connect(duplicateAction, &QAction::triggered, this, [this] { duplicateTileset(); });
    connect(updatePageAction, &QAction::triggered, this, [this] { updateSourceImage(); });
    connect(deletePageAction, &QAction::triggered, this, [this] { deleteTilesetPage(); });
    connect(reduceAction, &QAction::triggered, this, [this] { reduceTilesetSize(); });
    connect(importAutotileAction, &QAction::triggered, this, [this] { importAutotile(); });
    connect(importA1Action, &QAction::triggered, this, [this] { importA1Sheet(); });
    connect(m_renameAutotile, &QPushButton::clicked, this, [this] { renameAutotile(); });
    connect(m_deleteAutotile, &QPushButton::clicked, this, [this] { deleteAutotile(); });
    connect(m_editAutotileAnimationAction, &QAction::triggered, this, [this] { editAnimations(selectedAutotileId()); });

    refresh(ed.session.activeTilesetIdx);
}

void TilesetManagerDialog::refresh(int select)
{
    const QString keepAutotile = selectedAutotileId();
    m_list->blockSignals(true);
    m_list->clear();

    const QVector<int> visible = visibleTilesetIndices(ed);
    const int wanted = select >= 0 ? select : ed.session.activeTilesetIdx;
    QTreeWidgetItem* wantedItem = nullptr;
    QSet<QString> seenGroups;

    for (int anyIndex : visible) {
        const Tileset* any = ed.tilesetAt(anyIndex);
        if (!any) continue;
        const QString group = core::tilesetPageGroupKey(*any);
        if (seenGroups.contains(group)) continue;
        seenGroups.insert(group);

        const QVector<int> pages = core::tilesetPageIndices(ed, anyIndex);
        if (pages.isEmpty()) continue;
        const int representative = pages.first();
        const Tileset* logical = ed.tilesetAt(representative);
        if (!logical) continue;

        QString logicalName = logical->category.isEmpty()
            ? logical->name
            : QStringLiteral("[%1] %2").arg(logical->category, logical->name);
        if (!logical->paletteVisible) logicalName += tr(" · oculto da paleta");

        auto* parent = new QTreeWidgetItem(m_list);
        parent->setText(0, logicalName);
        parent->setData(0, Qt::UserRole, representative);
        parent->setData(0, kTilesetNodeKindRole, kTilesetLogicalNode);
        parent->setToolTip(0, tr("Tileset com %1 página(s). Selecione uma página para ações específicas dela.").arg(pages.size()));
        parent->setExpanded(true);

        const Tileset* wantedTs = ed.tilesetAt(wanted);
        if (wantedTs && core::tilesetPageGroupKey(*wantedTs) == group && wanted < 0)
            wantedItem = parent;

        for (int pagePos = 0; pagePos < pages.size(); ++pagePos) {
            const int pageIndex = pages.at(pagePos);
            const Tileset* page = ed.tilesetAt(pageIndex);
            if (!page) continue;
            auto* child = new QTreeWidgetItem(parent);
            child->setText(0, tr("Página %1 — %2×%3 px").arg(pagePos + 1).arg(page->image.width()).arg(page->image.height()));
            child->setData(0, Qt::UserRole, pageIndex);
            child->setData(0, kTilesetNodeKindRole, kTilesetPageNode);
            child->setToolTip(0, tr("%1×%2 tiles · tiles de %3×%4 px%5%6")
                .arg(page->columns).arg(page->rows).arg(page->tilewidth).arg(page->tileheight)
                .arg(page->combined ? tr(" · editável/combinado") : QString())
                .arg(page->generatedFromBake ? tr(" · gerado por Bake") : QString()));
            if (pageIndex == wanted) wantedItem = child;
        }
    }

    if (!wantedItem && m_list->topLevelItemCount() > 0) wantedItem = m_list->topLevelItem(0);
    if (wantedItem) {
        m_list->setCurrentItem(wantedItem);
        const int idx = selectedTilesetIndex();
        if (idx >= 0) ed.session.activeTilesetIdx = idx;
    } else {
        // Um projeto pode conter apenas backings internos de Autotile. Eles
        // nunca se tornam o Tileset normal ativo por acidente.
        ed.session.activeTilesetIdx = -1;
        ed.session.tsSel = TilesetSelection();
    }
    m_list->expandAll();
    m_list->blockSignals(false);

    // Dentro do Gerenciador o atlas permanece físico/original. A dobra visual
    // em 8 colunas existe somente na paleta principal de pintura.
    if (m_priorityView) m_priorityView->setTilesetOverride(ed.session.activeTilesetIdx);
    if (m_effectsView) m_effectsView->setTilesetOverride(ed.session.activeTilesetIdx);
    refreshAutotiles(keepAutotile);
    refreshPreview();
}

int TilesetManagerDialog::selectedTilesetIndex() const
{
    if (!m_list || !m_list->currentItem()) return -1;
    bool ok = false;
    const int index = m_list->currentItem()->data(0, Qt::UserRole).toInt(&ok);
    return ok ? index : -1;
}

bool TilesetManagerDialog::selectedTilesetIsPage() const
{
    if (!m_list || !m_list->currentItem()) return false;
    return m_list->currentItem()->data(0, kTilesetNodeKindRole).toInt() == kTilesetPageNode;
}

QString TilesetManagerDialog::selectedAutotileId() const
{
    if (!m_autotileList || !m_autotileList->currentItem()) return QString();
    return m_autotileList->currentItem()->data(Qt::UserRole).toString();
}

int TilesetManagerDialog::selectedAutotileOwnerIdx() const
{
    if (!m_autotileList || !m_autotileList->currentItem()) return -1;
    bool ok = false;
    const int index = m_autotileList->currentItem()->data(Qt::UserRole + 1).toInt(&ok);
    return ok ? index : -1;
}

void TilesetManagerDialog::refreshAutotiles(const QString& selectId)
{
    if (!m_autotileList) return;
    const QString keep = selectId.isEmpty() ? selectedAutotileId() : selectId;
    m_autotileList->blockSignals(true);
    m_autotileList->clear();

    int selectRow = -1;
    const QVector<TilesetAutotileInfo> catalog = tilesetAutotileCatalog(ed);
    for (int i = 0; i < catalog.size(); ++i) {
        const TilesetAutotileInfo& info = catalog.at(i);
        if (!info.autotile || !info.tileset || info.tilesetIdx < 0) continue;
        const TilesetAutotile& autotile = *info.autotile;
        QString name = autotile.name.trimmed().isEmpty() ? tr("Autotile %1").arg(i + 1)
                                                          : autotile.name.trimmed();
        if (!autotile.category.trimmed().isEmpty())
            name = QStringLiteral("[%1] %2").arg(autotile.category.trimmed(), name);
        QStringList badges;
        if (autotile.hasTerrain()) badges << tr("Terreno");
        if (autotile.animated()) badges << tr("Animado");
        if (!badges.isEmpty()) name += tr("  ·  %1").arg(badges.join(QStringLiteral(" + ")));
        auto* item = new QListWidgetItem(name, m_autotileList);
        item->setData(Qt::UserRole, autotile.id);
        item->setData(Qt::UserRole + 1, info.tilesetIdx);
        item->setToolTip(tr("ID: %1\nCategoria: %2\nTipo: %3\nRecurso global; o atlas físico é gerenciado internamente.")
                             .arg(autotile.id,
                                  autotile.category.trimmed().isEmpty() ? tr("Sem categoria") : autotile.category.trimmed(),
                                  autotileKindLabel(autotile)));
        if (autotile.id == keep) selectRow = m_autotileList->count() - 1;
    }
    if (selectRow < 0 && m_autotileList->count() > 0) selectRow = 0;
    if (selectRow >= 0) m_autotileList->setCurrentRow(selectRow);
    m_autotileList->blockSignals(false);
    refreshAutotilePanel();
}

void TilesetManagerDialog::refreshAutotilePanel()
{
    const QString id = selectedAutotileId();
    const int ownerIdx = selectedAutotileOwnerIdx();
    const TilesetAutotile* autotile = ownerIdx >= 0 ? tilesetAutotileById(ed, ownerIdx, id) : nullptr;
    const Tileset* ts = ed.tilesetAt(ownerIdx);
    const bool valid = autotile && ts;

    if (m_renameAutotile) m_renameAutotile->setEnabled(valid);
    if (m_deleteAutotile) m_deleteAutotile->setEnabled(valid);
    if (m_editAutotileAnimationAction) m_editAutotileAnimationAction->setEnabled(valid && autotile->animated());
    if (m_openTerrainEditor) m_openTerrainEditor->setVisible(valid);
    if (m_openTerrainEditor) m_openTerrainEditor->setEnabled(valid);
    if (m_tabs && m_terrainTabIndex >= 0) {
        m_tabs->setTabEnabled(m_terrainTabIndex, valid);
        m_tabs->setTabVisible(m_terrainTabIndex, valid);
    }
    if (m_terrainEditor) {
        if (valid) m_terrainEditor->setAutotileContext(ownerIdx, id);
        else m_terrainEditor->clearAutotileContext();
    }
    if (m_terrainView) {
        m_terrainView->setTilesetOverride(valid ? ownerIdx : -1);
        m_terrainView->updateGeometry();
        m_terrainView->resize(m_terrainView->sizeHint());
        m_terrainView->update();
    }
    if (m_autotileCategory) {
        QSignalBlocker blocker(m_autotileCategory);
        m_autotileCategory->setEnabled(valid);
        m_autotileCategory->setText(valid ? autotile->category : QString());
    }
    if (m_autotileBoundary) {
        m_autotileBoundary->blockSignals(true);
        m_autotileBoundary->setEnabled(valid);
        m_autotileBoundary->setVisible(valid);
        m_autotileBoundary->setChecked(valid && autotile->extendAtMapBoundary);
        m_autotileBoundary->blockSignals(false);
    }

    if (!valid) {
        if (m_autotilePriority) m_autotilePriority->setEnabled(false);
        if (m_autotileCollision) m_autotileCollision->setEnabled(false);
        if (m_autotileReflectionPreset) { QSignalBlocker blocker(m_autotileReflectionPreset); m_autotileReflectionPreset->setEnabled(false); m_autotileReflectionPreset->setCurrentIndex(0); }
        if (m_autotileReflectionOpacity) { QSignalBlocker blocker(m_autotileReflectionOpacity); m_autotileReflectionOpacity->setEnabled(false); m_autotileReflectionOpacity->setValue(-1); }
        if (m_autotileReflectionBlurMode) { QSignalBlocker blocker(m_autotileReflectionBlurMode); m_autotileReflectionBlurMode->setEnabled(false); m_autotileReflectionBlurMode->setCurrentIndex(0); }
        if (m_autotileReflectionBlurStrength) { QSignalBlocker blocker(m_autotileReflectionBlurStrength); m_autotileReflectionBlurStrength->setEnabled(false); m_autotileReflectionBlurStrength->setValue(0); }
        if (m_autotileDetails) m_autotileDetails->setText(tr("Nenhum Autotile selecionado."));
        if (m_terrainStatus) m_terrainStatus->setText(tr("Selecione um Autotile para escolher como ele se conecta aos tiles vizinhos."));
        return;
    }

    const QVector<QPoint> tiles = tilesetAutotileTiles(ed, ownerIdx, *autotile);
    auto commonInt = [&](int kind, int fallback) {
        bool first = true; int value = fallback;
        for (const QPoint& tile : tiles) {
            const int current = kind == 0 ? ts->tilePriority(tile.x(), tile.y())
                                          : ts->tileCollisionMask(tile.x(), tile.y());
            if (first) { value = current; first = false; }
            else if (value != current) return fallback;
        }
        return value;
    };
    if (m_autotilePriority) {
        QSignalBlocker blocker(m_autotilePriority);
        m_autotilePriority->setEnabled(true);
        const int value = commonInt(0, 0);
        const int index = m_autotilePriority->findData(value);
        m_autotilePriority->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_autotileCollision) {
        QSignalBlocker blocker(m_autotileCollision);
        m_autotileCollision->setEnabled(true);
        const int value = commonInt(1, 0);
        int index = m_autotileCollision->findData(value);
        if (index < 0) {
            m_autotileCollision->addItem(tr("Colisão personalizada"), value);
            index = m_autotileCollision->count() - 1;
        }
        m_autotileCollision->setCurrentIndex(index);
    }
    QString autotileReflectionPreset;
    if (m_autotileReflectionPreset) {
        QSignalBlocker blocker(m_autotileReflectionPreset);
        autotileReflectionPreset = resourceEffectPreset(autotile->resourceEffects, QStringLiteral("reflection"));
        m_autotileReflectionPreset->setEnabled(true);
        const int index = m_autotileReflectionPreset->findData(autotileReflectionPreset);
        m_autotileReflectionPreset->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (m_autotileReflectionOpacity) {
        QSignalBlocker blocker(m_autotileReflectionOpacity);
        const bool reflectionEnabled = !autotileReflectionPreset.isEmpty();
        m_autotileReflectionOpacity->setEnabled(reflectionEnabled);
        m_autotileReflectionOpacity->setValue(reflectionEnabled
            ? resourceEffectOpacityPercent(autotile->resourceEffects, QStringLiteral("reflection"))
            : -1);
    }
    if (m_autotileReflectionBlurMode && m_autotileReflectionBlurStrength) {
        QSignalBlocker modeBlocker(m_autotileReflectionBlurMode);
        QSignalBlocker strengthBlocker(m_autotileReflectionBlurStrength);
        const bool reflectionEnabled = !autotileReflectionPreset.isEmpty();
        const QString blurMode = reflectionEnabled ? resourceEffectBlurMode(autotile->resourceEffects, QStringLiteral("reflection")) : QStringLiteral("none");
        const int blurStrength = reflectionEnabled ? resourceEffectBlurStrength(autotile->resourceEffects, QStringLiteral("reflection")) : 0;
        m_autotileReflectionBlurMode->setEnabled(reflectionEnabled);
        const int index = m_autotileReflectionBlurMode->findData(blurMode);
        m_autotileReflectionBlurMode->setCurrentIndex(index >= 0 ? index : 0);
        m_autotileReflectionBlurStrength->setEnabled(reflectionEnabled && blurMode != QLatin1String("none"));
        m_autotileReflectionBlurStrength->setValue(blurStrength);
    }
    QString terrain = tr("nenhum");
    if (autotile->hasTerrain()) {
        const WangSet* set = wangSetById(ed, autotile->wangSetId);
        const WangColor* color = set ? set->colorById(autotile->wangColorId) : nullptr;
        terrain = set ? set->name : autotile->wangSetId;
        if (color) terrain += tr(" / %1").arg(color->name);
    }
    QString animation = tr("nenhuma");
    if (autotile->animated()) {
        for (const AnimatedAutotile& anim : ts->animatedAutotiles) {
            if (anim.id != autotile->animatedAutotileId) continue;
            animation = tr("%1 quadros · %2 quadros/s · %3")
                .arg(anim.frameCount()).arg(anim.fps, 0, 'f', 1)
                .arg(anim.synchronized ? tr("sincronizado") : tr("fase por célula"));
            break;
        }
    }
    m_autotileDetails->setText(tr(
        "<b>%1</b><br>Tipo: %2<br>Terreno: %3<br>Animação: %4")
        .arg(autotile->name.isEmpty() ? tr("Autotile") : autotile->name)
        .arg(autotileKindLabel(*autotile))
        .arg(terrain)
        .arg(animation));
    if (m_terrainStatus)
        m_terrainStatus->setText(autotile->hasTerrain()
            ? tr("Terreno configurado para este Autotile.")
            : tr("Este Autotile ainda não possui terreno configurado."));
}

void TilesetManagerDialog::renameAutotile()
{
    const QString id = selectedAutotileId();
    const int ownerIdx = selectedAutotileOwnerIdx();
    const TilesetAutotile* autotile = ownerIdx >= 0 ? tilesetAutotileById(ed, ownerIdx, id) : nullptr;
    if (!autotile) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renomear Autotile"), tr("Nome:"),
                                                QLineEdit::Normal, autotile->name, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!renameTilesetAutotile(ed, ownerIdx, id, name)) return;
    emit ed.tilesetsChanged();
    refreshAutotiles(id);
}

void TilesetManagerDialog::deleteAutotile()
{
    const QString id = selectedAutotileId();
    const int ownerIdx = selectedAutotileOwnerIdx();
    const TilesetAutotile* autotile = ownerIdx >= 0 ? tilesetAutotileById(ed, ownerIdx, id) : nullptr;
    if (!autotile) return;
    QString consequences;
    if (autotile->hasTerrain()) consequences += tr("\n• as regras de conexão exclusivas deste recurso serão removidas");
    if (autotile->animated()) consequences += tr("\n• a animação exclusiva vinculada será removida");
    if (QMessageBox::question(this, tr("Excluir Autotile"),
        tr("Excluir o recurso “%1”?\n\nO backing interno será removido se ficar sem uso.%2")
            .arg(autotile->name.isEmpty() ? tr("Autotile") : autotile->name, consequences))
        != QMessageBox::Yes) return;

    TilesetAutotileRemovalResult result;
    if (!removeTilesetAutotile(ed, ownerIdx, id, &result)) return;
    emit ed.wangChanged();
    emit ed.tilesetsChanged();
    emit ed.mapChanged();
    refreshAutotiles();
}

void TilesetManagerDialog::refreshPreview()
{
    const QString autotileId = selectedAutotileId();
    const int autotileOwner = selectedAutotileOwnerIdx();
    const TilesetAutotile* selectedAutotile = autotileOwner >= 0
        ? tilesetAutotileById(ed, autotileOwner, autotileId) : nullptr;
    if (selectedAutotile) {
        const QImage img = autotileResourcePreview(ed, autotileOwner, *selectedAutotile);
        if (!img.isNull()) {
            m_preview->setPixmap(QPixmap::fromImage(img));
            m_preview->setFixedSize(img.size());
        } else {
            m_preview->clear();
            m_preview->setText(tr("Prévia indisponível."));
            m_preview->setFixedSize(240, 120);
        }
        const Tileset* owner = ed.tilesetAt(autotileOwner);
        const QVector<QPoint> tiles = tilesetAutotileTiles(ed, autotileOwner, *selectedAutotile);
        m_info->setText(tr("<b>%1</b><br>Autotile · %2 tiles%3")
            .arg(selectedAutotile->name.isEmpty() ? tr("Autotile") : selectedAutotile->name)
            .arg(tiles.size())
            .arg(owner ? tr(" · Tile %1×%2 px").arg(owner->tilewidth).arg(owner->tileheight) : QString()));
        refreshAutotilePanel();
        return;
    }

    const int row = selectedTilesetIndex();
    const Tileset* ts = ed.tilesetAt(row);
    if (!ts) {
        m_preview->clear(); m_preview->setText(tr("Nenhum tileset.")); m_info->clear();
        refreshAutotilePanel();
        return;
    }
    // Prévia 2.0: escala física 1:1. O QScrollArea é responsável por navegar
    // atlas grandes; nunca reduzimos o tileset para "caber" no diálogo.
    QImage img = compactAnimatedPalettePreview(*ts);
    if (!img.isNull()) {
        m_preview->setPixmap(QPixmap::fromImage(img));
        m_preview->setFixedSize(img.size());
    } else {
        m_preview->clear();
        m_preview->setText(tr("Imagem indisponível."));
        m_preview->setFixedSize(240, 120);
    }
    const QStringList used = mapsUsingTileset(row);
    QString animationInfo = tr("nenhum");
    if (!ts->animatedAutotiles.isEmpty()) {
        QStringList parts;
        for (const AnimatedAutotile& anim : ts->animatedAutotiles)
            parts << tr("%1 (%2 quadros, %3 quadros/s%4)")
                .arg(anim.name.isEmpty() ? tr("Autotile") : anim.name)
                .arg(anim.frameCount()).arg(anim.fps, 0, 'f', 1)
                .arg(anim.synchronized ? tr(", sincronizado") : tr(", fase por célula"));
        animationInfo = parts.join(QStringLiteral(" · "));
    }
    int priorityTiles = 0;
    for (auto it = ts->tilePriorities.constBegin(); it != ts->tilePriorities.constEnd(); ++it)
        if (it.value() > 0) ++priorityTiles;
    int collisionTiles = 0;
    for (auto it = ts->tileCollisionMasks.constBegin(); it != ts->tileCollisionMasks.constEnd(); ++it)
        if ((it.value() & 0x0f) != 0) ++collisionTiles;
    int linkedSources = 0;
    int totalSources = 0;
    if (ts->combined) {
        totalSources = ts->combinedSources.size();
        for (const CombinedSource& source : ts->combinedSources) if (!source.sourcePath.trimmed().isEmpty()) ++linkedSources;
    } else {
        totalSources = 1;
        linkedSources = ts->sourcePath.trimmed().isEmpty() ? 0 : 1;
    }
    const QString originInfo = ts->generatedFromBake
        ? tr("Gerado por Converter de Camada de objetos")
        : tr("Tileset importado/editável");
    m_info->setText(tr("<b>%1</b><br><br>"
                       "<b>Atlas</b><br>%2×%3 tiles<br>Tile %4×%5 px<br>Total: %6<br>Origem: %7<br><br>"
                       "<b>Recursos</b><br>Partes: %8<br>Fontes vinculadas: %9/%10<br>Animações: %11<br><br>"
                       "<b>Propriedades</b><br>Prioridade: %12<br>Colisão: %13<br><br>"
                       "<b>Uso</b><br>%14")
        .arg(tr("%1 — PÁGINA %2/%3").arg(ts->name)
             .arg(qMax(1, core::tilesetPageIndices(ed, row).indexOf(row) + 1))
             .arg(qMax(1, core::tilesetPageIndices(ed, row).size())))
        .arg(ts->columns).arg(ts->rows).arg(ts->tilewidth).arg(ts->tileheight)
        .arg(ts->tilecount).arg(originInfo).arg(ts->combinedSources.size()).arg(linkedSources).arg(totalSources).arg(animationInfo)
        .arg(priorityTiles).arg(collisionTiles)
        .arg(used.isEmpty() ? tr("Não usado por mapas") : tr("Mapas: %1").arg(used.join(QStringLiteral(", ")))));
}

// Fluxos de criação/importação foram isolados em TilesetManagerFlows.cpp.

void TilesetManagerDialog::createTileset()
{
    int select = -1;
    if (runCreateTilesetFlow(ed, this, &select)) refresh(select);
}

void TilesetManagerDialog::importIntoTileset(int preferredRepresentative)
{
    int select = -1;
    if (runAddImageFlow(ed, this, preferredRepresentative, &select)) refresh(select);
}

void TilesetManagerDialog::importAutotile()
{
    QString createdId;
    const int normalTileset = ed.session.activeTilesetIdx;
    if (!runImportAutotileFlow(ed, this, &createdId)) return;
    refresh(normalTileset);
    if (!createdId.isEmpty()) {
        refreshAutotiles(createdId);
        if (m_tabs) m_tabs->setCurrentIndex(1);
    }
}

void TilesetManagerDialog::importA1Sheet()
{
    QString firstId;
    const int normalTileset = ed.session.activeTilesetIdx;
    if (!runImportA1Flow(ed, this, &firstId)) return;
    refresh(normalTileset);
    refreshAutotiles(firstId);
    if (m_tabs) m_tabs->setCurrentIndex(1);
}

void TilesetManagerDialog::reduceTilesetSize()
{
    const int index=selectedTilesetIndex();Tileset* ts=ed.tilesetAt(index);if(!ts)return;
    QDialog dlg(this);dlg.setWindowTitle(tr("Reduzir tamanho do Tileset"));
    auto* outer=new QVBoxLayout(&dlg);
    auto* note=new QLabel(tr("Reduz apenas pela <b>direita</b> e por <b>baixo</b>. O tamanho de cada tile (%1×%2 px) não muda e os IDs restantes permanecem iguais.").arg(ts->tilewidth).arg(ts->tileheight),&dlg);note->setWordWrap(true);outer->addWidget(note);
    auto* form=new QFormLayout;auto* cols=new QSpinBox(&dlg);auto* rows=new QSpinBox(&dlg);
    cols->setRange(1,qMax(1,ts->columns));cols->setValue(ts->columns);rows->setRange(1,qMax(1,ts->rows));rows->setValue(ts->rows);
    form->addRow(tr("Colunas:"),cols);form->addRow(tr("Linhas:"),rows);outer->addLayout(form);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dlg);buttons->button(QDialogButtonBox::Ok)->setText(tr("Reduzir"));outer->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    if(dlg.exec()!=QDialog::Accepted)return;
    const int nc=cols->value(),nr=rows->value();if(nc==ts->columns&&nr==ts->rows)return;

    const QStringList usage=referencesOutsideCrop(ed,index,nc,nr);
    if(!usage.isEmpty()){QMessageBox::warning(this,tr("Não é seguro reduzir"),tr("A área que seria removida ainda é usada:\n• %1\n\nRemova essas referências do mapa primeiro.").arg(usage.mid(0,8).join(QStringLiteral("\n• "))));return;}
    for(const AnimatedAutotile& a:ts->animatedAutotiles){
        for(const QPoint& o:a.frameOrigins)if(o.x()+a.cols>nc||o.y()+a.rows>nr){QMessageBox::warning(this,tr("Não é seguro reduzir"),tr("O autotile animado “%1” possui quadros na área que seria removida.").arg(a.name));return;}
    }
    for (const TilesetAutotile& a : ed.autotiles) {
        if (a.tilesetId != ts->id) continue;
        if (a.hasRegion() && (a.baseX + a.cols > nc || a.baseY + a.rows > nr)) {
            QMessageBox::warning(this, tr("Não é seguro reduzir"),
                tr("O Autotile “%1” usa esta área física. Remova/reimporte o recurso antes de reduzir.")
                    .arg(a.name.isEmpty() ? tr("Sem nome") : a.name));
            return;
        }
    }
    for(const CombinedSource& c:ts->combinedSources)if(c.x+c.cols>nc||c.y+c.rows>nr){QMessageBox::warning(this,tr("Não é seguro reduzir"),tr("A parte registrada “%1” ocupa a área que seria removida.").arg(c.name));return;}

    const int newW=ts->margin*2+nc*ts->tilewidth+qMax(0,nc-1)*ts->spacing;
    const int newH=ts->margin*2+nr*ts->tileheight+qMax(0,nr-1)*ts->spacing;
    const QRect removedRight(newW,0,qMax(0,ts->image.width()-newW),ts->image.height());
    const QRect removedBottom(0,newH,qMin(newW,ts->image.width()),qMax(0,ts->image.height()-newH));
    if((hasVisiblePixels(ts->image,removedRight)||hasVisiblePixels(ts->image,removedBottom))&&
       QMessageBox::question(this,tr("Apagar tiles?"),tr("Esta área contém imagens que não estão em uso nos mapas. Apagá-las do tileset?"))!=QMessageBox::Yes)return;

    ts->image=ts->image.copy(0,0,qMin(newW,ts->image.width()),qMin(newH,ts->image.height()));
    ts->recomputeGrid();
    ed.reindexTilesetGids();ed.markDirty();emit ed.tilesetsChanged();emit ed.mapChanged();refresh(index);
}

void TilesetManagerDialog::renameTileset()
{
    const int row = selectedTilesetIndex(); Tileset* ts = ed.tilesetAt(row); if (!ts) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renomear Tileset"), tr("Novo nome:"),
                                               QLineEdit::Normal, ts->name, &ok).trimmed();
    if (!ok || name.isEmpty() || name == ts->name) return;
    const QString group = core::tilesetPageGroupKey(*ts);
    for (const Tileset& other : ed.tilesets) {
        if (other.name == name && core::tilesetPageGroupKey(other) != group) {
            QMessageBox::warning(this, tr("Nome já usado"),
                tr("Já existe outro Tileset chamado “%1”. Use outro nome para não misturar dois conjuntos de páginas diferentes.").arg(name));
            return;
        }
    }
    for (Tileset& page : ed.tilesets)
        if (core::tilesetPageGroupKey(page) == group) page.name = name;
    ed.markDirty(); emit ed.tilesetsChanged(); refresh(row);
}

void TilesetManagerDialog::duplicateTileset()
{
    const int row = selectedTilesetIndex();
    const Tileset* selected = ed.tilesetAt(row);
    if (!selected) return;

    const QVector<int> sourcePages = core::tilesetPageIndices(ed, row);
    if (sourcePages.isEmpty()) return;

    QString baseName = selected->name + tr(" (cópia)");
    QString copyName = baseName;
    int suffix = 2;
    auto logicalNameExists = [this](const QString& name) {
        QSet<QString> seen;
        for (const Tileset& ts : ed.tilesets) {
            const QString group = core::tilesetPageGroupKey(ts);
            if (seen.contains(group)) continue;
            seen.insert(group);
            if (ts.name.compare(name, Qt::CaseSensitive) == 0) return true;
        }
        return false;
    };
    while (logicalNameExists(copyName)) copyName = tr("%1 %2").arg(baseName).arg(suffix++);

    QString newGroup;
    int firstAdded = -1;
    for (int i = 0; i < sourcePages.size(); ++i) {
        const int sourceIndex = sourcePages.at(i);
        const Tileset* source = ed.tilesetAt(sourceIndex);
        if (!source) continue;
        Tileset copy = *source;
        regenerateTilesetResourceIds(copy);
        if (newGroup.isEmpty()) newGroup = copy.id;
        copy.pageGroupId = newGroup;
        copy.pageIndex = i;
        copy.name = copyName;
        const int targetIndex = ed.addTileset(copy);
        if (firstAdded < 0) firstAdded = targetIndex;
        copyTilesetTerrainLabels(ed, sourceIndex, targetIndex);
    }
    if (firstAdded < 0) return;
    core::renumberTilesetPages(ed, newGroup);
    reconcileTilesetAutotiles(ed);
    ed.markDirty();
    emit ed.tilesetsChanged();
    refresh(firstAdded);
}

void TilesetManagerDialog::updateSourceImage()
{
    const int row = selectedTilesetIndex();
    Tileset* ts = ed.tilesetAt(row);
    if (!ts) return;

    int sourceIndex = -1;
    if (ts->combined) {
        if (ts->combinedSources.isEmpty()) {
            QMessageBox::information(this, tr("Atualizar imagem de origem"),
                                     tr("Este tileset combinado ainda não possui partes vinculáveis."));
            return;
        }
        QStringList labels;
        for (const CombinedSource& source : ts->combinedSources) {
            const QString file = source.sourcePath.isEmpty()
                ? tr("sem arquivo vinculado") : QFileInfo(source.sourcePath).fileName();
            labels << tr("%1 — bloco %2×%3 — %4")
                          .arg(source.name.isEmpty() ? tr("Imagem") : source.name)
                          .arg(source.cols).arg(source.rows).arg(file);
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Atualizar imagem de origem"),
                                                     tr("Parte do tileset:"), labels, 0, false, &ok);
        if (!ok || chosen.isEmpty()) return;
        sourceIndex = labels.indexOf(chosen);
        if (sourceIndex < 0) return;
    }

    const QString path = AssetBrowserDialog::chooseImage(ed, this, QStringLiteral("Tilesets"));
    if (path.isEmpty()) return;

    QString error;
    if (!bindTilesetSource(ed, row, sourceIndex, path, &error)) {
        QMessageBox::warning(this, tr("Atualizar imagem de origem"), error);
        return;
    }
    ed.markDirty();
    pixmapCache().invalidate(row);
    emit ed.tilesetsChanged();
    emit ed.mapChanged();
    refresh(row);
    QMessageBox::information(this, tr("Fonte vinculada"),
                             tr("A imagem foi atualizada sem mover os tiles já usados no mapa.\n\n"
                                "A partir de agora, ao salvar alterações nesse arquivo, o LUDO recarrega o tileset automaticamente."));
}

void TilesetManagerDialog::editAnimations(const QString& autotileId)
{
    const int row = selectedAutotileOwnerIdx();
    Tileset* ts = ed.tilesetAt(row);
    if (!ts) return;
    const TilesetAutotile* resource = autotileId.isEmpty() ? nullptr
        : tilesetAutotileById(ed, row, autotileId);
    if (!resource || !resource->animated()) {
        QMessageBox::information(this, tr("Autotile sem animação"),
                                 tr("O Autotile selecionado não possui animação vinculada."));
        return;
    }
    int index = -1;
    for (int i = 0; i < ts->animatedAutotiles.size(); ++i)
        if (ts->animatedAutotiles.at(i).id == resource->animatedAutotileId) { index = i; break; }
    if (index < 0) return;
    AnimatedAutotile& anim = ts->animatedAutotiles[index];

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Editar Autotile Animado"));
    dlg.resize(620, 560);
    auto* outer = new QVBoxLayout(&dlg);

    auto* preview = new QLabel(&dlg);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(420, 220);
    preview->setStyleSheet(QStringLiteral("background:#181818;border:1px solid #444;"));
    outer->addWidget(preview);
    auto* previewInfo = new QLabel(&dlg);
    previewInfo->setAlignment(Qt::AlignCenter);
    outer->addWidget(previewInfo);

    auto* form = new QFormLayout;
    auto* name = new QLineEdit(resource->name.isEmpty() ? anim.name : resource->name, &dlg);
    auto* fps = new QDoubleSpinBox(&dlg);
    fps->setRange(0.1, 120.0);
    fps->setDecimals(1);
    fps->setSingleStep(0.5);
    fps->setValue(qBound(0.1, anim.fps, 120.0));
    fps->setSuffix(tr(" quadros/s"));
    fps->setToolTip(tr("Velocidade própria deste autotile. Não altera os demais autotiles animados do tileset."));
    auto* defaultFps = new QPushButton(tr("Usar velocidade padrão (6 quadros/s)"), &dlg);
    auto* speedRow = new QWidget(&dlg);
    auto* speedLayout = new QHBoxLayout(speedRow);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->addWidget(fps, 1);
    speedLayout->addWidget(defaultFps);

    auto* loop = new QCheckBox(tr("Loop"), &dlg); loop->setChecked(anim.loop);
    auto* ping = new QCheckBox(tr("Ping-Pong"), &dlg); ping->setChecked(anim.pingPong);
    auto* sync = new QComboBox(&dlg);
    sync->addItem(tr("Sincronizado — recomendado para água, lava e terrenos conectados"), true);
    sync->addItem(tr("Fase diferente por célula"), false);
    sync->setCurrentIndex(anim.synchronized ? 0 : 1);
    auto* pausePreview = new QCheckBox(tr("Pausar prévia"), &dlg);

    form->addRow(tr("Nome:"), name);
    form->addRow(tr("Quadros da animação:"), new QLabel(QString::number(anim.frameCount()), &dlg));
    form->addRow(tr("Velocidade:"), speedRow);
    form->addRow(tr("Prévia:"), pausePreview);
    form->addRow(tr("Reprodução:"), loop);
    form->addRow(QString(), ping);
    form->addRow(tr("Sincronização:"), sync);
    outer->addLayout(form);

    auto* note = new QLabel(tr("A velocidade vale apenas para este autotile e não altera os mapas já pintados."), &dlg);
    note->setWordWrap(true);
    outer->addWidget(note);

    qint64 previewElapsedMs = 0;
    QElapsedTimer previewClock;
    previewClock.start();
    auto renderPreview = [&] {
        if (anim.frameCount() <= 0 || anim.frameOrigins.isEmpty() || ts->image.isNull()) {
            preview->setText(tr("Prévia indisponível"));
            previewInfo->clear();
            return;
        }
        AnimatedAutotile current = anim;
        current.fps = fps->value();
        current.loop = loop->isChecked();
        current.pingPong = ping->isChecked();
        current.synchronized = sync->currentData().toBool();
        const int frame = qBound(0, animatedAutotileFrame(current, previewElapsedMs, 0), current.frameCount() - 1);
        const QPoint origin = current.frameOrigins.at(frame);
        const int strideX = ts->tilewidth + ts->spacing;
        const int strideY = ts->tileheight + ts->spacing;
        const QRect src(ts->margin + origin.x() * strideX,
                        ts->margin + origin.y() * strideY,
                        current.cols * ts->tilewidth + qMax(0, current.cols - 1) * ts->spacing,
                        current.rows * ts->tileheight + qMax(0, current.rows - 1) * ts->spacing);
        const QImage frameImage = ts->image.copy(src.intersected(ts->image.rect()));
        if (frameImage.isNull()) {
            preview->setText(tr("Frame fora do atlas"));
        } else {
            const QSize box(qMax(1, preview->width() - 16), qMax(1, preview->height() - 16));
            const QImage shown = frameImage.scaled(box, Qt::KeepAspectRatio, Qt::FastTransformation);
            preview->setPixmap(QPixmap::fromImage(shown));
        }
        previewInfo->setText(tr("Quadro %1/%2 · %3 quadros/s")
                             .arg(frame + 1).arg(current.frameCount()).arg(current.fps, 0, 'f', 1));
    };

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setInterval(16);
    connect(previewTimer, &QTimer::timeout, &dlg, [&] {
        const qint64 delta = qMax<qint64>(0, previewClock.restart());
        if (!pausePreview->isChecked()) previewElapsedMs += delta;
        renderPreview();
    });
    connect(fps, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, [&](double) { renderPreview(); });
    connect(loop, &QCheckBox::toggled, &dlg, [&](bool) { renderPreview(); });
    connect(ping, &QCheckBox::toggled, &dlg, [&](bool) { renderPreview(); });
    connect(sync, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int) { renderPreview(); });
    connect(defaultFps, &QPushButton::clicked, &dlg, [&] { fps->setValue(6.0); });
    connect(pausePreview, &QCheckBox::toggled, &dlg, [&](bool) { previewClock.restart(); renderPreview(); });
    previewTimer->start();
    renderPreview();

    auto* buttonsRow = new QHBoxLayout;
    auto* removeAnim = new QPushButton(icons::get(QStringLiteral("remove")), tr("Remover animação"), &dlg);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    if (auto* save = buttons->button(QDialogButtonBox::Save)) save->setText(tr("Aplicar"));
    buttonsRow->addWidget(removeAnim);
    buttonsRow->addStretch(1);
    buttonsRow->addWidget(buttons);
    outer->addLayout(buttonsRow);

    bool removeRequested = false;
    connect(removeAnim, &QPushButton::clicked, &dlg, [&] {
        if (QMessageBox::question(&dlg, tr("Remover animação"),
                                  tr("Remover a animação? Os quadros continuam no tileset.")) != QMessageBox::Yes) return;
        removeRequested = true;
        dlg.accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    if (removeRequested) {
        const QString animationId = anim.id;
        ts->animatedAutotiles.remove(index);
        for (TilesetAutotile& autotile : ed.autotiles)
            if (autotile.tilesetId == ts->id && autotile.animatedAutotileId == animationId)
                autotile.animatedAutotileId.clear();
    } else {
        const QString resourceName = name->text().trimmed();
        anim.name = resourceName;
        anim.fps = fps->value();
        anim.loop = loop->isChecked();
        anim.pingPong = ping->isChecked();
        anim.synchronized = sync->currentData().toBool();
        if (!resourceName.isEmpty()) renameTilesetAutotile(ed, row, autotileId, resourceName);
    }
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.mapChanged();
    refresh(ed.session.activeTilesetIdx);
    refreshAutotiles(autotileId);
}

QStringList TilesetManagerDialog::mapsUsingTileset(int tilesetIdx) const
{
    QStringList names;
    for (const MapDoc& doc : ed.docs) {
        bool used = false; collectLayerUsage(doc.layers, tilesetIdx, &used);
        if (used) names << doc.name;
    }
    return names;
}

void TilesetManagerDialog::deleteTileset()
{
    const int row = selectedTilesetIndex();
    const Tileset* ts = ed.tilesetAt(row);
    if (!ts) return;
    const QString name = ts->name;
    const QVector<int> pages = core::tilesetPageIndices(ed, row);
    if (pages.isEmpty()) return;

    QStringList used;
    int semanticUses = 0;
    for (int pageIndex : pages) {
        for (const QString& map : mapsUsingTileset(pageIndex)) if (!used.contains(map)) used << map;
        const Tileset* page = ed.tilesetAt(pageIndex);
        if (page) semanticUses += core::findProjectUses(ed, core::ReferenceSymbolKind::Tileset, page->id).size();
    }

    QString text = tr("Excluir o Tileset “%1” e suas %2 página(s)?").arg(name).arg(pages.size());
    if (!used.isEmpty()) text += tr("\n\n⚠ Ele é usado por %1 mapa(s):\n%2\n\nA exclusão removerá/remapeará essas referências.")
                                     .arg(used.size()).arg(used.join(QStringLiteral("\n")));
    if (semanticUses > 0) text += tr("\n\nA Busca Global encontrou também %1 referência(s) semântica(s) por ID. Essas referências não podem ser remapeadas automaticamente para outro tileset.").arg(semanticUses);
    if (QMessageBox::warning(this, tr("Excluir Tileset"), text, QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes) return;

    QVector<int> descending = pages;
    std::sort(descending.begin(), descending.end(), std::greater<int>());
    for (int pageIndex : descending) ed.removeTileset(pageIndex);
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.mapChanged();
    refresh(qMin(row, ed.tilesets.size() - 1));
}

void TilesetManagerDialog::deleteTilesetPage()
{
    const int row = selectedTilesetIndex();
    const Tileset* ts = ed.tilesetAt(row);
    if (!ts) return;
    const QVector<int> pages = core::tilesetPageIndices(ed, row);
    if (pages.size() <= 1) {
        QMessageBox::information(this, tr("Excluir página"),
            tr("Este Tileset possui somente uma página. Use Excluir para remover o Tileset inteiro."));
        return;
    }
    const int logicalPage = pages.indexOf(row) + 1;
    const QString group = core::tilesetPageGroupKey(*ts);
    QString text = tr("Excluir a Página %1/%2 do Tileset “%3”?").arg(logicalPage).arg(pages.size()).arg(ts->name);
    const QStringList used = mapsUsingTileset(row);
    if (!used.isEmpty()) text += tr("\n\n⚠ Esta página é usada por %1 mapa(s):\n%2\n\nAs referências desta página serão removidas/remapeadas.")
                                  .arg(used.size()).arg(used.join(QStringLiteral("\n")));
    const int semanticUses = core::findProjectUses(ed, core::ReferenceSymbolKind::Tileset, ts->id).size();
    if (semanticUses > 0) text += tr("\n\nA Busca Global encontrou também %1 referência(s) semântica(s) por ID.").arg(semanticUses);
    if (QMessageBox::warning(this, tr("Excluir página"), text, QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes) return;

    ed.removeTileset(row);
    core::renumberTilesetPages(ed, group);
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.mapChanged();
    refresh(qMin(row, ed.tilesets.size() - 1));
}


} // namespace ui
