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

bool choosePlacement(QWidget* parent, const Tileset& ts, const NamedImage& source,
                     int* outCol, int* outRow)
{
    if (!outCol || !outRow || source.img.isNull()) return false;
    const QPoint suggestion = findFreeSlot(ts, source.img);

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Posicionar parte no Tileset"));
    dlg.resize(760, 620);
    auto* outer = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(QObject::tr(
        "Escolha onde esta imagem ficará no tileset. A posição é em <b>coluna/linha de tiles</b>. "
        "O preview é atualizado antes de confirmar; o conteúdo já existente nunca é deslocado."), &dlg);
    hint->setWordWrap(true); outer->addWidget(hint);

    auto* form = new QFormLayout;
    auto* col = new QSpinBox(&dlg); col->setRange(0, 9999); col->setValue(suggestion.x());
    auto* row = new QSpinBox(&dlg); row->setRange(0, 9999); row->setValue(suggestion.y());
    form->addRow(QObject::tr("Coluna:"), col);
    form->addRow(QObject::tr("Linha:"), row);
    outer->addLayout(form);

    auto* warning = new QLabel(&dlg); warning->setWordWrap(true); outer->addWidget(warning);
    auto* scroll = new QScrollArea(&dlg); scroll->setWidgetResizable(false);
    auto* preview = new QLabel(scroll); preview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll->setWidget(preview); outer->addWidget(scroll, 1);

    auto update = [&] {
        const int c = col->value(), r = row->value();
        const int tw = qMax(1, ts.tilewidth), th = qMax(1, ts.tileheight);
        const int sourceCols = (source.img.width() + tw - 1) / tw;
        const int sourceRows = (source.img.height() + th - 1) / th;
        const int cols = qMax(ts.columns, c + sourceCols);
        const int rows = qMax(ts.rows, r + sourceRows);
        QImage image(cols * tw, rows * th, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.drawImage(0, 0, ts.image);
        p.setOpacity(0.65);
        p.drawImage(c * tw, r * th, source.img);
        p.setOpacity(1.0);
        p.setPen(QPen(QColor("#42a5ff"), qMax(1, tw / 16)));
        p.drawRect(QRect(c * tw, r * th, sourceCols * tw, sourceRows * th).adjusted(0,0,-1,-1));
        p.setPen(QPen(QColor(255,255,255,45), 1));
        for (int x = 0; x <= cols; ++x) p.drawLine(x*tw, 0, x*tw, rows*th);
        for (int y = 0; y <= rows; ++y) p.drawLine(0, y*th, cols*tw, y*th);
        p.end();
        const double scale = qMin(1.0, 680.0 / qMax(1, image.width()));
        const QImage shown = scale < 1.0 ? image.scaled(qRound(image.width()*scale),
                                                       qRound(image.height()*scale),
                                                       Qt::KeepAspectRatio, Qt::FastTransformation)
                                         : image;
        preview->setPixmap(QPixmap::fromImage(shown));
        preview->setFixedSize(shown.size());
        const int overwritten = countOverwrittenTiles(ts, source.img, c, r);
        warning->setText(overwritten > 0
            ? QObject::tr("⚠ Esta posição substituirá %1 tile(s) não vazio(s).").arg(overwritten)
            : QObject::tr("Posição livre — nenhum tile existente será substituído."));
        warning->setStyleSheet(overwritten > 0 ? QStringLiteral("color:#ffb74d")
                                               : QStringLiteral("color:#7ecf8a"));
    };
    QObject::connect(col, &QSpinBox::valueChanged, &dlg, [&](int){ update(); });
    QObject::connect(row, &QSpinBox::valueChanged, &dlg, [&](int){ update(); });
    update();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Inserir nesta posição"));
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    if (countOverwrittenTiles(ts, source.img, col->value(), row->value()) > 0 &&
        QMessageBox::question(parent, QObject::tr("Substituir tiles?"),
                              QObject::tr("Há tiles ocupados nessa posição. Deseja realmente substituí-los?"))
            != QMessageBox::Yes) return false;
    *outCol = col->value(); *outRow = row->value();
    return true;
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

QImage convertA1Surface(const QImage& sheet,int tileSize,int tileX,int tileY)
{
    autotile::Converter c;
    c.img=sheet;c.tileSize=tileSize;c.mode=atc::RPGM_2x3;c.layout=autotile::Layout::Vertical;
    // Selection guarda o centro do bloco 2x3 em pixels.
    c.selections.push_back(autotile::Selection{(tileX+1)*tileSize,
                                                tileY*tileSize+(3*tileSize)/2,
                                                atc::RPGM_2x3});
    return c.buildOutput();
}

int logicalTilesetByExactName(const Editor& ed, const QString& name)
{
    QSet<QString> seen;
    for (int idx : visibleTilesetIndices(ed)) {
        const Tileset* ts = ed.tilesetAt(idx);
        if (!ts) continue;
        const QString group = core::tilesetPageGroupKey(*ts);
        if (seen.contains(group)) continue;
        seen.insert(group);
        if (ts->name == name) return idx;
    }
    return -1;
}

bool appendIncomingToLogicalTileset(Editor& ed, QWidget* parent, int representative,
                                    Tileset incoming, int* selectedTilesetIndex)
{
    const Tileset* anchor = ed.tilesetAt(representative);
    if (!anchor) return false;
    const QString logicalGroup = core::tilesetPageGroupKey(*anchor);
    const QString logicalName = anchor->name;
    const QString logicalCategory = anchor->category;
    const bool logicalPaletteVisible = anchor->paletteVisible;
    const int logicalTileWidth = anchor->tilewidth;
    const int logicalTileHeight = anchor->tileheight;

    if (incoming.tilewidth != logicalTileWidth || incoming.tileheight != logicalTileHeight) {
        QMessageBox::warning(parent, QObject::tr("Tamanho de tile diferente"),
            QObject::tr("O Tileset “%1” usa tiles de %2×%3 px, mas a imagem importada usa %4×%5 px.")
                .arg(logicalName).arg(logicalTileWidth).arg(logicalTileHeight)
                .arg(incoming.tilewidth).arg(incoming.tileheight));
        return false;
    }

    QMessageBox mode(QMessageBox::Question, QObject::tr("Adicionar imagem"),
        QObject::tr("Como deseja adicionar esta imagem ao Tileset “%1”?").arg(logicalName),
        QMessageBox::NoButton, parent);
    QPushButton* newPage = mode.addButton(QObject::tr("Nova página"), QMessageBox::AcceptRole);
    QPushButton* existingPage = mode.addButton(QObject::tr("Página existente"), QMessageBox::ActionRole);
    QPushButton* cancel = mode.addButton(QMessageBox::Cancel);
    mode.exec();
    if (mode.clickedButton() == cancel) return false;

    if (mode.clickedButton() == newPage) {
        QVector<Tileset> split = splitTilesetForTextureLimit(incoming, kPortableTilesetTextureLimit);
        const int base = core::tilesetPageIndices(ed, representative).size();
        int firstAdded = -1;
        for (int i = 0; i < split.size(); ++i) {
            Tileset page = split.at(i);
            page.pageGroupId = logicalGroup;
            page.pageIndex = base + i;
            page.name = logicalName;
            page.category = logicalCategory;
            page.paletteVisible = logicalPaletteVisible;
            const int added = ed.addTileset(page);
            if (firstAdded < 0) firstAdded = added;
        }
        core::renumberTilesetPages(ed, logicalGroup);
        ed.markDirty();
        emit ed.tilesetsChanged();
        emit ed.mapChanged();
        if (selectedTilesetIndex) *selectedTilesetIndex = firstAdded >= 0 ? firstAdded : representative;
        if (split.size() > 1) {
            QMessageBox::information(parent, QObject::tr("Páginas criadas"),
                QObject::tr("A imagem ultrapassa o limite de %1×%1 px e foi organizada automaticamente em %2 páginas do mesmo Tileset.")
                    .arg(kPortableTilesetTextureLimit).arg(split.size()));
        }
        return true;
    }
    if (mode.clickedButton() != existingPage) return false;

    const QImage clean = extractTileBlock(incoming, 0, 0, incoming.columns, incoming.rows);
    if (clean.isNull()) return false;
    QVector<int> candidates;
    QStringList pageLabels;
    const QVector<int> pages = core::tilesetPageIndices(ed, representative);
    for (int pos = 0; pos < pages.size(); ++pos) {
        const int idx = pages.at(pos);
        const Tileset* page = ed.tilesetAt(idx);
        if (!page || page->spacing != 0 || page->margin != 0 ||
            page->tilewidth != incoming.tilewidth || page->tileheight != incoming.tileheight) continue;
        const QPoint slot = findFreeSlot(*page, clean);
        const int addCols = (clean.width() + page->tilewidth - 1) / page->tilewidth;
        const int addRows = (clean.height() + page->tileheight - 1) / page->tileheight;
        const int cols = qMax(page->columns, slot.x() + addCols);
        const int rows = qMax(page->rows, slot.y() + addRows);
        if (cols * page->tilewidth > kPortableTilesetTextureLimit ||
            rows * page->tileheight > kPortableTilesetTextureLimit) continue;
        candidates.push_back(idx);
        pageLabels << QObject::tr("Página %1 — %2×%3 px")
                         .arg(pos + 1).arg(page->image.width()).arg(page->image.height());
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Nenhuma página disponível"),
            QObject::tr("Nenhuma página existente possui espaço suficiente para esta imagem. Use “Nova página”."));
        return false;
    }

    bool ok = false;
    const QString targetLabel = QInputDialog::getItem(parent, QObject::tr("Página existente"),
        QObject::tr("Páginas com espaço suficiente:"), pageLabels, 0, false, &ok);
    if (!ok) return false;
    const int candidateRow = pageLabels.indexOf(targetLabel);
    if (candidateRow < 0) return false;
    const int targetIndex = candidates.at(candidateRow);
    Tileset* target = ed.tilesetAt(targetIndex);
    if (!target) return false;

    NamedImage source{clean,
                      incoming.name.trimmed().isEmpty() ? QObject::tr("Imagem") : incoming.name,
                      incoming.sourcePath};
    int col = 0, row = 0;
    QMessageBox placement(QMessageBox::Question, QObject::tr("Posicionamento"),
        QObject::tr("Como deseja posicionar a imagem na página escolhida?"),
        QMessageBox::NoButton, parent);
    QPushButton* automatic = placement.addButton(QObject::tr("Encontrar espaço automaticamente"), QMessageBox::AcceptRole);
    QPushButton* manual = placement.addButton(QObject::tr("Escolher posição"), QMessageBox::ActionRole);
    QPushButton* placementCancel = placement.addButton(QMessageBox::Cancel);
    placement.exec();
    if (placement.clickedButton() == placementCancel) return false;
    if (placement.clickedButton() == manual) {
        if (!choosePlacement(parent, *target, source, &col, &row)) return false;
    } else if (placement.clickedButton() == automatic) {
        const QPoint slot = findFreeSlot(*target, clean);
        col = slot.x(); row = slot.y();
    } else return false;

    const int addCols = (clean.width() + target->tilewidth - 1) / target->tilewidth;
    const int addRows = (clean.height() + target->tileheight - 1) / target->tileheight;
    const int resultingCols = qMax(target->columns, col + addCols);
    const int resultingRows = qMax(target->rows, row + addRows);
    if (resultingCols * target->tilewidth > kPortableTilesetTextureLimit ||
        resultingRows * target->tileheight > kPortableTilesetTextureLimit) {
        QMessageBox::warning(parent, QObject::tr("A imagem não cabe nesta página"),
            QObject::tr("A posição escolhida ultrapassaria o limite de %1×%1 px. Escolha outra posição ou crie uma nova página.")
                .arg(kPortableTilesetTextureLimit));
        return false;
    }

    InsertAtResult result;
    QString error;
    if (!insertCombinedSourceAt(*target, source, col, row, &result, &error)) {
        QMessageBox::warning(parent, QObject::tr("Não foi possível inserir"), error);
        return false;
    }
    ed.reindexTilesetGids();
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.mapChanged();
    if (selectedTilesetIndex) *selectedTilesetIndex = targetIndex;
    return true;
}

} // namespace

