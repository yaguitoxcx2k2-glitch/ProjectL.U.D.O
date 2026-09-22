#include "game/GpuMapMeshData.h"

#include <QtMath>
#include <utility>

namespace game {
namespace {

bool appendBatcher(const SpriteBatcher& source,
                   QVector<float>& vertices,
                   QVector<GpuMapSegmentData>& segments,
                   QString* error)
{
    segments.reserve(segments.size() + source.batches().size());
    for (const DrawBatch& batch : source.batches()) {
        if (batch.space != RuntimeCoordinateSpace::World) {
            if (error) {
                *error = QStringLiteral("GPU map mesh exige World Space; recebeu %1")
                             .arg(QString::fromLatin1(runtimeCoordinateSpaceName(batch.space)));
            }
            return false;
        }
        if (batch.verts.isEmpty()) continue;
        GpuMapSegmentData segment;
        segment.texture = batch.texture;
        segment.firstVertex = quint32(vertices.size() / 8);
        segment.vertexCount = quint32(batch.verts.size());
        segment.blend = batch.blend;
        segment.depth = batch.depth;

        vertices.reserve(vertices.size() + batch.verts.size() * 8);
        for (const QuadVertex& v : batch.verts) {
            vertices << v.x << v.y << v.u << v.v
                     << v.r << v.g << v.b << -(2.0f + qAbs(v.a)); // domínio World (1) + Screen Tone
        }
        segments.push_back(std::move(segment));
    }
    return true;
}

} // namespace

bool buildGpuMapMeshData(const SpriteBatcher& below,
                         const SpriteBatcher& stars,
                         const SpriteBatcher& above,
                         GpuMapMeshData* out,
                         QString* error)
{
    if (!out) {
        if (error) *error = QStringLiteral("destino nulo para GPU map mesh");
        return false;
    }
    GpuMapMeshData data;
    const qsizetype totalVerts = qsizetype(below.totalQuads() + stars.totalQuads() + above.totalQuads()) * 6;
    data.vertices.reserve(totalVerts * 8);
    if (!appendBatcher(below, data.vertices, data.below, error) ||
        !appendBatcher(stars, data.vertices, data.stars, error) ||
        !appendBatcher(above, data.vertices, data.above, error)) {
        return false;
    }
    *out = std::move(data);
    return true;
}

} // namespace game
