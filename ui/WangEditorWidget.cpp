#include "WangEditorWidget.h"
#include "core/TilesetCatalog.h"

#include "Icons.h"

#include "core/Renderer.h"
#include "core/ProjectIO.h"
#include "core/Wang.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

using namespace core;

namespace ui {

// ------------------------------------------------------------ WangTileCanvas
WangTileCanvas::WangTileCanvas(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    setMinimumSize(200, 200);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Clique nos cantos/bordas para rotular com a cor ativa.\n"
                  "Clique de novo na mesma posição para remover o rótulo.\n"
                  "Atalhos 1..8 seguem a ordem TL, T, TR, R, BR, B, BL, L."));
}

void WangTileCanvas::setTile(int tilesetIdx, int tx, int ty)
{
    m_tilesetIdx = tilesetIdx;
    m_tx = tx;
    m_ty = ty;
    update();
}

QString WangTileCanvas::posAt(const QPoint& p) const
{
    const double fx = double(p.x()) / qMax(1, width());
    const double fy = double(p.y()) / qMax(1, height());
    const int cx = fx < 0.34 ? 0 : (fx < 0.67 ? 1 : 2);
    const int cy = fy < 0.34 ? 0 : (fy < 0.67 ? 1 : 2);
    static const char* grid[3][3] = {
        { "tl", "t", "tr" }, { "l", "", "r" }, { "bl", "b", "br" }
    };
    return QString::fromLatin1(grid[cy][cx]);
}

void WangTileCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#161616"));
    const int side = qMin(width(), height());
    const QRect box((width() - side) / 2, (height() - side) / 2, side, side);

    const Tileset* ts = ed.tilesetAt(m_tilesetIdx);
    if (ts && ts->contains(m_tx, m_ty)) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawPixmap(box, pixmapCache().pixmap(ed, m_tilesetIdx), ts->tileRect(m_tx, m_ty));
    } else {
        p.setPen(QColor("#777"));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Selecione um tile na paleta para rotulá-lo."));
        return;
    }

    // grade 3x3
    p.setPen(QPen(QColor(255, 255, 255, 45), 1));
    for (int i = 1; i < 3; ++i) {
        p.drawLine(box.left() + side * i / 3, box.top(), box.left() + side * i / 3, box.bottom());
        p.drawLine(box.left(), box.top() + side * i / 3, box.right(), box.top() + side * i / 3);
    }

    const WangSet* ws = ed.activeWangSet();
    if (!ws) return;
    const WangTileData d = ws->tiles.value(WangSet::tileKeyOf(m_tilesetIdx, m_tx, m_ty));
    struct { const char* pos; int cx, cy; } spots[] = {
        { "tl", 0, 0 }, { "t", 1, 0 }, { "tr", 2, 0 },
        { "l",  0, 1 }, { "r", 2, 1 },
        { "bl", 0, 2 }, { "b", 1, 2 }, { "br", 2, 2 }
    };
    QFont f = p.font();
    f.setPixelSize(qMax(9, side / 18));
    p.setFont(f);
    for (const auto& s : spots) {
        const int cid = d.get(QString::fromLatin1(s.pos));
        const QRect cellRect(box.left() + s.cx * side / 3, box.top() + s.cy * side / 3,
                             side / 3, side / 3);
        if (cid >= 0) {
            const WangColor* wc = ws->colorById(cid);
            QColor col = wc ? wc->color : QColor("#4a90d7");
            col.setAlpha(140);
            p.fillRect(cellRect.adjusted(3, 3, -3, -3), col);
            p.setPen(Qt::white);
            p.drawText(cellRect, Qt::AlignCenter, wc ? wc->name : QString::number(cid));
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cellRect.adjusted(4, 4, -4, -4));
        }
    }

    // mascara resultante
    const int mask = wang::maskFromData(d, ed.session.activeWangColorId, ws->type);
    p.setPen(QColor("#8fd18f"));
    p.drawText(rect().adjusted(4, 4, -4, -4), Qt::AlignTop | Qt::AlignLeft,
               tr("combinação %1").arg(mask));
}

void WangTileCanvas::mousePressEvent(QMouseEvent* e)
{
    const QString pos = posAt(e->pos());
    if (!pos.isEmpty()) emit positionClicked(pos);
}

