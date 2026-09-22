// ============================================================================
// RuntimeBackendMatrix.h — matriz oficial GPU-first do runtime 4.0.
//
// O objetivo não é duplicar renderização: cada linha declara qual estado
// compartilhado alimenta o QRhi. O raster de referência é utilitário de teste
// e geração de conteúdo, nunca um runtime selecionável pelo usuário.
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace game {

struct RuntimeBackendMatrixEntry {
    QString feature;
    QString sharedContract;
    bool gpu = true;
    bool referenceRaster = true;
};

const QVector<RuntimeBackendMatrixEntry>& runtimeBackendMatrix();
bool runtimeBackendMatrixComplete();
QJsonArray runtimeBackendMatrixJson();

} // namespace game
