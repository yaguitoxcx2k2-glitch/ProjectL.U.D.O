#pragma once
#include "RpgMakerExporter.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>

namespace ui {
inline void editMapDepth(core::Editor& ed, QWidget* parent)
{
    auto* doc = ed.doc();
    if (!doc) return;
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Níveis do mapa — chão e plataforma"));
    dialog.resize(880, 760);
    auto* root = new QVBoxLayout(&dialog);
    auto* enabled = new QCheckBox(QObject::tr("Ativar sistema de níveis neste mapa"));
    enabled->setChecked(doc->map.depthEnabled);
    root->addWidget(enabled);
    auto* form = new QFormLayout;
    auto* scale = new QSpinBox; scale->setRange(70, 100); scale->setSuffix(" %");
    scale->setValue(qRound(doc->map.depthScale * 100));
    scale->setValue(100); scale->hide(); scale->setParent(&dialog); // legacy storage only
    auto* initial = new QComboBox;
    initial->addItems({QObject::tr("0 — Chão"), QObject::tr("1 — Ponte")});
    initial->setCurrentIndex(doc->map.depthStartLevel);
    form->addRow(QObject::tr("Nível ao entrar neste mapa:"), initial);
    root->addLayout(form);
    auto* info = new QLabel(QObject::tr(
        "Escadas: coordenadas de células, começando em 0. Chão = início; ponte = fim.\n"
        "Use trechos retos horizontais ou verticais, sem cruzamentos. A faixa tem uma célula de largura.\n"
        "Desenhe as escadas no nível 1. Deixe os tiles da passagem livres de colisão nos dois níveis."));
    info->setWordWrap(true); root->addWidget(info);
    auto* table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({"Chão X", "Chão Y", "Ponte X", "Ponte Y"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setMaximumHeight(160); root->addWidget(table);
    auto addRow = [=](const QJsonObject& t) {
        const int row = table->rowCount(); table->insertRow(row);
        const QStringList keys{"x0", "y0", "x1", "y1"};
        for (int col = 0; col < 4; ++col) {
            auto* cell = new QSpinBox;
            cell->setRange(0, qMax(0, (col % 2 ? doc->map.height : doc->map.width) - 1));
            cell->setValue(t.value(keys[col]).toInt()); table->setCellWidget(row, col, cell);
        }
    };
    for (const auto& t : doc->map.depthTransitions) addRow(t.toObject());
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(QObject::tr("Adicionar escada"));
    auto* remove = new QPushButton(QObject::tr("Remover selecionada"));
    buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch(); root->addLayout(buttons);
    QObject::connect(add, &QPushButton::clicked, &dialog, [=] { addRow(QJsonObject{}); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [=] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
    auto* controls = new QHBoxLayout;
    auto* viewLevel = new QComboBox; viewLevel->addItems({"Prévia: no chão", "Prévia: na ponte"});
    auto* px = new QSpinBox; px->setRange(0, qMax(0, doc->map.width - 1)); px->setValue(doc->map.width / 2);
    auto* py = new QSpinBox; py->setRange(0, qMax(0, doc->map.height - 1)); py->setValue(doc->map.height / 2);
    auto* previewButton = new QPushButton("Atualizar prévia");
    controls->addWidget(viewLevel); controls->addWidget(new QLabel("Jogador X:")); controls->addWidget(px);
    controls->addWidget(new QLabel("Y:")); controls->addWidget(py); controls->addWidget(previewButton);
    root->addLayout(controls);
    auto* preview = new QLabel; preview->setMinimumHeight(210); preview->setAlignment(Qt::AlignCenter); root->addWidget(preview, 1);
    root->addWidget(new QLabel("Prévia estática: ponto vermelho = jogador. Animações e eventos são verificados no RPG Maker de destino."));
    auto refresh = [=, &ed] {
        const QSize size(800, 300);
        const QPoint origin(qRound((px->value() + .5) * doc->map.tileWidth - size.width()/2),
                            qRound((py->value() + 1.) * doc->map.tileHeight - size.height()/2));
        const int margin = 200;
        const QRect rect(origin - QPoint(margin,margin), size + QSize(margin*2,margin*2));
        QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(doc->map.background);
        QPainter p(&result);

        // Runtime Contract v5 divide o cenário estático em bandas de renderização.
        // A prévia de profundidade percorre as mesmas bandas, mas mantém a
        // parte estática de células que também possuem conteúdo dinâmico.
        rpgMaker::RuntimeOrdering previewOrdering = rpgMaker::buildRuntimeOrdering(ed, *doc);
        previewOrdering.dynamicCells.clear();

        for (int level = 0; level < 2; ++level) {
            const double factor = 1.;
            p.save(); p.translate(size.width()/2., size.height()/2.); p.scale(factor, factor); p.translate(-size.width()/2., -size.height()/2.);
            for (int band = 0; band < previewOrdering.bandCount; ++band) {
                for (bool above : {false, true}) {
                    p.drawImage(-margin, -margin,
                                rpgMaker::renderStatic(ed, *doc, previewOrdering, band, rect, above, 0, level));
                }
            }
            p.restore();
        }
        p.setBrush(Qt::red); p.setPen(Qt::white); p.drawEllipse(QPoint(size.width()/2, size.height()/2), 5, 5);
        p.end(); preview->setPixmap(QPixmap::fromImage(result).scaled(size, Qt::KeepAspectRatio, Qt::FastTransformation));
    };
    QObject::connect(previewButton, &QPushButton::clicked, &dialog, refresh);
    QObject::connect(viewLevel, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [=](int) { refresh(); });
    refresh();
    auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(actions);
    QObject::connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(actions, &QDialogButtonBox::accepted, &dialog, [&] {
        QJsonArray transitions; QSet<QString> occupied;
        for (int row = 0; row < table->rowCount(); ++row) {
            int v[4]; for (int c = 0; c < 4; ++c) v[c] = static_cast<QSpinBox*>(table->cellWidget(row,c))->value();
            if ((v[0] == v[2]) == (v[1] == v[3])) {
                QMessageBox::warning(&dialog, "Escada inválida", "Cada escada precisa de dois pontos diferentes na mesma linha ou coluna."); return;
            }
            const int steps = qAbs(v[2]-v[0]) + qAbs(v[3]-v[1]);
            for (int i = 0; i <= steps; ++i) {
                const QString key = QString::number(v[0]+(v[2]-v[0])*i/steps)+","+QString::number(v[1]+(v[3]-v[1])*i/steps);
                if (occupied.contains(key)) { QMessageBox::warning(&dialog, "Escadas sobrepostas", "As faixas das escadas não podem se cruzar ou compartilhar células."); return; }
                occupied.insert(key);
            }
            transitions.append(QJsonObject{{"x0",v[0]},{"y0",v[1]},{"x1",v[2]},{"y1",v[3]}});
        }
        const auto before = ed.snapshotDoc();
        doc->map.depthEnabled = enabled->isChecked(); doc->map.depthScale = scale->value()/100.;
        doc->map.depthStartLevel = initial->currentIndex(); doc->map.depthTransitions = transitions;
        ed.pushDocHistory(before, QObject::tr("Configurar profundidade"));
        emit ed.mapChanged(); dialog.accept();
    });
    dialog.exec();
}
}
