// ============================================================================
// FilterSystem.h — contrato de dados do Ludo Filter System.
//
// RC2.85 / Filter System v3: todos os filtros compartilham o mesmo contrato de
// escopo, slots 1..99, persistência, Validator, Preview e backends CPU/QRhi.
// O núcleo não conhece QRhi nem widgets.
// ============================================================================
#pragma once

#include <QVariantMap>
#include <QString>

namespace core {

inline constexpr int LudoFilterMinSlot = 1;
inline constexpr int LudoFilterMaxSlot = 99;
inline constexpr double ChromaticAberrationMaxPixels = 32.0;
inline constexpr double NoiseMaxGrainPixels = 64.0;
inline constexpr double NoiseMaxSpeed = 20.0;
inline constexpr double NoiseMaxContrast = 4.0;
inline constexpr double ScanlineMaxSpacingPixels = 32.0;
inline constexpr double ScanlineMaxSweepSpeed = 5.0;
inline constexpr double ScanlineMaxSweepDelaySeconds = 60.0;
inline constexpr double ScanlineMaxScrollSpeed = 120.0;
inline constexpr double ScanlineMaxInterlaceSpeed = 120.0;
inline constexpr double BlurMaxRadiusPixels = 24.0;
inline constexpr double TiltShiftMaxBlurPixels = 24.0;

enum class FilterRenderDomain { World = 1, Pictures = 2, Hud = 4 };

struct FilterScopeConfig
{
    bool world = true;
    bool pictures = false;
    bool hud = false;

    int mask() const;
    bool includes(FilterRenderDomain domain) const;
    bool valid() const { return world || pictures || hud; }
    QVariantMap toVariantMap() const;
    static FilterScopeConfig fromVariantMap(const QVariantMap& params);
};

enum class ChromaticAberrationMode { Normal, Lens };
QString chromaticAberrationModeId(ChromaticAberrationMode mode);
QString chromaticAberrationModeLabel(ChromaticAberrationMode mode);
ChromaticAberrationMode chromaticAberrationModeFromId(const QString& id);

struct ChromaticAberrationConfig
{
    ChromaticAberrationMode mode = ChromaticAberrationMode::Lens;
    double intensityPixels = 0.0;
    double edgeStart = 0.55;
    double falloff = 2.0;
    double mix = 1.0;
    FilterScopeConfig scope;

    bool active() const { return intensityPixels > 0.0001 && mix > 0.0001 && scope.valid(); }
    void normalize();
    QVariantMap toVariantMap() const;
    static ChromaticAberrationConfig fromVariantMap(const QVariantMap& params);
};

enum class NoiseStyle { LegacyBlock = 0, FilmGrain = 1 };
enum class NoiseTemporalMode { Static = 0, Flicker = 1, Drift = 2, Smooth = 3 };
QString noiseStyleId(NoiseStyle style);
QString noiseStyleLabel(NoiseStyle style);
NoiseStyle noiseStyleFromId(const QString& id);
QString noiseTemporalModeId(NoiseTemporalMode mode);
QString noiseTemporalModeLabel(NoiseTemporalMode mode);
NoiseTemporalMode noiseTemporalModeFromId(const QString& id);

struct NoiseFilterConfig
{
    double intensity = 0.0;
    double grainSizePixels = 2.0;
    double speed = 1.0;
    NoiseStyle style = NoiseStyle::LegacyBlock; // projetos antigos preservam o visual RC2.85
    NoiseTemporalMode temporalMode = NoiseTemporalMode::Flicker;
    int seed = 0;
    double contrast = 1.0;
    double colorAmount = 0.0; // 0 = monocromático, 1 = RGB independente
    FilterScopeConfig scope;

    bool active() const { return intensity > 0.0001 && scope.valid(); }
    void normalize();
    QVariantMap toVariantMap() const;
    static NoiseFilterConfig fromVariantMap(const QVariantMap& params);
};

enum class ScanlineStyle { Legacy = 0, SoftCrt = 1, SharpCrt = 2, Interlaced = 3 };
QString scanlineStyleId(ScanlineStyle style);
QString scanlineStyleLabel(ScanlineStyle style);
ScanlineStyle scanlineStyleFromId(const QString& id);

struct ScanlineFilterConfig
{
    double intensity = 0.0;
    double spacingPixels = 3.0;
    ScanlineStyle style = ScanlineStyle::Legacy;
    double thickness = 0.50;      ///< fração do período ocupada pela linha escura
    double softness = 0.22;       ///< feather normalizado da borda da linha
    double phasePixels = 0.0;     ///< offset vertical fixo em pixels lógicos
    double scrollSpeed = 0.0;     ///< pixels lógicos por segundo
    double interlaceAmount = 0.55;///< 0..1: diferença entre fields pares/ímpares
    double interlaceSpeed = 60.0; ///< alternâncias por segundo
    bool whiteSweep = false;
    double sweepSpeed = 0.35;
    double sweepWidth = 0.055;
    double sweepSoftness = 1.0;   ///< 0 duro, 1 feather largo; 1 preserva a sweep Legacy
    double sweepIntensity = 0.45;
    int sweepRed = 255;
    int sweepGreen = 255;
    int sweepBlue = 255;
    double sweepDelaySeconds = 0.0; ///< pausa entre uma varredura branca e outra
    FilterScopeConfig scope;