bool TilesetManagerDialog::runCreateTilesetFlow(Editor& ed, QWidget* parent, int* selectedTilesetIndex)
{
    CombinedTilesetDialog dlg(ed, parent);
    if (dlg.exec() != QDialog::Accepted) return false;
    Tileset result = dlg.result();
    if (result.name.trimmed().isEmpty()) result.name = QObject::tr("Novo Tileset");

    // "Novo" nunca junta recursos silenciosamente. Nome igual apenas abre uma
    // escolha explícita para o usuário decidir se quer adicionar ao existente.
    int existing = logicalTilesetByExactName(ed, result.name);
    if (existing >= 0) {
        QMessageBox conflict(QMessageBox::Question, QObject::tr("Nome já usado"),
            QObject::tr("Já existe um Tileset chamado “%1”.\n\nO que deseja fazer?").arg(result.name),
            QMessageBox::NoButton, parent);
        QPushButton* addExisting = conflict.addButton(QObject::tr("Adicionar ao Tileset existente"), QMessageBox::AcceptRole);
        QPushButton* chooseName = conflict.addButton(QObject::tr("Escolher outro nome"), QMessageBox::ActionRole);
        QPushButton* cancel = conflict.addButton(QMessageBox::Cancel);
        conflict.exec();
        if (conflict.clickedButton() == cancel) return false;
        if (conflict.clickedButton() == addExisting)
            return appendIncomingToLogicalTileset(ed, parent, existing, result, selectedTilesetIndex);
        if (conflict.clickedButton() != chooseName) return false;

        QString candidate = result.name;
        while (true) {
            bool ok = false;
            candidate = QInputDialog::getText(parent, QObject::tr("Nome do novo Tileset"),
                QObject::tr("Digite um nome que ainda não exista:"), QLineEdit::Normal,
                candidate, &ok).trimmed();
            if (!ok) return false;
            if (candidate.isEmpty()) {
                QMessageBox::information(parent, QObject::tr("Nome vazio"), QObject::tr("Digite um nome para o Tileset."));
                continue;
            }
            if (logicalTilesetByExactName(ed, candidate) >= 0) {
                QMessageBox::information(parent, QObject::tr("Nome já usado"),
                    QObject::tr("Já existe um Tileset chamado “%1”. Escolha outro nome.").arg(candidate));
                continue;
            }
            result.name = candidate;
            break;
        }
    }

    QVector<Tileset> pages = splitTilesetForTextureLimit(result, kPortableTilesetTextureLimit);
    if (pages.isEmpty()) return false;
    const QString group = core::tilesetPageGroupKey(pages.first());
    int first = -1;
    for (int i = 0; i < pages.size(); ++i) {
        Tileset page = pages.at(i);
        page.pageGroupId = group;
        page.pageIndex = i;
        page.name = result.name;
        const int added = ed.addTileset(page);
        if (first < 0) first = added;
    }
    core::renumberTilesetPages(ed, group);
    if (selectedTilesetIndex) *selectedTilesetIndex = first;

    if (pages.size() > 1) {
        QMessageBox::information(parent, QObject::tr("Páginas do Tileset"),
            QObject::tr("A imagem ultrapassa o limite de %1×%1 px e foi organizada automaticamente em %2 páginas do Tileset “%3”.")
                .arg(kPortableTilesetTextureLimit).arg(pages.size()).arg(result.name));
    }
    return true;
}