// ----------------------------------------------------------- WangEditorWidget
WangEditorWidget::WangEditorWidget(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_contextLabel = new QLabel(this);
    m_contextLabel->setObjectName(QStringLiteral("autotileTerrainContext"));
    m_contextLabel->setAccessibleName(tr("Conexões do Autotile selecionado"));
    m_contextLabel->setWordWrap(true);
    root->addWidget(m_contextLabel);
    m_contextActions = new QWidget(this);
    auto* contextRow = new QHBoxLayout(m_contextActions);
    contextRow->setContentsMargins(0, 0, 0, 0);
    m_createTerrain = new QPushButton(icons::get(QStringLiteral("add")), tr("Criar regras de conexão"), m_contextActions);
    m_bindTerrain = new QPushButton(tr("Usar estas conexões"), m_contextActions);
    m_bindTerrain->setToolTip(tr("Faz o Autotile usar as conexões que você configurou aqui."));
    m_clearTerrain = new QPushButton(icons::get(QStringLiteral("remove")), tr("Remover conexões"), m_contextActions);
    contextRow->addWidget(m_createTerrain);
    contextRow->addWidget(m_bindTerrain);
    contextRow->addWidget(m_clearTerrain);
    root->addWidget(m_contextActions);

    connect(m_createTerrain, &QPushButton::clicked, this, [this] {
        const TilesetAutotile* autotile = contextAutotile();
        if (!autotile) return;
        WangSet ws;
        ws.name = autotile->name.trimmed().isEmpty() ? tr("Terreno do Autotile")
                                                     : tr("Terreno · %1").arg(autotile->name);
        WangColor color;
        color.id = 1;
        color.name = autotile->name.trimmed().isEmpty() ? tr("Terreno") : autotile->name;
        color.color = QColor("#6ab04c");
        ws.colors.push_back(color);
        const QString setId = ws.id;
        ed.wangSets.push_back(ws);
        if (!setTilesetAutotileTerrain(ed, m_contextTilesetIdx, m_contextAutotileId, setId, color.id)) return;
        emit ed.wangChanged();
        emit ed.tilesetsChanged();
        refresh();
        emit statusMessage(tr("Regras de conexão criadas e aplicadas ao Autotile selecionado."));
    });
    connect(m_bindTerrain, &QPushButton::clicked, this, [this] {
        const WangSet* ws = ed.activeWangSet();
        if (!contextAutotile() || !ws || !ws->colorById(ed.session.activeWangColorId)) {
            emit statusMessage(tr("Escolha primeiro as regras e o tipo de terreno que este Autotile deve usar."));
            return;
        }
        if (!setTilesetAutotileTerrain(ed, m_contextTilesetIdx, m_contextAutotileId,
                                       ws->id, ed.session.activeWangColorId)) return;
        emit ed.wangChanged();
        emit ed.tilesetsChanged();
        refresh();
        emit statusMessage(tr("Regras de conexão ligadas ao Autotile selecionado."));
    });
    connect(m_clearTerrain, &QPushButton::clicked, this, [this] {
        const TilesetAutotile* autotile = contextAutotile();
        if (!autotile || !autotile->hasTerrain()) return;
        if (QMessageBox::question(this, tr("Remover conexões"),
                tr("Remover as regras de conexão deste Autotile?\n\nAs regras salvas somente para este Autotile serão apagadas. A imagem do Tileset não será alterada."))
            != QMessageBox::Yes) return;
        if (!clearTilesetAutotileTerrain(ed, m_contextTilesetIdx, m_contextAutotileId)) return;
        if (m_editMode) m_editMode->setChecked(false);
        emit ed.wangChanged();
        emit ed.tilesetsChanged();
        refresh();
        emit statusMessage(tr("Regras de conexão removidas do Autotile."));
    });

    // ---- Conjunto de conexõess -------------------------------------------------------
    auto* setRow = new QHBoxLayout;
    m_setCombo = new QComboBox(this);
    m_setCombo->setToolTip(tr("Agrupa tipos de terreno que compartilham as mesmas regras de conexão. Por exemplo, diferentes variações de grama dentro do mesmo sistema."));
    setRow->addWidget(m_setCombo, 1);
    auto* addSet = new QPushButton(icons::get(QStringLiteral("add")), QString(), this);
    addSet->setToolTip(tr("Criar grupo de conexões"));
    addSet->setFixedWidth(30);
    auto* delSet = new QPushButton(icons::get(QStringLiteral("remove")), QString(), this);
    delSet->setToolTip(tr("Excluir grupo de conexões"));
    delSet->setFixedWidth(30);
    setRow->addWidget(addSet);
    setRow->addWidget(delSet);
    root->addLayout(setRow);

    auto* typeRow = new QHBoxLayout;
    typeRow->addWidget(new QLabel(tr("Tipo de conexão:"), this));
    m_setType = new QComboBox(this);
    m_setType->addItem(tr("Completo — lados e cantos (8 vizinhos)"), QStringLiteral("mixed"));
    m_setType->addItem(tr("Somente lados (cima, direita, baixo e esquerda)"), QStringLiteral("edge"));
    m_setType->addItem(tr("Somente cantos (4 diagonais)"), QStringLiteral("corner"));
    m_setType->setToolTip(tr("Escolha se os tiles devem se conectar pelos lados, pelos cantos ou pelos dois. Os modelos prontos e a verificação usam esta escolha."));
    typeRow->addWidget(m_setType, 1);
    root->addLayout(typeRow);

    connect(addSet, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Novo grupo de conexões"), tr("Nome:"),
                                                   QLineEdit::Normal,
                                                   tr("Terreno %1").arg(ed.wangSets.size() + 1), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        WangSet ws;
        ws.name = name.trimmed();
        WangColor c;
        c.id = 1;
        c.name = tr("Terreno 1");
        c.color = QColor("#6ab04c");
        ws.colors.push_back(c);
        ed.wangSets.push_back(ws);
        ed.setActiveWangSet(ed.wangSets.size() - 1);
        ed.session.activeWangColorId = 1;
        ed.markDirty();
        refresh();
        emit statusMessage(tr("Conjunto “%1” criado.").arg(ws.name));
    });
    connect(delSet, &QPushButton::clicked, this, [this] {
        WangSet* ws = set();
        if (!ws) return;
        const QString removedId = ws->id;
        if (QMessageBox::question(this, tr("Excluir grupo de conexões"),
                                  tr("Excluir “%1”?\n\nOs tiles que já estão no mapa continuam visíveis, mas deixam de ter estas regras de conexão.")
                                      .arg(ws->name)) != QMessageBox::Yes) return;

        bool mapRefsChanged = false;
        for (MapDoc& doc : ed.docs) {
            const QVector<LayerPtr> layers = flattenRenderableLayers(doc.layers);
            for (const LayerPtr& layer : layers) {
                if (!layer || layer->type != LayerType::Tile) continue;
                for (auto& row : layer->data2D) {
                    for (Cell& cell : row) {
                        for (TileRef& tile : cell) {
                            if (tile.wangSetId != removedId) continue;
                            tile.wangSetId.clear();
                            tile.wangColorId = -1;
                            mapRefsChanged = true;
                        }
                    }
                }
            }
        }
        for (TilesetAutotile& autotile : ed.autotiles) {
            if (autotile.wangSetId != removedId) continue;
            autotile.wangSetId.clear();
            autotile.wangColorId = -1;
        }
        ed.wangSets.remove(ed.session.activeWangSetIdx);
        ed.setActiveWangSet(ed.wangSets.isEmpty() ? -1 : 0);
        reconcileTilesetAutotiles(ed);
        ed.markDirty();
        emit ed.wangChanged();
        if (mapRefsChanged) emit ed.mapChanged();
        refresh();
    });
    connect(m_setCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (m_updating) return;
        ed.setActiveWangSet(i);
        refresh();
    });
    connect(m_setType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (m_updating) return;
        WangSet* ws = set();
        if (!ws || i < 0) return;
        const QString type = m_setType->itemData(i).toString();
        if (type.isEmpty() || ws->type == type) return;
        ws->type = type;
        ed.markDirty();
        emit ed.wangChanged();
        refresh();
        emit statusMessage(tr("Tipo de conexão alterado para %1.").arg(m_setType->currentText()));
    });

    // ---- cores / terrenos -------------------------------------------------
    auto* colorBox = new QGroupBox(tr("Tipos de terreno"), this);
    auto* cbLayout = new QVBoxLayout(colorBox);
    cbLayout->setContentsMargins(6, 6, 6, 6);
    m_colorList = new QListWidget(colorBox);
    m_colorList->setMaximumHeight(120);
    cbLayout->addWidget(m_colorList);
    auto* colorBtns = new QHBoxLayout;
    auto* addColor = new QPushButton(icons::get(QStringLiteral("add")), tr("Novo terreno"), colorBox);
    auto* editColor = new QPushButton(icons::get(QStringLiteral("color-palette")), tr("Cor de identificação"), colorBox);
    auto* delColor = new QPushButton(icons::get(QStringLiteral("remove")), QString(), colorBox);
    colorBtns->addWidget(addColor);
    colorBtns->addWidget(editColor);
    colorBtns->addWidget(delColor);
    cbLayout->addLayout(colorBtns);
    root->addWidget(colorBox);

    connect(addColor, &QPushButton::clicked, this, [this] {
        WangSet* ws = set();
        if (!ws) { emit statusMessage(tr("Crie um grupo de conexões primeiro.")); return; }
        int nextId = 1;
        for (const WangColor& c : ws->colors) nextId = qMax(nextId, c.id + 1);
        WangColor c;
        c.id = nextId;
        c.name = tr("Terreno %1").arg(nextId);
        static const char* palette[] = { "#6ab04c", "#4a90d7", "#c58af9", "#e58e26", "#eb4d4b", "#22a6b3" };
        c.color = QColor(palette[(nextId - 1) % 6]);
        ws->colors.push_back(c);
        ed.session.activeWangColorId = c.id;
        ed.markDirty();
        refresh();
    });
    connect(editColor, &QPushButton::clicked, this, [this] {
        WangSet* ws = set();
        if (!ws) return;
        WangColor* c = ws->colorById(ed.session.activeWangColorId);
        if (!c) return;
        const QColor col = QColorDialog::getColor(c->color, this, tr("Cor do terreno"));
        if (col.isValid()) { c->color = col; ed.markDirty(); refresh(); }
    });
    connect(delColor, &QPushButton::clicked, this, [this] {
        WangSet* ws = set();
        if (!ws || ws->colors.isEmpty()) return;
        const WangColor* c = ws->colorById(ed.session.activeWangColorId);
        if (!c) return;
        const bool last = ws->colors.size() == 1;
        if (QMessageBox::question(
                this, tr("Excluir terreno"),
                last ? tr("Excluir o terreno “%1”?\n\nEle é o último deste grupo de conexões. O grupo ficará vazio e as marcações usadas para formar este terreno serão apagadas.").arg(c->name)
                     : tr("Excluir o terreno “%1”?\n\nAs marcações usadas para formar este terreno serão removidas dos tiles deste grupo.").arg(c->name)) != QMessageBox::Yes) return;

        const int touched = wang::removeColor(*ws, ed.session.activeWangColorId);
        ed.session.activeWangColorId = ws->colors.isEmpty() ? -1 : ws->colors.first().id;
        reconcileTilesetAutotiles(ed);
        ed.markDirty();
        emit ed.wangChanged();
        refresh();
        emit statusMessage(tr("Terreno excluído. As regras foram removidas de %1 tile(s).").arg(touched));
    });
    connect(m_colorList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating) return;
        WangSet* ws = set();
        if (!ws || row < 0 || row >= ws->colors.size()) return;
        ed.session.activeWangColorId = ws->colors[row].id;
        emit ed.wangChanged();
    });

    // ---- modos ------------------------------------------------------------
    m_editMode = new QCheckBox(tr("Ensinar como os tiles se conectam"), this);
    m_editMode->setToolTip(tr("Ative para indicar quais lados e cantos de cada tile pertencem ao terreno escolhido. Isso ensina ao editor qual peça usar em cada situação."));
    m_eraseMode = new QCheckBox(tr("Apagar conexões ao clicar"), this);
    m_manualMode = new QCheckBox(tr("Usar peça exata (sem ajuste automático)"), this);
    root->addWidget(m_editMode);
    root->addWidget(m_eraseMode);
    root->addWidget(m_manualMode);
    connect(m_editMode, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            WangSet* ws = set();
            if (!ws || !ws->colorById(ed.session.activeWangColorId)) {
                m_editMode->setChecked(false);
                emit statusMessage(tr("Escolha ou crie um grupo de conexões e um tipo de terreno antes de editar."));
                return;
            }
        }
        emit editModeChanged(on);
        emit statusMessage(on
            ? tr("Edição de conexões ligada. Clique nas bordas/cantos dos tiles para ensinar ao editor como este terreno se conecta.")
            : tr("Edição de conexões desligada."));
    });
    connect(m_eraseMode, &QCheckBox::toggled, this, [this](bool on) { ed.session.wangEraseMode = on; });
    connect(m_manualMode, &QCheckBox::toggled, this, [this](bool on) { ed.session.terrainManual = on; });

    // ---- editor grande ----------------------------------------------------
    m_selectedTileLabel = new QLabel(tr("Nenhum tile selecionado"), this);
    m_selectedTileLabel->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    root->addWidget(m_selectedTileLabel);
    m_canvas = new WangTileCanvas(ed, this);
    root->addWidget(m_canvas, 1);
    connect(m_canvas, &WangTileCanvas::positionClicked, this, [this](const QString& pos) {
        if (!ed.session.tsSel.valid()) return;
        applyLabel(ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y, pos);
    });

    // ---- presets ----------------------------------------------------------
    auto* presetRow = new QHBoxLayout;
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setToolTip(tr("Escolha um modelo pronto para preencher rapidamente as regras dos tiles selecionados."));
    presetRow->addWidget(m_presetCombo, 1);
    auto* applyPreset = new QPushButton(tr("Aplicar"), this);
    applyPreset->setToolTip(tr("Aplica o modelo escolhido começando pelo canto superior esquerdo da seleção atual."));
    presetRow->addWidget(applyPreset);
    root->addLayout(presetRow);

    auto* presetBtns = new QHBoxLayout;
    auto* savePresetBtn = new QPushButton(icons::get(QStringLiteral("save")),
                                          tr("Salvar seleção como modelo"), this);
    savePresetBtn->setToolTip(tr("Guarda as regras da seleção como um modelo reutilizável. Depois você pode aplicar o mesmo padrão em outros Autotiles ou exportá-lo."));
    auto* delPresetBtn = new QPushButton(icons::get(QStringLiteral("remove")), QString(), this);
    delPresetBtn->setToolTip(tr("Excluir o modelo selecionado. Os modelos que vêm com o editor não podem ser apagados."));
    delPresetBtn->setFixedWidth(38);
    presetBtns->addWidget(savePresetBtn, 1);
    presetBtns->addWidget(delPresetBtn);
    root->addLayout(presetBtns);
    connect(savePresetBtn, &QPushButton::clicked, this, &WangEditorWidget::savePresetFromSelection);
    connect(delPresetBtn, &QPushButton::clicked, this, &WangEditorWidget::deleteSelectedPreset);
    connect(applyPreset, &QPushButton::clicked, this, [this] {
        WangSet* ws = set();
        if (!ws) { emit statusMessage(tr("Crie um grupo de conexões primeiro.")); return; }
        if (!ed.session.tsSel.valid()) { emit statusMessage(tr("⚠ Selecione na paleta o canto superior-esquerdo do bloco.")); return; }
        const int idx = m_presetCombo->currentIndex();
        if (idx < 0 || idx >= ed.wangPresets.size()) return;
        const WangPreset p = ed.wangPresets.at(idx);
        const bool validPresetType = p.type == QLatin1String("mixed") ||
                                     p.type == QLatin1String("edge") ||
                                     p.type == QLatin1String("corner");
        if (validPresetType && ws->type != p.type) {
            if (QMessageBox::question(this, tr("Tipo de conexão diferente"),
                                      tr("O modelo “%1” foi criado para um tipo de conexão diferente do grupo atual.\n\n"
                                         "Quer ajustar o grupo para combinar com o modelo? As conexões já marcadas podem passar a funcionar de outra forma.")
                                          .arg(p.name)) != QMessageBox::Yes)
                return;
            ws->type = p.type;
        }
        QRect allowedRegion;
        QSet<QString> allowedTileKeys;
        if (const TilesetAutotile* autotile = contextAutotile()) {
            allowedRegion = tilesetAutotileBounds(ed, m_contextTilesetIdx, *autotile);
            for (const QPoint& tile : tilesetAutotileTiles(ed, m_contextTilesetIdx, *autotile))
                allowedTileKeys.insert(WangSet::tileKeyOf(m_contextTilesetIdx, tile.x(), tile.y()));
        }
        const int touched = wang::applyPreset(*ws, p, ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y,
                                              ed.session.activeWangColorId, allowedRegion, allowedTileKeys);
        if (!hasAutotileContext()) reconcileTilesetAutotiles(ed);
        ed.markDirty();
        emit ed.wangChanged();
        refresh();
        emit statusMessage(tr("Modelo “%1” aplicado a partir de (%2,%3) — %4 tiles configurados. %5")
                               .arg(p.name).arg(ed.session.tsSel.x).arg(ed.session.tsSel.y).arg(touched)
                               .arg(hasAutotileContext()
                                    ? tr("Quando terminar, use ‘Usar estas conexões’ para aplicar o resultado ao Autotile.")
                                    : QString()));
    });

    // ---- diagnostico ------------------------------------------------------
    auto* diagBox = new QGroupBox(tr("Verificação das conexões"), this);
    auto* dl = new QVBoxLayout(diagBox);
    dl->setContentsMargins(6, 6, 6, 6);
    m_coverage = new QProgressBar(diagBox);
    m_coverage->setRange(0, 100);
    m_coverage->setFormat(tr("Conexões configuradas: %p%"));
    dl->addWidget(m_coverage);
    m_diagLabel = new QLabel(diagBox);
    m_diagLabel->setWordWrap(true);
    m_diagLabel->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    dl->addWidget(m_diagLabel);
    root->addWidget(diagBox);

    connect(&ed, &Editor::wangChanged, this, &WangEditorWidget::refresh);
    connect(&ed, &Editor::selectionChanged, this, [this] {
        if (ed.session.tsSel.valid()) {
            m_canvas->setTile(ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y);
            m_selectedTileLabel->setText(tr("Peça selecionada: coluna %1, linha %2 do Tileset %3")
                                             .arg(ed.session.tsSel.x).arg(ed.session.tsSel.y).arg(ed.session.tsSel.tilesetIdx + 1));
        }
    });

    bool hasBuiltinBlob47 = false;
    bool hasBuiltinTerrenos12x4 = false;
    bool hasBuiltinAutotile4x4 = false;
    for (const WangPreset& preset : ed.wangPresets) {
        if (preset.id == QLatin1String("builtin-blob47")) hasBuiltinBlob47 = true;
        if (preset.id == QLatin1String("builtin-terrenos-12x4")) hasBuiltinTerrenos12x4 = true;
        if (preset.id == QLatin1String("builtin-autotile-4x4")) hasBuiltinAutotile4x4 = true;
    }
    if (!hasBuiltinBlob47) ed.wangPresets.push_back(wang::builtinBlob47());
    if (!hasBuiltinTerrenos12x4) ed.wangPresets.push_back(wang::builtinTerrenos12x4());
    if (!hasBuiltinAutotile4x4) ed.wangPresets.push_back(wang::builtinAutotile4x4());
    // Presets personalizados continuam disponíveis para autoria manual e para
    // geometrias diferentes; grades canônicas automáticas usam os embutidos.
    io::loadWangPresetsFromSettings(ed);
    refresh();
}

