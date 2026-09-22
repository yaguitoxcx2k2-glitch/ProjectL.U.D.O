#pragma once

#include "game/SpriteBatch.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace game {

/// Segmento puro de CPU para um mesh de mapa residente na GPU. As posições
/// permanecem em World Space; a câmera é aplicada depois pelo uniform do QRhi.
struct GpuMapSegmentData {
    QString texture;
    quint32 firstVertex = 0;
    quint32 vertexCount = 0;
    core::PictureBlend blend = core::PictureBlend::Normal;
    double depth = 0.0;
};

/// Payload independente de QRhi usado para construir/testar o cache de meshes.
/// 8 floats por vértice: pos.xy, uv.xy, rgba.
struct GpuMapMeshData {
    QVector<float> vertices;
    QVector<GpuMapSegmentData> below;
    QVector<GpuMapSegmentData> stars;
    QVector<GpuMapSegmentData> above;

    quint32 vertexBytes() const { return quint32(vertices.size() * qsizetype(sizeof(float))); }
    int drawCalls() const { return below.size() + stars.size() + above.size(); }
    int quads() const { return vertices.size() / (8 * 6); }
};

/// Converte os três batchers estáticos do mapa para um único payload de
/// vértices em World Space. Retorna false se algum batch violar o contrato do
/// mapa (por exemplo, Screen Space), evitando esconder regressões de composição.
bool buildGpuMapMeshData(const SpriteBatcher& below,
                         const SpriteBatcher& stars,
                         const SpriteBatcher& above,
                         GpuMapMeshData* out,
                         QString* error = nullptr);

} // namespace game
