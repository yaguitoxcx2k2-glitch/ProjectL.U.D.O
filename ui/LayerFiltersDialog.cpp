#include "LayerFiltersDialog.h"

#include "Icons.h"
#include "core/LayerRasterFilters.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

using namespace core;

namespace ui {
namespace {

RasterLayerFilter makeDefaultFilter(const QString& type)
{
    RasterLayerFilter f;
    f.type = type;
    if (type == QLatin1String("gaussianBlur")) {
        f.radius = 4.0; f.strength = 0.75; f.quality = 1;
    } else if (type == QLatin1String("directionalBlur")) {
        f.radius = 14.0; f.angle = 0.0; f.strength = 0.72; f.quality = 1;
    } else if (type == QLatin1String("noise")) {
        f.amount = 0.12; f.scale = 2.0; f.strength = 1.0; f.seed = 1337; f.monochrome = true;
    } else if (type == QLatin1String("contactShadow")) {
        f.radius = 5.0; f.distance = 3.0; f.angle = 90.0; f.spread = 1.0;
        f.opacity = 0.36; f.strength = 1.0; f.quality = 1; f.color = QColor(0,0,0,255);
    }
    return f;
}

int qualityIndex(int quality) { return qBound(0, quality, 2); }

} // namespace

bool editLayerRasterFilters(Editor& editor, const LayerPtr& layer,
                            bool maskTarget, QWidget* parent)
{
    if (!layer) return false;
    if (maskTarget) {
        if (layer->imageMask.isNull()) return false;
        if (layer->type != LayerType::Image && layer->type != LayerType::Tile) return false;
    } else if (layer->type != LayerType::Image && layer->type != LayerType::Tile &&
               layer->type != LayerType::Group) {
        return false;
    }

    const bool contactOnly = !maskTarget &&
        (layer->type == LayerType::Tile || layer->type == LayerType::Group);
    QVector<RasterLayerFilter>& target = maskTarget ? layer->maskFilters : layer->imageFilters;
    const QVector<RasterLayerFilter> original = target;
    QVector<RasterLayerFilter> working = target;
    if (contactOnly) {
        for (int i = working.size() - 1; i >= 0; --i)
            if (working[i].type != QLatin1String("contactShadow")) working.removeAt(i);
    }
    const DocSnapshot before = editor.snapshotDoc();

    QDialog dialog(parent);
    dialog.setWindowTitle(maskTarget ? QObject::tr("Efeitos da máscara")
                                     : (contactOnly ? QObject::tr("Sombra de contato")
                                                        : QObject::tr("Efeitos da camada")));
    dialog.resize(780, 620);
    auto* root = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(maskTarget
        ? QObject::tr("Estes efeitos mudam apenas a forma como a máscara aparece. Use-os para suavizar bordas, criar desgaste ou deixar o recorte mais irregular, sem alterar a imagem, a pintura ou os tiles originais.")
        : (contactOnly
           ? (layer->type == LayerType::Group
              ? QObject::tr("Cria uma sombra curta usando o conjunto das camadas dentro deste grupo. Tiles marcados para aparecer acima do personagem não entram nessa sombra, evitando achatar a profundidade do cenário.")
              : QObject::tr("Cria uma sombra curta perto das bordas dos tiles, como a sombra que aparece onde uma parede encosta no chão. A máscara da camada é respeitada e tiles com prioridade não entram no efeito."))
           : QObject::tr("Adicione efeitos sem alterar permanentemente a imagem original. A ordem importa: um efeito pode mudar o resultado do próximo. Você pode desligar, reordenar ou remover qualquer efeito depois.")), &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* cacheHint = new QLabel(QObject::tr("A prévia atualiza enquanto você ajusta os valores. O editor reaproveita o resultado quando nada mudou para manter o mapa leve."), &dialog);
    cacheHint->setProperty("uiRole", QStringLiteral("hint"));
    cacheHint->setWordWrap(true);
    root->addWidget(cacheHint);

    auto* content = new QHBoxLayout;
    root->addLayout(content, 1);

    auto* left = new QVBoxLayout;
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    left->addWidget(list, 1);

    auto* addRow = new QHBoxLayout;
    auto* typeCombo = new QComboBox(&dialog);
    if (!contactOnly) {
        typeCombo->addItem(icons::get(QStringLiteral("filter-gaussian")), QObject::tr("Desfoque suave"), QStringLiteral("gaussianBlur"));
        typeCombo->addItem(icons::get(QStringLiteral("filter-directional")), QObject::tr("Desfoque em direção"), QStringLiteral("directionalBlur"));
        typeCombo->addItem(icons::get(QStringLiteral("filter-noise")), QObject::tr("Granulação"), QStringLiteral("noise"));
    }
    if (!maskTarget)
        typeCombo->addItem(icons::get(QStringLiteral("filter-contact-shadow")), QObject::tr("Sombra de contato"), QStringLiteral("contactShadow"));
    auto* add = new QPushButton(icons::get(QStringLiteral("layer-add")), QObject::tr("Adicionar"), &dialog);
    addRow->addWidget(typeCombo, 1); addRow->addWidget(add);
    left->addLayout(addRow);

    auto* buttons = new QHBoxLayout;
    auto* duplicate = new QPushButton(icons::get(QStringLiteral("duplicate")), QObject::tr("Duplicar"), &dialog);
    auto* up = new QPushButton(icons::get(QStringLiteral("up")), QObject::tr("Subir"), &dialog);
    auto* down = new QPushButton(icons::get(QStringLiteral("down")), QObject::tr("Descer"), &dialog);
    auto* remove = new QPushButton(icons::get(QStringLiteral("layer-remove")), QObject::tr("Remover"), &dialog);
    buttons->addWidget(duplicate); buttons->addWidget(up); buttons->addWidget(down); buttons->addWidget(remove);
    left->addLayout(buttons);
    content->addLayout(left, 1);

    auto* editorBox = new QGroupBox(QObject::tr("Ajustes do efeito"), &dialog);
    auto* editorRoot = new QVBoxLayout(editorBox);
    auto* enabled = new QCheckBox(QObject::tr("Efeito ligado"), editorBox);
    editorRoot->addWidget(enabled);
    auto* stack = new QStackedWidget(editorBox);
    editorRoot->addWidget(stack, 1);
    content->addWidget(editorBox, 1);

    // Página: Gaussiano
    auto* gaussianPage = new QWidget(stack);
    auto* gaussianForm = new QFormLayout(gaussianPage);
    auto* gaussianRadius = new QDoubleSpinBox(gaussianPage); gaussianRadius->setRange(0,64); gaussianRadius->setDecimals(1); gaussianRadius->setSuffix(QObject::tr(" px"));
    auto* gaussianStrength = new QSpinBox(gaussianPage); gaussianStrength->setRange(0,100); gaussianStrength->setSuffix(QStringLiteral("%"));
    auto* gaussianQuality = new QComboBox(gaussianPage); gaussianQuality->addItems({QObject::tr("Rápida"), QObject::tr("Equilibrada"), QObject::tr("Alta")});
    gaussianForm->addRow(QObject::tr("Suavidade"), gaussianRadius); gaussianForm->addRow(QObject::tr("Quanto aplicar"), gaussianStrength); gaussianForm->addRow(QObject::tr("Qualidade da prévia"), gaussianQuality);
    gaussianRadius->setToolTip(QObject::tr("Aumente para deixar bordas e detalhes mais suaves. Valores baixos fazem mudanças discretas; valores altos espalham mais o desfoque."));
    gaussianStrength->setToolTip(QObject::tr("Controla quanto do desfoque aparece. 0% mantém o original; 100% usa todo o efeito."));
    gaussianQuality->setToolTip(QObject::tr("Qualidade maior deixa o resultado mais suave, mas exige mais processamento enquanto você edita."));
    stack->addWidget(gaussianPage);

    // Página: Direcional
    auto* directionalPage = new QWidget(stack);
    auto* directionalForm = new QFormLayout(directionalPage);
    auto* directionalLength = new QDoubleSpinBox(directionalPage); directionalLength->setRange(0,128); directionalLength->setDecimals(1); directionalLength->setSuffix(QObject::tr(" px"));
    auto* directionalAngle = new QDoubleSpinBox(directionalPage); directionalAngle->setRange(-180,180); directionalAngle->setDecimals(1); directionalAngle->setSuffix(QStringLiteral("°"));
    auto* directionalStrength = new QSpinBox(directionalPage); directionalStrength->setRange(0,100); directionalStrength->setSuffix(QStringLiteral("%"));
    auto* directionalQuality = new QComboBox(directionalPage); directionalQuality->addItems({QObject::tr("Rápida"), QObject::tr("Equilibrada"), QObject::tr("Alta")});
    directionalForm->addRow(QObject::tr("Distância do arrasto"), directionalLength); directionalForm->addRow(QObject::tr("Direção"), directionalAngle); directionalForm->addRow(QObject::tr("Quanto aplicar"), directionalStrength); directionalForm->addRow(QObject::tr("Qualidade da prévia"), directionalQuality);
    directionalLength->setToolTip(QObject::tr("Define o quanto a imagem será esticada na direção escolhida."));
    directionalAngle->setToolTip(QObject::tr("Escolhe para que lado o efeito se espalha. Útil para chuva, movimento, manchas e luzes alongadas."));
    directionalStrength->setToolTip(QObject::tr("Controla o quanto do efeito aparece sobre o original."));
    stack->addWidget(directionalPage);

    // Página: Ruído
    auto* noisePage = new QWidget(stack);
    auto* noiseForm = new QFormLayout(noisePage);
    auto* noiseAmount = new QSpinBox(noisePage); noiseAmount->setRange(0,100); noiseAmount->setSuffix(QStringLiteral("%"));
    auto* noiseScale = new QDoubleSpinBox(noisePage); noiseScale->setRange(1,64); noiseScale->setDecimals(1); noiseScale->setSuffix(QObject::tr(" px"));
    auto* noiseStrength = new QSpinBox(noisePage); noiseStrength->setRange(0,100); noiseStrength->setSuffix(QStringLiteral("%"));
    auto* noiseMono = new QCheckBox(QObject::tr("Monocromático"), noisePage);
    auto* noiseSeed = new QSpinBox(noisePage); noiseSeed->setRange(0,9999999);
    noiseForm->addRow(QObject::tr("Quantidade"), noiseAmount); noiseForm->addRow(QObject::tr("Tamanho das manchas"), noiseScale); noiseForm->addRow(QObject::tr("Quanto aplicar"), noiseStrength); noiseForm->addRow(QString(), noiseMono); noiseForm->addRow(QObject::tr("Variação"), noiseSeed);
    noiseAmount->setToolTip(QObject::tr("Controla quanto de irregularidade será criada. Use pouco para sujeira sutil e mais para superfícies bem desgastadas."));
    noiseScale->setToolTip(QObject::tr("Valores pequenos criam grãos finos. Valores maiores criam manchas mais largas."));
    noiseMono->setToolTip(QObject::tr("Ligado: a variação usa apenas claro e escuro. Desligado: também pode variar as cores."));
    noiseSeed->setToolTip(QObject::tr("Troque este número para obter outra distribuição mantendo os mesmos ajustes."));
    stack->addWidget(noisePage);

    // Página: Sombra de contato
    auto* contactPage = new QWidget(stack);
    auto* contactLayout = new QVBoxLayout(contactPage);
    auto* contactInfo = new QLabel(QObject::tr("Cria uma sombra pequena onde a forma parece encostar em outra superfície. Use valores curtos para dar volume a paredes, caixas, degraus e objetos sem parecer uma sombra projetada comum."), contactPage);
    contactInfo->setWordWrap(true); contactLayout->addWidget(contactInfo);
    auto* contactForm = new QFormLayout;
    auto* contactRadius = new QDoubleSpinBox(contactPage); contactRadius->setRange(0,64); contactRadius->setDecimals(1); contactRadius->setSuffix(QObject::tr(" px"));
    auto* contactDistance = new QDoubleSpinBox(contactPage); contactDistance->setRange(0,64); contactDistance->setDecimals(1); contactDistance->setSuffix(QObject::tr(" px"));
    auto* contactAngle = new QDoubleSpinBox(contactPage); contactAngle->setRange(-180,180); contactAngle->setDecimals(1); contactAngle->setSuffix(QStringLiteral("°"));
    auto* contactSpread = new QDoubleSpinBox(contactPage); contactSpread->setRange(0,32); contactSpread->setDecimals(1); contactSpread->setSuffix(QObject::tr(" px"));
    auto* contactOpacity = new QSpinBox(contactPage); contactOpacity->setRange(0,100); contactOpacity->setSuffix(QStringLiteral("%"));
    auto* contactStrength = new QSpinBox(contactPage); contactStrength->setRange(0,100); contactStrength->setSuffix(QStringLiteral("%"));
    auto* contactQuality = new QComboBox(contactPage); contactQuality->addItems({QObject::tr("Rápida"), QObject::tr("Equilibrada"), QObject::tr("Alta")});
    auto* contactColor = new QPushButton(QObject::tr("Escolher…"), contactPage);
    contactForm->addRow(QObject::tr("Suavidade"), contactRadius); contactForm->addRow(QObject::tr("Distância da borda"), contactDistance); contactForm->addRow(QObject::tr("Direção"), contactAngle); contactForm->addRow(QObject::tr("Aumentar área de contato"), contactSpread); contactForm->addRow(QObject::tr("Opacidade"), contactOpacity); contactForm->addRow(QObject::tr("Quanto aplicar"), contactStrength); contactForm->addRow(QObject::tr("Qualidade da prévia"), contactQuality); contactForm->addRow(QObject::tr("Cor da sombra"), contactColor);
    contactRadius->setToolTip(QObject::tr("Controla o quanto a borda da sombra fica macia. Valores pequenos deixam a sombra mais marcada; valores maiores deixam a transição mais suave."));
    contactDistance->setToolTip(QObject::tr("Afasta a sombra um pouco da borda. Para aparência de contato, prefira valores baixos."));
    contactAngle->setToolTip(QObject::tr("Escolhe para que lado a sombra se desloca. 90° normalmente funciona bem para paredes encontrando o chão."));
    contactSpread->setToolTip(QObject::tr("Faz a sombra ocupar uma área um pouco maior ao redor da forma. Use pouco para manter o efeito natural."));
    contactOpacity->setToolTip(QObject::tr("Controla o quão escura a sombra fica."));
    contactStrength->setToolTip(QObject::tr("Mistura o efeito com a camada original. Diminua se a sombra estiver chamando atenção demais."));
    contactQuality->setToolTip(QObject::tr("Qualidade maior suaviza melhor a sombra, mas usa mais processamento durante a edição."));
    contactLayout->addLayout(contactForm); contactLayout->addStretch(1);
    stack->addWidget(contactPage);

    auto* reset = new QPushButton(icons::get(QStringLiteral("refresh")), QObject::tr("Restaurar valores iniciais"), editorBox);
    editorRoot->addWidget(reset);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(box);

    bool updating = false;
    int currentIndex = -1;
    QTimer previewTimer(&dialog);
    previewTimer.setSingleShot(true);
    previewTimer.setInterval(55);
    QObject::connect(&previewTimer, &QTimer::timeout, &dialog, [&] { emit editor.mapChanged(); });

    auto applyPreview = [&] {
        target = working;
        // Debounce curto: girar um SpinBox não força vários blurs completos no
        // mesmo frame. O cache cuida das repinturas seguintes.
        previewTimer.start();
    };

    auto refreshList = [&] {
        updating = true;
        const int keep = qBound(0, currentIndex, qMax(0, working.size() - 1));
        list->clear();
        for (const RasterLayerFilter& f : working) {
            auto* item = new QListWidgetItem(rasterLayerFilterTypeLabel(f.type), list);
            item->setCheckState(f.enabled ? Qt::Checked : Qt::Unchecked);
            const QString summary = rasterLayerFilterSummary(f);
            if (!summary.isEmpty()) item->setText(item->text() + QStringLiteral(" — ") + summary);
            item->setIcon(icons::get(f.type == QLatin1String("gaussianBlur") ? QStringLiteral("filter-gaussian") :
                                    f.type == QLatin1String("directionalBlur") ? QStringLiteral("filter-directional") :
                                    f.type == QLatin1String("noise") ? QStringLiteral("filter-noise") : QStringLiteral("filter-contact-shadow")));
        }
        currentIndex = working.isEmpty() ? -1 : keep;
        if (currentIndex >= 0) list->setCurrentRow(currentIndex);
        updating = false;
    };

    auto fillEditor = [&] {
        updating = true;
        const bool valid = currentIndex >= 0 && currentIndex < working.size();
        editorBox->setEnabled(valid);
        duplicate->setEnabled(valid); up->setEnabled(valid && currentIndex > 0); down->setEnabled(valid && currentIndex + 1 < working.size()); remove->setEnabled(valid);
        if (!valid) { updating = false; return; }
        const RasterLayerFilter& f = working[currentIndex];
        enabled->setChecked(f.enabled);
        int page = 0;
        if (f.type == QLatin1String("gaussianBlur")) {
            page = 0; gaussianRadius->setValue(f.radius); gaussianStrength->setValue(int(f.strength*100)); gaussianQuality->setCurrentIndex(qualityIndex(f.quality));
        } else if (f.type == QLatin1String("directionalBlur")) {
            page = 1; directionalLength->setValue(f.radius); directionalAngle->setValue(f.angle); directionalStrength->setValue(int(f.strength*100)); directionalQuality->setCurrentIndex(qualityIndex(f.quality));
        } else if (f.type == QLatin1String("noise")) {
            page = 2; noiseAmount->setValue(int(f.amount*100)); noiseScale->setValue(f.scale); noiseStrength->setValue(int(f.strength*100)); noiseMono->setChecked(f.monochrome); noiseSeed->setValue(qMax(0,f.seed));
        } else {
            page = 3; contactRadius->setValue(f.radius); contactDistance->setValue(f.distance); contactAngle->setValue(f.angle); contactSpread->setValue(f.spread); contactOpacity->setValue(int(f.opacity*100)); contactStrength->setValue(int(f.strength*100)); contactQuality->setCurrentIndex(qualityIndex(f.quality));
            contactColor->setStyleSheet(QStringLiteral("background:%1").arg(f.color.name()));
        }
        stack->setCurrentIndex(page);
        updating = false;
    };

    auto changed = [&] {
        if (updating || currentIndex < 0 || currentIndex >= working.size()) return;
        RasterLayerFilter& f = working[currentIndex];
        f.enabled = enabled->isChecked();
        if (f.type == QLatin1String("gaussianBlur")) {
            f.radius=gaussianRadius->value(); f.strength=gaussianStrength->value()/100.0; f.quality=gaussianQuality->currentIndex();
        } else if (f.type == QLatin1String("directionalBlur")) {
            f.radius=directionalLength->value(); f.angle=directionalAngle->value(); f.strength=directionalStrength->value()/100.0; f.quality=directionalQuality->currentIndex();
        } else if (f.type == QLatin1String("noise")) {
            f.amount=noiseAmount->value()/100.0; f.scale=noiseScale->value(); f.strength=noiseStrength->value()/100.0; f.monochrome=noiseMono->isChecked(); f.seed=noiseSeed->value();
        } else if (f.type == QLatin1String("contactShadow")) {
            f.radius=contactRadius->value(); f.distance=contactDistance->value(); f.angle=contactAngle->value(); f.spread=contactSpread->value(); f.opacity=contactOpacity->value()/100.0; f.strength=contactStrength->value()/100.0; f.quality=contactQuality->currentIndex();
        }
        applyPreview();
        const QSignalBlocker blocker(list);
        if (QListWidgetItem* item = list->item(currentIndex)) {
            item->setCheckState(f.enabled ? Qt::Checked : Qt::Unchecked);
            item->setText(rasterLayerFilterTypeLabel(f.type) + QStringLiteral(" — ") + rasterLayerFilterSummary(f));
        }
    };

    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) { if (updating) return; currentIndex=row; fillEditor(); });
    QObject::connect(list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* item) {
        if (updating) return; const int row=list->row(item); if(row<0||row>=working.size())return; working[row].enabled=item->checkState()==Qt::Checked; currentIndex=row; applyPreview(); fillEditor();
    });
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] { working.push_back(makeDefaultFilter(typeCombo->currentData().toString())); currentIndex=working.size()-1; refreshList(); fillEditor(); applyPreview(); });
    QObject::connect(duplicate, &QPushButton::clicked, &dialog, [&] { if(currentIndex<0||currentIndex>=working.size())return; RasterLayerFilter copy=working[currentIndex]; copy.id=idGen(); working.insert(currentIndex+1,copy); ++currentIndex; refreshList(); fillEditor(); applyPreview(); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { if(currentIndex<0||currentIndex>=working.size())return; working.removeAt(currentIndex); currentIndex=qMin(currentIndex,working.size()-1); refreshList(); fillEditor(); applyPreview(); });
    QObject::connect(up, &QPushButton::clicked, &dialog, [&] { if(currentIndex<=0)return; working.swapItemsAt(currentIndex,currentIndex-1); --currentIndex; refreshList(); fillEditor(); applyPreview(); });
    QObject::connect(down, &QPushButton::clicked, &dialog, [&] { if(currentIndex<0||currentIndex+1>=working.size())return; working.swapItemsAt(currentIndex,currentIndex+1); ++currentIndex; refreshList(); fillEditor(); applyPreview(); });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] { if(currentIndex<0||currentIndex>=working.size())return; const bool wasEnabled=working[currentIndex].enabled; const QString type=working[currentIndex].type; const QString id=working[currentIndex].id; working[currentIndex]=makeDefaultFilter(type); working[currentIndex].id=id; working[currentIndex].enabled=wasEnabled; refreshList(); fillEditor(); applyPreview(); });

    for (QObject* obj : {static_cast<QObject*>(enabled), static_cast<QObject*>(gaussianRadius), static_cast<QObject*>(gaussianStrength), static_cast<QObject*>(gaussianQuality), static_cast<QObject*>(directionalLength), static_cast<QObject*>(directionalAngle), static_cast<QObject*>(directionalStrength), static_cast<QObject*>(directionalQuality), static_cast<QObject*>(noiseAmount), static_cast<QObject*>(noiseScale), static_cast<QObject*>(noiseStrength), static_cast<QObject*>(noiseMono), static_cast<QObject*>(noiseSeed), static_cast<QObject*>(contactRadius), static_cast<QObject*>(contactDistance), static_cast<QObject*>(contactAngle), static_cast<QObject*>(contactSpread), static_cast<QObject*>(contactOpacity), static_cast<QObject*>(contactStrength), static_cast<QObject*>(contactQuality)}) {
        if (auto* cb = qobject_cast<QCheckBox*>(obj)) QObject::connect(cb, &QCheckBox::toggled, &dialog, changed);
        else if (auto* sb = qobject_cast<QSpinBox*>(obj)) QObject::connect(sb, qOverload<int>(&QSpinBox::valueChanged), &dialog, changed);
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(obj)) QObject::connect(dsb, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, changed);
        else if (auto* combo = qobject_cast<QComboBox*>(obj)) QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, changed);
    }
    QObject::connect(contactColor, &QPushButton::clicked, &dialog, [&] {
        if(currentIndex<0||currentIndex>=working.size())return;
        QColor chosen=QColorDialog::getColor(working[currentIndex].color,&dialog,QObject::tr("Cor da sombra de contato"),QColorDialog::ShowAlphaChannel);
        if(!chosen.isValid())return; working[currentIndex].color=chosen; contactColor->setStyleSheet(QStringLiteral("background:%1").arg(chosen.name())); applyPreview();
    });

    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    refreshList(); fillEditor();
    const int result = dialog.exec();
    previewTimer.stop();
    if (result != QDialog::Accepted) {
        target = original;
        emit editor.mapChanged();
        return false;
    }
    target = working;
    if (working != original) {
        editor.pushDocHistory(before, maskTarget ? QObject::tr("Editar efeitos da máscara") : QObject::tr("Editar efeitos da camada"));
        emit editor.layersChanged();
        emit editor.mapChanged();
        return true;
    }
    return false;
}

} // namespace ui