const TilesetAutotile* WangEditorWidget::contextAutotile() const
{
    if (m_contextTilesetIdx < 0 || m_contextAutotileId.isEmpty()) return nullptr;
    return tilesetAutotileById(ed, m_contextTilesetIdx, m_contextAutotileId);
}

bool WangEditorWidget::hasAutotileContext() const
{
    return contextAutotile() != nullptr;
}

void WangEditorWidget::setAutotileContext(int tilesetIdx, const QString& autotileId)
{
    const bool contextChanged = m_contextTilesetIdx != tilesetIdx || m_contextAutotileId != autotileId;
    m_contextTilesetIdx = tilesetIdx;
    m_contextAutotileId = autotileId;
    if (const TilesetAutotile* autotile = contextAutotile()) {
        // Só reposiciona a seleção de conexão quando o usuário realmente troca de
        // Autotile. Refreshes causados pela edição não podem desfazer uma
        // seleção diferente que o usuário está prestes a vincular.
        if (contextChanged && autotile->hasTerrain()) {
            for (int i = 0; i < ed.wangSets.size(); ++i) {
                if (ed.wangSets.at(i).id == autotile->wangSetId) {
                    ed.setActiveWangSet(i);
                    ed.session.activeWangColorId = autotile->wangColorId;
                    break;
                }
            }
        }
        if (contextChanged) {
            const QRect bounds = tilesetAutotileBounds(ed, tilesetIdx, *autotile);
            if (!bounds.isEmpty()) {
                ed.session.tsSel = TilesetSelection{tilesetIdx, bounds.left(), bounds.top(),
                                            bounds.width(), bounds.height()};
                emit ed.selectionChanged();
            }
        }
    }
    refresh();
}