bool TilesetManagerDialog::runAddImageFlow(Editor& ed, QWidget* parent, int representativeTilesetIndex,
                                            int* selectedTilesetIndex)
{
    const Tileset* anchor = ed.tilesetAt(representativeTilesetIndex);
    if (!anchor || anchor->internalAutotileAtlas) {
        QMessageBox::information(parent, QObject::tr("Adicionar imagem"),
            QObject::tr("Selecione primeiro o Tileset que receberá a imagem."));
        return false;
    }

    // Sempre convertemos qualquer página recebida para o representante lógico.
    const QVector<int> pages = core::tilesetPageIndices(ed, representativeTilesetIndex);
    const int representative = pages.isEmpty() ? representativeTilesetIndex : pages.first();

    const Tileset* logical = ed.tilesetAt(representative);
    if (!logical) return false;
    CombinedTilesetDialog dlg(ed, parent, CombinedTilesetDialog::Mode::AddImage,
                              logical->tilewidth, logical->tileheight);
    if (dlg.exec() != QDialog::Accepted) return false;
    return appendIncomingToLogicalTileset(ed, parent, representative, dlg.result(), selectedTilesetIndex);
}

bool TilesetManagerDialog::runImportAutotileFlow(Editor& ed, QWidget* parent, QString* createdAutotileId)
{
    QSet<QString> before;
    for (const TilesetAutotile& autotile : ed.autotiles) before.insert(autotile.id);
    const int normalTileset = ed.session.activeTilesetIdx;
    const TilesetSelection normalSelection = ed.session.tsSel;

    AutoTileConverterDialog dlg(ed, parent);
    dlg.exec();

    QString firstCreated;
    for (const TilesetAutotile& autotile : ed.autotiles) {
        if (!before.contains(autotile.id)) { firstCreated = autotile.id; break; }
    }
    ed.session.activeTilesetIdx = normalTileset;
    ed.session.tsSel = normalSelection;
    if (createdAutotileId) *createdAutotileId = firstCreated;
    return !firstCreated.isEmpty();
}

