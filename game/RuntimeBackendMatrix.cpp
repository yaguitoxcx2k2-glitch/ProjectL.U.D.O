#include "RuntimeBackendMatrix.h"

#include <QJsonObject>

namespace game {

const QVector<RuntimeBackendMatrixEntry>& runtimeBackendMatrix()
{
    static const QVector<RuntimeBackendMatrixEntry> matrix{
        {QStringLiteral("Panorama/Parallax"), QStringLiteral("RuntimePanoramaLayout")},
        {QStringLiteral("MapBelow/MapAbove"), QStringLiteral("GameSession + RuntimeRenderState")},
        {QStringLiteral("Actors/Player"), QStringLiteral("GameSession actor scene")},
        {QStringLiteral("Fog"), QStringLiteral("RuntimeRenderState projection")},
        {QStringLiteral("Weather"), QStringLiteral("RuntimeWeatherFrame")},
        {QStringLiteral("ScreenTone"), QStringLiteral("ScreenToneState after world composition")},
        {QStringLiteral("Pictures"), QStringLiteral("RuntimePictureTransform + PictureFrame")},
        {QStringLiteral("ScreenEffects"), QStringLiteral("RuntimeScreenEffectsState")},
        {QStringLiteral("LudoFilterSystem"), QStringLiteral("LudoFilterSystem + final QRhi post-pass / CPU reference")},
        // QPainter aqui gera pixels de texto/UI para uma textura; não é um
        // segundo runtime. Composição, apresentação e gameplay continuam QRhi.
        {QStringLiteral("UI/Text/Subtitles"), QStringLiteral("GameSession overlay / UiDrawList")},
        {QStringLiteral("Presentation"), QStringLiteral("logical resolution + letterbox")}
    };
    return matrix;
}

bool runtimeBackendMatrixComplete()
{
    for (const RuntimeBackendMatrixEntry& entry : runtimeBackendMatrix())
        if (entry.feature.isEmpty() || entry.sharedContract.isEmpty() || !entry.gpu)
            return false;
    return !runtimeBackendMatrix().isEmpty();
}

QJsonArray runtimeBackendMatrixJson()
{
    QJsonArray out;
    for (const RuntimeBackendMatrixEntry& entry : runtimeBackendMatrix())
        out.append(QJsonObject{{QStringLiteral("feature"), entry.feature},
                               {QStringLiteral("sharedContract"), entry.sharedContract},
                               {QStringLiteral("gpu"), entry.gpu},
                               {QStringLiteral("referenceRaster"), entry.referenceRaster},
                               {QStringLiteral("productRuntime"), QStringLiteral("QRhi")}});
    return out;
}

} // namespace game
