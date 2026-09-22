// ============================================================================
// LudoFilterSystem.h — runtime do stack de filtros LUDO RC2.85.4.
//
// Contrato:
//  * slots 1..99 por tipo; maior slot ativo vence dentro do mesmo tipo;
//  * tipos diferentes continuam fundidos no mesmo shader;
//  * nenhum filtro ativo => pipeline legado, sem pass/framebuffer extra;
//  * escopo World/Pictures/HUD é decidido pelo render graph;
//  * RC2.85.3: filtros de vizinhança no World amostram a WorldScene composta,
//    nunca cada tile/UV do atlas isoladamente;
//  * Save/Load preserva cada slot e transição.
// ============================================================================
#pragma once

#include "core/FilterSystem.h"

#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QSize>

namespace game {

/// 17 vec4 (272 bytes) enviados após MVP + ScreenTone. Blur 2.0 + Tilt-Shift 2.0 sem novo pass.
struct LudoFilterUniforms
{
    float chromaticIntensityPx=0, chromaticEdgeStart=.55f, chromaticFalloff=2, chromaticMix=0;
    float chromaticMode=1, vignetteScopeMask=0, blurScopeMask=0, activeMask=0;
    float noiseIntensity=0, noiseGrainSizePx=2, noiseSpeed=0, timeSeconds=0;
    float scanlineIntensity=0, scanlineSpacingPx=3, scanlineWhiteSweep=0, scanlineTimeSeconds=0;
    float sweepSpeed=0, sweepWidth=.055f, sweepIntensity=0, sweepDelaySeconds=0;
    float vignetteIntensity=0, vignetteRadius=.68f, vignetteSoftness=.28f, noisePackedModeSeed=0;
    float blurRadiusPx=0, blurDirection=2, noiseContrast=1, noiseColorAmount=0;
    float tiltBlurPx=0, tiltCenterY=.5f, tiltFocusWidth=.25f, tiltFalloff=.2f;
    float chromaticScopeMask=0, noiseScopeMask=0, scanlineScopeMask=0, tiltScopeMask=0;
    float viewportWidth=1, viewportHeight=1, invViewportWidth=1, invViewportHeight=1;
    float scanlineStyle=0, scanlineThickness=.5f, scanlineSoftness=.22f, scanlineScrollSpeed=0;
    float scanlinePhasePx=0, scanlineInterlaceAmount=.55f, scanlineInterlaceSpeed=60, scanlineSweepSoftness=1;
    float scanlineSweepRed=1, scanlineSweepGreen=1, scanlineSweepBlue=1, scanlineReserved=0;
    float blurStyle=0, blurStrength=1, blurQuality=1, blurEdgePreservation=0;
    float blurAngleDegrees=0, blurReserved1=0, blurReserved2=0, blurReserved3=0;
    float tiltStyle=0, tiltStrength=1, tiltQuality=1, tiltEdgePreservation=0;
    float tiltAngleDegrees=0, tiltUpperBlur=1, tiltLowerBlur=1, tiltReserved=0;
};

template <typename Config>
struct FilterTransitionChannel
{
    Config current;
    Config from;
    Config target;
    double elapsed=0.0;
    double duration=0.0;
    bool transitioning=false;
};

class LudoFilterSystem
{
public:
    void reset();

    void setChromaticAberration(core::ChromaticAberrationConfig c,double durationSeconds,int slot=1);
    void setNoise(core::NoiseFilterConfig c,double durationSeconds,int slot=1);
    void setScanlines(core::ScanlineFilterConfig c,double durationSeconds,int slot=1);
    void setVignette(core::VignetteFilterConfig c,double durationSeconds,int slot=1);
    void setBlur(core::BlurFilterConfig c,double durationSeconds,int slot=1);
    void setTiltShift(core::TiltShiftFilterConfig c,double durationSeconds,int slot=1);

    void clearChromaticAberration(double durationSeconds,int slot=0);
    void clearNoise(double durationSeconds,int slot=0);
    void clearScanlines(double durationSeconds,int slot=0);
    void clearVignette(double durationSeconds,int slot=0);
    void clearBlur(double durationSeconds,int slot=0);
    void clearTiltShift(double durationSeconds,int slot=0);
    bool clearFilter(const QString& filterId,double durationSeconds,int slot=0);
    void clearAll(double durationSeconds,int slot=0);
    void clearAll();

    bool update(double dt);
    bool hasActiveFilters() const;
    bool isTransitioning() const;
    int activeFilterCount() const;
    bool affects(core::FilterRenderDomain domain) const;
    bool requiresComposedSampling(core::FilterRenderDomain domain) const;
    bool fullFrameEligible() const;

    core::ChromaticAberrationConfig chromaticAberration() const;
    core::NoiseFilterConfig noise() const;
    core::ScanlineFilterConfig scanlines() const;
    core::VignetteFilterConfig vignette() const;
    core::BlurFilterConfig blur() const;
    core::TiltShiftFilterConfig tiltShift() const;
    QStringList activeSlotDescriptions() const;

    LudoFilterUniforms uniforms(const QSize& logicalViewport) const;
    void applyCpu(QImage& frame,QImage& scratch,core::FilterRenderDomain domain) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

private:
    QMap<int,FilterTransitionChannel<core::ChromaticAberrationConfig>> m_chromatic;
    QMap<int,FilterTransitionChannel<core::NoiseFilterConfig>> m_noise;
    QMap<int,FilterTransitionChannel<core::ScanlineFilterConfig>> m_scanlines;
    QMap<int,double> m_scanlineSweepAge; ///< relógio por slot; nasce em zero para entrada natural da faixa
    QMap<int,FilterTransitionChannel<core::VignetteFilterConfig>> m_vignette;
    QMap<int,FilterTransitionChannel<core::BlurFilterConfig>> m_blur;
    QMap<int,FilterTransitionChannel<core::TiltShiftFilterConfig>> m_tiltShift;
    double m_timeSeconds=0.0;
};

} // namespace game
