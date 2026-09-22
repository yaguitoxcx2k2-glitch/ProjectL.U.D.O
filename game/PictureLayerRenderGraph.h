#pragma once

#include "core/Picture.h"

#include <array>

namespace game {

/// Ponto exato da pilha visual em que cada PictureLayer entra. Este contrato
/// complementa RuntimeVisualStage: o enum histórico continua útil para macro
/// diagnóstico/validação de espaços, enquanto este grafo descreve a ordem
/// fina 0..9 usada igualmente pelo raster CPU de referência e pelo QRhi.
enum class PictureLayerRenderAnchor : unsigned char {
    AfterParallax = 0,
    AfterMapBelow,
    AfterEventsBelow,
    AfterActorsSame,
    AfterMapAbove,
    AfterEventsAbove,
    AfterWeather,
    AfterScreenEffects,
    AfterUi,
    AfterPresentation
};

struct PictureLayerRenderContract {
    core::PictureLayer layer;
    PictureLayerRenderAnchor anchor;
};

constexpr int pictureLayerIndex(core::PictureLayer layer)
{
    return static_cast<int>(layer);
}

constexpr PictureLayerRenderAnchor pictureLayerRenderAnchor(core::PictureLayer layer)
{
    return static_cast<PictureLayerRenderAnchor>(pictureLayerIndex(layer));
}

constexpr std::array<PictureLayerRenderContract, 10> pictureLayerRenderGraph()
{
    return {{
        {core::PictureLayer::AboveParallax,   PictureLayerRenderAnchor::AfterParallax},
        {core::PictureLayer::BelowTiles,      PictureLayerRenderAnchor::AfterMapBelow},
        {core::PictureLayer::BelowEvents,     PictureLayerRenderAnchor::AfterEventsBelow},
        {core::PictureLayer::SameAsPlayer,    PictureLayerRenderAnchor::AfterActorsSame},
        {core::PictureLayer::AboveTiles,      PictureLayerRenderAnchor::AfterMapAbove},
        {core::PictureLayer::AboveEvents,     PictureLayerRenderAnchor::AfterEventsAbove},
        {core::PictureLayer::AboveWeather,    PictureLayerRenderAnchor::AfterWeather},
        {core::PictureLayer::AboveAnimations, PictureLayerRenderAnchor::AfterScreenEffects},
        {core::PictureLayer::AboveMessage,    PictureLayerRenderAnchor::AfterUi},
        {core::PictureLayer::AboveTimers,     PictureLayerRenderAnchor::AfterPresentation}
    }};
}

constexpr const char* pictureLayerRenderAnchorName(PictureLayerRenderAnchor anchor)
{
    switch (anchor) {
    case PictureLayerRenderAnchor::AfterParallax:     return "AfterParallax";
    case PictureLayerRenderAnchor::AfterMapBelow:     return "AfterMapBelow";
    case PictureLayerRenderAnchor::AfterEventsBelow:  return "AfterEventsBelow";
    case PictureLayerRenderAnchor::AfterActorsSame:   return "AfterActorsSame";
    case PictureLayerRenderAnchor::AfterMapAbove:     return "AfterMapAbove";
    case PictureLayerRenderAnchor::AfterEventsAbove:  return "AfterEventsAbove";
    case PictureLayerRenderAnchor::AfterWeather:      return "AfterWeather";
    case PictureLayerRenderAnchor::AfterScreenEffects:return "AfterScreenEffects";
    case PictureLayerRenderAnchor::AfterUi:           return "AfterUi";
    case PictureLayerRenderAnchor::AfterPresentation: return "AfterPresentation";
    }
    return "Unknown";
}

static_assert(pictureLayerIndex(core::PictureLayer::AboveParallax) == 0);
static_assert(pictureLayerIndex(core::PictureLayer::BelowTiles) == 1);
static_assert(pictureLayerIndex(core::PictureLayer::BelowEvents) == 2);
static_assert(pictureLayerIndex(core::PictureLayer::SameAsPlayer) == 3);
static_assert(pictureLayerIndex(core::PictureLayer::AboveTiles) == 4);
static_assert(pictureLayerIndex(core::PictureLayer::AboveEvents) == 5);
static_assert(pictureLayerIndex(core::PictureLayer::AboveWeather) == 6);
static_assert(pictureLayerIndex(core::PictureLayer::AboveAnimations) == 7);
static_assert(pictureLayerIndex(core::PictureLayer::AboveMessage) == 8);
static_assert(pictureLayerIndex(core::PictureLayer::AboveTimers) == 9);

} // namespace game
