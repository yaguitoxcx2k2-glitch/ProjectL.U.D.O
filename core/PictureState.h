// ============================================================================
// PictureState.h — componentes lógicos do estado de uma Picture.
//
// PictureDef mantém os campos planos para compatibilidade com projetos,
// plugins e código existente. Estes value objects agrupam responsabilidades
// para editor, previews e código novo sem exigir migração destrutiva.
// ============================================================================
#pragma once

#include "Picture.h"

namespace core {

struct PictureTransformState {
    double x = 0.0;
    double y = 0.0;
    double scaleX = 100.0;
    double scaleY = 100.0;
    double opacity = 255.0;
    double angle = 0.0;
    PictureAnchor anchor = PictureAnchor::TopLeft;
    double anchorX = 0.0;
    double anchorY = 0.0;
};

struct PictureDisplayState {
    PictureBlend blend = PictureBlend::Normal;
    PictureSpace space = PictureSpace::Screen;
    PictureLayer layer = PictureLayer::BelowMessage;
    bool smooth = false;
    bool flipH = false;
    bool flipV = false;
    bool duringBattle = true;
    bool eraseOnMapChange = false;
    bool affectedByTone = false;
};

struct PictureAnimationState {
    FrameSequence frames;
};

struct PicturePhysicsState {
    double floatSpeed = 0.0;
    double floatRange = 12.0;
    double swaySpeed = 0.0;
    double swayRange = 10.0;
    double spinSpeed = 0.0;
    double pulseSpeed = 0.0;
    double pulseRange = 8.0;

    bool active() const {
        return floatSpeed != 0.0 || swaySpeed != 0.0 || spinSpeed != 0.0 || pulseSpeed != 0.0;
    }
};

struct PictureVisualState {
    PictureTransformState transform;
    PictureDisplayState display;
    PictureAnimationState animation;
    PicturePhysicsState physics;
    PictureNineSlice nineSlice;
    VisualEffects effects;
};

} // namespace core
