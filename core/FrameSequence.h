// ============================================================================
//  FrameSequence.h — Regra comum para spritesheets/imagens em sequência.
//
//  Pictures e Panoramas guardam seus campos no formato legado do projeto,
//  mas usam esta estrutura para decidir qual frame deve aparecer. Assim a
//  semântica de FPS, loop e frame fixo não se duplica entre subsistemas.
// ============================================================================
#pragma once

#include <QRect>
#include <QSize>

namespace core {

enum class FrameSequenceIssue {
    None,
    ImageUnavailable,
    CountExceedsGrid,
    GridExceedsImage,
    ImageNotDivisible
};

struct FrameSequenceValidation {
    FrameSequenceIssue issue = FrameSequenceIssue::None;
    int capacity = 1;
    QSize frameSize;

    bool valid() const { return issue == FrameSequenceIssue::None; }
};

struct FrameSequence {
    bool enabled = false;
    int count = 1;
    int columns = 1;
    int rows = 1;
    int firstFrame = 0;
    double fps = 12.0;
    bool loop = true;
    bool playing = true;

    int frameAt(double ageSeconds) const;
    QRect rectAt(const QSize& imageSize, double ageSeconds) const;
    FrameSequenceValidation validate(const QSize& imageSize) const;
    bool dynamic() const { return enabled && playing && fps > 0.0 && count > 1; }
};

} // namespace core