void WangEditorWidget::clearAutotileContext()
{
    m_contextTilesetIdx = -1;
    m_contextAutotileId.clear();
    if (m_editMode) m_editMode->setChecked(false);
    refresh();
}

bool WangEditorWidget::editMode() const { return m_editMode && m_editMode->isChecked(); }

void WangEditorWidget::applyLabel(int tilesetIdx, int tx, int ty, const QString& position)
{
    if (position.isEmpty()) return;
    WangSet* ws = set();
    if (!ws || !ws->colorById(ed.session.activeWangColorId)) return;
    if (!wang::allowedPositions(ws->type).contains(position)) {
        emit statusMessage(tr("A posição %1 não pode ser usada com o tipo de conexão “%2”.")
                               .arg(position, ws->type));
        return;
    }

    // O contexto do Autotile restringe apenas ONDE a geometria pode ser
    // desenhada. O Conjunto de conexões/cor em edicao pode ser novo e ainda nao estar
    // vinculado; isso permite criar e testar presets antes do vinculo final.
    if (hasAutotileContext()) {
        const TilesetAutotile* autotile = contextAutotile();
        if (!autotile || tilesetIdx != m_contextTilesetIdx) return;
        const QVector<QPoint> allowed = tilesetAutotileTiles(ed, tilesetIdx, *autotile);
        if (!allowed.contains(QPoint(tx, ty))) {
            emit statusMessage(tr("Este tile não pertence ao Autotile selecionado."));
            return;
        }
    }
    const QString key = WangSet::tileKeyOf(tilesetIdx, tx, ty);
    WangTileData d = ws->tiles.value(key);
    // Clicar de novo na mesma posicao com a mesma cor remove o rotulo.
    if (d.get(position) == ed.session.activeWangColorId) d.set(position, -1);
    else {
        d.set(position, ed.session.activeWangColorId);
        d.isolatedColorId = -1;   // ao ganhar uma borda/quina, deixa de ser isolado
    }
    if (d.isEmpty()) ws->tiles.remove(key);
    else ws->tiles.insert(key, d);
    // Em contexto de Autotile, editar uma geometria ainda nao vinculada nao
    // deve sintetizar outro recurso semantico via reconciliacao legado.
    if (!hasAutotileContext()) reconcileTilesetAutotiles(ed);
    ed.markDirty();
    emit ed.wangChanged();
    m_canvas->setTile(tilesetIdx, tx, ty);
    updateDiagnostics();
}