    bool active() const { return scope.valid() && (intensity > 0.0001 || (whiteSweep && sweepIntensity > 0.0001)); }
    void normalize();
    QVariantMap toVariantMap() const;
    static ScanlineFilterConfig fromVariantMap(const QVariantMap& params);
};

struct VignetteFilterConfig
{
    double intensity = 0.0;
    double radius = 0.68;
    double softness = 0.28;
    FilterScopeConfig scope;

    bool active() const { return intensity > 0.0001 && scope.valid(); }
    void normalize();
    QVariantMap toVariantMap() const;
    static VignetteFilterConfig fromVariantMap(const QVariantMap& params);
};

enum class BlurDirection { Horizontal, Vertical, Full };
enum class BlurStyle { LegacyGaussian = 0, GaussianSoft = 1, GaussianSharp = 2, Box = 3, Directional = 4, Pixel = 5 };
enum class BlurQuality { Low = 0, Medium = 1, High = 2, Ultra = 3 };
QString blurStyleId(BlurStyle style);
QString blurStyleLabel(BlurStyle style);
BlurStyle blurStyleFromId(const QString& id);
QString blurQualityId(BlurQuality quality);
QString blurQualityLabel(BlurQuality quality);
BlurQuality blurQualityFromId(const QString& id);
QString blurDirectionId(BlurDirection direction);
QString blurDirectionLabel(BlurDirection direction);
BlurDirection blurDirectionFromId(const QString& id);

struct BlurFilterConfig
{
    double radiusPixels = 0.0;
    BlurDirection direction = BlurDirection::Full; // usado pelo Legacy e como fallback
    BlurStyle style = BlurStyle::LegacyGaussian;   // campos ausentes preservam projetos antigos
    BlurQuality quality = BlurQuality::Medium;
    double strength = 1.0;                         // mistura original -> blur
    double edgePreservation = 0.0;                 // 0 = blur clássico, 1 = protege contrastes
    double angleDegrees = 0.0;                     // Directional: 0° horizontal, 90° vertical
    FilterScopeConfig scope;

    bool active() const { return radiusPixels > 0.0001 && strength > 0.0001 && scope.valid(); }
    void normalize();
    QVariantMap toVariantMap() const;
    static BlurFilterConfig fromVariantMap(const QVariantMap& params);
};

enum class TiltShiftStyle { LegacyGaussian = 0, GaussianSoft = 1, GaussianSharp = 2, Box = 3 };
QString tiltShiftStyleId(TiltShiftStyle style);
QString tiltShiftStyleLabel(TiltShiftStyle style);
TiltShiftStyle tiltShiftStyleFromId(const QString& id);

struct TiltShiftFilterConfig
{
    double blurPixels = 0.0;
    double centerY = 0.5;
    double focusWidth = 0.25;
    double falloff = 0.2;
    TiltShiftStyle style = TiltShiftStyle::LegacyGaussian; // campos ausentes preservam RC2.85.4
    BlurQuality quality = BlurQuality::Medium;
    double strength = 1.0;
    double edgePreservation = 0.0;
    double angleDegrees = 0.0;              // 0° = faixa horizontal
    double upperBlurAmount = 1.0;           // multiplicador acima do plano de foco
    double lowerBlurAmount = 1.0;           // multiplicador abaixo do plano de foco
    FilterScopeConfig scope;

    bool active() const { return blurPixels > 0.0001 && strength > 0.0001 && scope.valid(); }
    void normalize();
    QVariantMap toVariantMap() const;
    static TiltShiftFilterConfig fromVariantMap(const QVariantMap& params);
};

enum class LudoFilterType { ChromaticAberration, Noise, Scanlines, Vignette, Blur, TiltShift };
QString ludoFilterTypeId(LudoFilterType type);
QString ludoFilterTypeLabel(LudoFilterType type);
bool ludoFilterTypeFromId(const QString& id, LudoFilterType* out);

int normalizedLudoFilterSlot(const QVariantMap& params);

} // namespace core