bool TilesetManagerDialog::runImportA1Flow(Editor& ed, QWidget* parent, QString* firstAutotileId)
{
    const QString path = AssetBrowserDialog::chooseImage(ed, parent, QStringLiteral("Autotiles"));
    if (path.isEmpty()) return false;
    QImage sheet(path);
    if (sheet.isNull()) {
        QMessageBox::warning(parent, QObject::tr("Importar A1"), QObject::tr("Não foi possível ler a imagem."));
        return false;
    }
    if (sheet.width() % 16 || sheet.height() % 12 || sheet.width() / 16 != sheet.height() / 12) {
        QMessageBox::information(parent, QObject::tr("Importar A1"),
            QObject::tr("Esta versão do importador A1 espera uma folha de 16×12 células iguais.\n\nA imagem escolhida tem %1×%2 px.")
                .arg(sheet.width()).arg(sheet.height()));
        return false;
    }
    const int tile = sheet.width() / 16;
    if (tile < 8 || tile > 256) {
        QMessageBox::information(parent, QObject::tr("Importar A1"),
            QObject::tr("Tamanho de tile A1 inválido: %1 px.").arg(tile));
        return false;
    }

    struct Piece { QImage image; QString name; bool animated = false; QVector<QImage> frames; };
    QVector<Piece> pieces;
    for (int band = 0; band < 4; ++band) {
        const int y = band * 3;
        for (int side = 0; side < 2; ++side) {
            const int x0 = side ? 8 : 0;
            QVector<QImage> frames;
            bool any = false;
            for (int f = 0; f < 3; ++f) {
                const int tx = x0 + f * 2;
                any |= hasVisiblePixels(sheet, QRect(tx * tile, y * tile, 2 * tile, 3 * tile));
                frames.push_back(convertA1Surface(sheet, tile, tx, y));
            }
            if (any) {
                Piece p; p.name = QObject::tr("A1 %1-%2").arg(band + 1).arg(side + 1);
                p.animated = true; p.frames = frames; pieces.push_back(p);
            }
            const int auxX = side ? 14 : 6;
            if (hasVisiblePixels(sheet, QRect(auxX * tile, y * tile, 2 * tile, 3 * tile))) {
                Piece p; p.name = QObject::tr("A1 auxiliar %1-%2").arg(band + 1).arg(side + 1);
                p.image = convertA1Surface(sheet, tile, auxX, y); pieces.push_back(p);
            }
        }
    }
    if (pieces.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Importar A1"),
            QObject::tr("Nenhum bloco de autotile utilizável foi encontrado."));
        return false;
    }

    struct PendingA1 { Tileset backing; QString name; QString animationId; };
    QVector<PendingA1> pending;
    pending.reserve(pieces.size());
    for (const Piece& piece : pieces) {
        QImage atlas;
        AnimatedAutotile anim;
        QString animationId;
        if (piece.animated) {
            atlas = QImage(12 * tile, 12 * tile, QImage::Format_ARGB32_Premultiplied);
            atlas.fill(Qt::transparent);
            QPainter painter(&atlas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            anim.name = piece.name; anim.baseX = 0; anim.baseY = 0; anim.cols = 12; anim.rows = 4;
            anim.fps = 6.0; anim.loop = true; anim.pingPong = false; anim.synchronized = true;
            for (int f = 0; f < piece.frames.size(); ++f) {
                const int fy = f * 4;
                painter.drawImage(0, fy * tile, piece.frames.at(f));
                anim.frameOrigins.push_back(QPoint(0, fy));
            }
            painter.end();
        } else atlas = piece.image;

        Tileset backing = makeTileset(atlas, piece.name, tile, tile, 0, 0, ed.projectRelativePath(path));
        backing.internalAutotileAtlas = true;
        if (piece.animated) {
            backing.animatedAutotiles.push_back(anim);
            animationId = backing.animatedAutotiles.first().id;
        }
        if (!enforcePortableTilesetSize(parent, &backing)) return false;
        pending.push_back(PendingA1{backing, piece.name, animationId});
    }

    io::loadWangPresetsFromSettings(ed);
    const int normalTileset = ed.session.activeTilesetIdx;
    const TilesetSelection normalSelection = ed.session.tsSel;
    const int addedStart = ed.tilesets.size();
    const int autotileStart = ed.autotiles.size();
    const int wangSetStart = ed.wangSets.size();
    const int wangPresetStart = ed.wangPresets.size();
    int wangConfigured = 0;
    auto rollbackA1 = [&] {
        while (ed.wangPresets.size() > wangPresetStart) ed.wangPresets.removeLast();
        while (ed.wangSets.size() > wangSetStart) ed.wangSets.removeLast();
        while (ed.autotiles.size() > autotileStart) ed.autotiles.removeLast();
        while (ed.tilesets.size() > addedStart) ed.removeTileset(ed.tilesets.size() - 1);
        ed.session.activeTilesetIdx = normalTileset;
        ed.session.tsSel = normalSelection;
    };

    QString firstId;
    for (PendingA1& item : pending) {
        item.backing.firstgid = ed.nextFirstGid();
        ed.tilesets.push_back(item.backing);
        const int ownerIdx = ed.tilesets.size() - 1;
        const QString resourceId = registerTilesetAutotile(
            ed, ownerIdx, item.name, QRect(0, 0, 12, 4), item.animationId,
            QString(), -1, true, QPoint(0, 3));
        if (resourceId.isEmpty()) {
            rollbackA1();
            QMessageBox::warning(parent, QObject::tr("Importar A1"),
                QObject::tr("A importação falhou e foi revertida por completo."));
            return false;
        }
        AutotileWangAutoConfigResult wangSetup;
        if (!autoConfigureTilesetAutotileWang(ed, ownerIdx, resourceId, &wangSetup)) {
            rollbackA1();
            QMessageBox::warning(parent, QObject::tr("Importar A1"),
                !wangSetup.failureMessage.isEmpty() ? wangSetup.failureMessage :
                QObject::tr("A folha 12×4 não pôde receber as regras de conexão automática. A importação foi desfeita por completo para evitar um Autotile incompleto."));
            return false;
        }
        if (!wangSetup.alreadyConfigured) ++wangConfigured;
        if (firstId.isEmpty()) firstId = resourceId;
    }

    ed.reindexTilesetGids();
    ed.session.activeTilesetIdx = normalTileset;
    ed.session.tsSel = normalSelection;
    ed.markDirty();
    emit ed.tilesetsChanged();
    emit ed.selectionChanged();
    if (firstAutotileId) *firstAutotileId = firstId;
    QMessageBox::information(parent, QObject::tr("Importar A1"),
        QObject::tr("A folha A1 foi convertida em %1 Autotile(s) independentes.\nAs conexões automáticas foram configuradas em %2 recurso(s).\nNenhum Tileset normal nem configuração de conexão existente foi alterado.")
            .arg(pieces.size()).arg(wangConfigured));
    return true;
}



} // namespace ui