void WangEditorWidget::rebuildSetCombo()
{
    m_setCombo->clear();
    m_setCombo->setIconSize(QSize(20, 20));
    for (const WangSet& ws : ed.wangSets) {
        const QString label = QStringLiteral("%1 (%2 tiles)").arg(ws.name).arg(ws.tiles.size());
        if (ws.hasIcon) m_setCombo->addItem(QIcon(tileIcon(ws.iconTilesetIdx, ws.iconTx, ws.iconTy, 20)), label);
        else m_setCombo->addItem(label);
    }
    if (ed.session.activeWangSetIdx >= 0) m_setCombo->setCurrentIndex(ed.session.activeWangSetIdx);
}

/// Miniatura de um tile do tileset, usada como icone de terreno/Conjunto de conexões.
QPixmap WangEditorWidget::tileIcon(int tilesetIdx, int tx, int ty, int size) const
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    if (!ts || !ts->contains(tx, ty)) return pm;
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawPixmap(QRect(0, 0, size, size), pixmapCache().pixmap(ed, tilesetIdx), ts->tileRect(tx, ty));
    return pm;
}

void WangEditorWidget::rebuildColorList()
{
    m_colorList->clear();
    const WangSet* ws = ed.activeWangSet();
    if (!ws) return;
    m_colorList->setIconSize(QSize(24, 24));
    int row = 0, sel = -1;
    for (const WangColor& c : ws->colors) {
        // Se o terreno tem um tile definido como icone, mostramos o TILE;
        // caso contrario, o quadradinho de cor.
        QPixmap pm;
        if (c.hasIcon) {
            pm = tileIcon(c.iconTilesetIdx, c.iconTx, c.iconTy, 24);
            QPainter p(&pm);                       // faixa com a cor do terreno
            p.fillRect(0, 20, 24, 4, c.color);
        } else {
            pm = QPixmap(24, 24);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setPen(QPen(QColor("#111"), 1));
            p.setBrush(c.color);
            p.drawRoundedRect(QRect(2, 2, 20, 20), 3, 3);
        }
        // Conta quantos tiles usam esta cor e se ela ja tem tile isolado.
        int labelled = 0;
        bool hasIsolated = false;
        for (auto it = ws->tiles.constBegin(); it != ws->tiles.constEnd(); ++it) {
            if (it.value().isIsolatedOf(c.id)) { hasIsolated = true; ++labelled; continue; }
            if (wang::dataHasColor(it.value(), c.id)) ++labelled;
        }
        QString text = tr("%1  ·  %2 tile(s)").arg(c.name).arg(labelled);
        if (hasIsolated) text += tr("  ·  peça sozinha ✓");
        auto* item = new QListWidgetItem(QIcon(pm), text);
        item->setToolTip((c.hasIcon ? tr("Peça de referência: tile (%1, %2)\n").arg(c.iconTx).arg(c.iconTy) : QString())
                             + (hasIsolated ? tr("A peça usada quando este terreno aparece sozinho já está definida.")
                                            : tr("Ainda falta escolher a peça usada quando este terreno aparece sozinho. Clique com o botão direito em um tile da paleta para defini-la.")));
        m_colorList->addItem(item);
        if (c.id == ed.session.activeWangColorId) sel = row;
        ++row;
    }
    if (sel >= 0) m_colorList->setCurrentRow(sel);
}

