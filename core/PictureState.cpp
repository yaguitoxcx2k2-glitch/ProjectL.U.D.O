#include "PictureState.h"

namespace core {

PictureTransformState PictureDef::transformState() const
{
    PictureTransformState s;
    s.x = x; s.y = y; s.scaleX = scaleX; s.scaleY = scaleY;
    s.opacity = opacity; s.angle = angle;
    s.anchor = anchor; s.anchorX = anchorX; s.anchorY = anchorY;
    return s;
}

void PictureDef::setTransformState(const PictureTransformState& s)
{
    x = s.x; y = s.y; scaleX = s.scaleX; scaleY = s.scaleY;
    opacity = s.opacity; angle = s.angle;
    anchor = s.anchor; anchorX = s.anchorX; anchorY = s.anchorY;
}

PictureDisplayState PictureDef::displayState() const
{
    PictureDisplayState s;
    s.blend = blend; s.space = space; s.layer = layer; s.smooth = smooth;
    s.flipH = flipH; s.flipV = flipV; s.duringBattle = duringBattle;
    s.eraseOnMapChange = eraseOnMapChange; s.affectedByTone = affectedByTone;
    return s;
}

void PictureDef::setDisplayState(const PictureDisplayState& s)
{
    blend = s.blend; space = s.space; layer = s.layer; smooth = s.smooth;
    flipH = s.flipH; flipV = s.flipV; duringBattle = s.duringBattle;
    eraseOnMapChange = s.eraseOnMapChange; affectedByTone = s.affectedByTone;
}

PictureAnimationState PictureDef::animationState() const
{
    PictureAnimationState s;
    s.frames = frameSequence();
    return s;
}

void PictureDef::setAnimationState(const PictureAnimationState& s)
{
    setFrameSequence(s.frames);
}

PicturePhysicsState PictureDef::physicsState() const
{
    PicturePhysicsState s;
    s.floatSpeed = floatSpeed; s.floatRange = floatRange;
    s.swaySpeed = swaySpeed; s.swayRange = swayRange;
    s.spinSpeed = spinSpeed; s.pulseSpeed = pulseSpeed; s.pulseRange = pulseRange;
    return s;
}

void PictureDef::setPhysicsState(const PicturePhysicsState& s)
{
    floatSpeed = s.floatSpeed; floatRange = s.floatRange;
    swaySpeed = s.swaySpeed; swayRange = s.swayRange;
    spinSpeed = s.spinSpeed; pulseSpeed = s.pulseSpeed; pulseRange = s.pulseRange;
}

PictureVisualState PictureDef::visualState() const
{
    PictureVisualState s;
    s.transform = transformState();
    s.display = displayState();
    s.animation = animationState();
    s.physics = physicsState();
    s.nineSlice = nineSlice;
    s.effects = fx;
    return s;
}

void PictureDef::setVisualState(const PictureVisualState& s)
{
    setTransformState(s.transform);
    setDisplayState(s.display);
    setAnimationState(s.animation);
    setPhysicsState(s.physics);
    nineSlice = s.nineSlice;
    fx = s.effects;
}

} // namespace core