void WangEditorWidget::updateDiagnostics()
{
    const WangSet* ws = ed.activeWangSet();
    if (!ws) {
        m_coverage->setValue(0);
        m_diagLabel->setText(tr("Nenhum conjunto de conexões selecionado."));
        return;
    }
    QSet<QString> diagnosticScope;
    if (const TilesetAutotile* autotile = contextAutotile()) {
        for (const QPoint& tile : tilesetAutotileTiles(ed, m_contextTilesetIdx, *autotile))
            diagnosticScope.insert(WangSet::tileKeyOf(m_contextTilesetIdx, tile.x(), tile.y()));
    }
    const double cov = wang::coverage(*ws, ed.session.activeWangColorId, diagnosticScope);
    m_coverage->setValue(int(cov * 100));
    const QVector<int> missing = wang::missingMasks(*ws, ed.session.activeWangColorId, diagnosticScope);
    const QHash<int, QStringList> dups = wang::duplicateMasks(*ws, ed.session.activeWangColorId, diagnosticScope);
    QStringList lines;
    lines << tr("%1 peça(s) já possuem regras neste grupo.").arg(ws->tiles.size());
    const int expectedCount = wang::expectedMasks(ws->type).size();
    if (missing.isEmpty()) lines << tr("✅ Todas as %1 combinações esperadas estão configuradas.").arg(expectedCount);
    else {
        QStringList first;
        for (int i = 0; i < qMin(12, missing.size()); ++i) first << QString::number(missing[i]);
        lines << tr("⚠ Ainda faltam %1 situações de vizinhança: %2%3")
                     .arg(missing.size(), 0)
                     .arg(first.join(QStringLiteral(", ")),
                          missing.size() > 12 ? QStringLiteral("…") : QString());
    }
    if (!dups.isEmpty()) lines << tr("ℹ %1 situações possuem mais de uma peça possível; o editor pode alternar entre elas automaticamente.").arg(dups.size());
    m_diagLabel->setText(lines.join(QStringLiteral("\n")));
}

void WangEditorWidget::savePresetFromSelection()
{
    WangSet* ws = set();
    if (!ws) {
        QMessageBox::information(this, tr("Salvar modelo"), tr("Nenhum conjunto de conexões selecionado."));
        return;
    }
    const WangColor* col = ws->colorById(ed.session.activeWangColorId);
    if (!col) {
        QMessageBox::information(this, tr("Salvar modelo"),
                                 tr("Selecione primeiro um terreno. As regras desse terreno serão salvas no modelo."));
        return;
    }
    const TilesetSelection sel = ed.session.tsSel;
    if (!sel.valid()) {
        QMessageBox::information(this, tr("Salvar modelo"),
                                 tr("Selecione na paleta o mesmo bloco de tiles em que você marcou as conexões antes de salvar o modelo."));
        return;
    }

    // Varre a seleção e guarda, por tile, as posições marcadas com a cor ativa.
    // Em contexto de Autotile, nem salvar preset pode capturar dados de um
    // recurso vizinho que compartilhe Conjunto de conexões/cor no mesmo backing.
    QSet<QString> presetScope;
    if (const TilesetAutotile* autotile = contextAutotile()) {
        for (const QPoint& tile : tilesetAutotileTiles(ed, m_contextTilesetIdx, *autotile))
            presetScope.insert(WangSet::tileKeyOf(m_contextTilesetIdx, tile.x(), tile.y()));
    }
    QHash<QString, QSet<QString>> positions;
    for (int ty = 0; ty < sel.h; ++ty)
        for (int tx = 0; tx < sel.w; ++tx) {
            const QString key = WangSet::tileKeyOf(sel.tilesetIdx, sel.x + tx, sel.y + ty);
            if (!presetScope.isEmpty() && !presetScope.contains(key)) continue;
            auto it = ws->tiles.constFind(key);
            if (it == ws->tiles.constEnd()) continue;
            QSet<QString> flags;
            for (const QString& pos : wang::allowedPositions(ws->type))
                if (it.value().get(pos) == col->id) flags.insert(pos);
            if (it.value().isIsolatedOf(col->id)) flags.insert(QStringLiteral("isolated"));
            if (!flags.isEmpty())
                positions.insert(QStringLiteral("%1,%2").arg(tx).arg(ty), flags);
        }

    if (positions.isEmpty()) {
        QMessageBox::information(this, tr("Salvar modelo"),
                                 tr("Nenhum rótulo da cor “%1” foi encontrado dentro dessa seleção.\n\n"
                                    "Ligue “Modo de edição Wang”, desenhe a geometria dentro do Autotile "
                                    "e tente de novo. O Terrain não precisa estar vinculado para salvar o preset.").arg(col->name));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Salvar modelo"), tr("Nome do modelo:"),
                                               QLineEdit::Normal,
                                               ws->name.isEmpty() ? tr("Meu modelo de conexão") : ws->name,
                                               &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    WangPreset p;
    p.name = name.trimmed();
    p.type = ws->type;
    p.w = sel.w;
    p.h = sel.h;
    p.positions = positions;
    p.builtin = false;
    ed.wangPresets.push_back(p);
    io::saveWangPresetsToSettings(ed);
    ed.markDirty();
    refresh();
    m_presetCombo->setCurrentIndex(ed.wangPresets.size() - 1);

    QMessageBox::information(this, tr("Modelo salvo"),
                             tr("Modelo “%1” salvo (%2×%3, %4 tile(s) configurado(s)).\n\n"
                                "Ele fica guardado no programa e agora pode ser exportado em "
                                "Arquivo ▸ Exportar ▸ Modelos de conexão.")
                                 .arg(p.name).arg(p.w).arg(p.h).arg(positions.size()));
    emit statusMessage(tr("Modelo “%1” salvo.").arg(p.name));
}

void WangEditorWidget::deleteSelectedPreset()
{
    const int idx = m_presetCombo->currentIndex();
    if (idx < 0 || idx >= ed.wangPresets.size()) return;
    const WangPreset& p = ed.wangPresets[idx];
    if (p.builtin) {
        QMessageBox::information(this, tr("Excluir modelo"),
                                 tr("“%1” é um modelo que vem com o editor e não pode ser excluído.").arg(p.name));
        return;
    }
    if (QMessageBox::question(this, tr("Excluir modelo"), tr("Excluir o modelo “%1”?").arg(p.name))
        != QMessageBox::Yes) return;
    ed.wangPresets.remove(idx);
    io::saveWangPresetsToSettings(ed);
    refresh();
    emit statusMessage(tr("Modelo excluído."));
}

void WangEditorWidget::refresh()
{
    if (m_updating) return;
    m_updating = true;
    rebuildSetCombo();
    if (m_setType) {
        const QString type = ed.activeWangSet() ? ed.activeWangSet()->type : QStringLiteral("mixed");
        const int idx = m_setType->findData(type);
        m_setType->setCurrentIndex(idx >= 0 ? idx : 0);
        m_setType->setEnabled(ed.activeWangSet() != nullptr);
    }
    rebuildColorList();
    const TilesetAutotile* autotile = contextAutotile();
    const bool contextual = autotile != nullptr;
    if (m_contextActions) m_contextActions->setVisible(contextual);
    if (m_contextLabel) {
        m_contextLabel->setVisible(contextual);
        if (contextual) {
            QString binding = tr("sem terreno conectado");
            if (autotile->hasTerrain()) {
                const WangSet* ws = wangSetById(ed, autotile->wangSetId);
                const WangColor* color = ws ? ws->colorById(autotile->wangColorId) : nullptr;
                binding = ws ? ws->name : autotile->wangSetId;
                if (color) binding += tr(" / %1").arg(color->name);
            }
            QString editing = tr("nenhuma seleção de conexão");
            if (const WangSet* current = ed.activeWangSet()) {
                const WangColor* currentColor = current->colorById(ed.session.activeWangColorId);
                editing = current->name;
                if (currentColor) editing += tr(" / %1").arg(currentColor->name);
            }
            m_contextLabel->setText(tr("<b>Autotile:</b> %1<br><b>Terreno conectado:</b> %2<br><b>Editando agora:</b> %3")
                                        .arg(autotile->name.isEmpty() ? tr("Autotile") : autotile->name,
                                             binding, editing));
        }
    }
    if (m_createTerrain) m_createTerrain->setEnabled(contextual && !autotile->hasTerrain());
    if (m_clearTerrain) m_clearTerrain->setEnabled(contextual && autotile->hasTerrain());
    if (m_bindTerrain) m_bindTerrain->setEnabled(contextual && ed.activeWangSet() &&
        ed.activeWangSet()->colorById(ed.session.activeWangColorId));
    if (m_editMode) {
        const WangSet* current = ed.activeWangSet();
        const bool canEdit = current && current->colorById(ed.session.activeWangColorId) &&
                             (!contextual || !tilesetAutotileTiles(ed, m_contextTilesetIdx, *autotile).isEmpty());
        m_editMode->setEnabled(canEdit);
        if (!canEdit && m_editMode->isChecked()) m_editMode->setChecked(false);
        m_editMode->setToolTip(canEdit
            ? tr("Marque os lados e cantos do terreno selecionado dentro deste Autotile. Você pode ligar o terreno ao Autotile depois.")
            : tr("Crie ou selecione um conjunto e um terreno para começar a definir as conexões."));
    }
    m_presetCombo->clear();
    for (const WangPreset& p : ed.wangPresets)
        m_presetCombo->addItem(QStringLiteral("%1 (%2×%3)").arg(p.name).arg(p.w).arg(p.h));
    if (m_eraseMode->isChecked() != ed.session.wangEraseMode) m_eraseMode->setChecked(ed.session.wangEraseMode);
    if (m_manualMode->isChecked() != ed.session.terrainManual) m_manualMode->setChecked(ed.session.terrainManual);
    if (ed.session.tsSel.valid()) m_canvas->setTile(ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y);
    updateDiagnostics();
    m_updating = false;
}

} // namespace ui
