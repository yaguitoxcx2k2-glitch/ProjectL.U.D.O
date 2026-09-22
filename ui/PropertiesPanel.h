// ============================================================================
//  PropertiesPanel.h — Inspetor de propriedades (equivalente a #propsContent
//  com as abas mapPropGroup / layerPropGroup / tilesetPropGroup /
//  objectPropGroup / randomPropGroup / wangPropGroup).
// ============================================================================
#pragma once

#include "core/Editor.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QPushButton;
class QToolButton;
QT_END_NAMESPACE

namespace ui {

class CollapsibleSection;

class PropertiesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanel(core::Editor& ed, QWidget* parent = nullptr);

    void refresh();
signals:
    void statusMessage(const QString& text);

private:
    QWidget* buildMapTab();
    QWidget* buildLayerTab();
    QWidget* buildTilesetTab();
    QWidget* buildObjectTab();
    QWidget* buildRandomTab();
    QWidget* buildPatternsTab();

    void refreshMap();
    void refreshLayer();
    void refreshTileset();
    void refreshObject();
    void refreshRandom();
    void refreshPatterns();
    void scheduleRefresh();
    void updatePatternPreview();
    void activateSelectedPattern(bool announce = true);
    void syncContextTab();

    core::MapObject* selectedObject();

    core::Editor& ed;
    QTabBar*        m_tabs = nullptr;
    QStackedWidget* m_stack = nullptr;
    QToolButton*    m_pin = nullptr;
    bool          m_updating = false;
    bool          m_refreshQueued = false;

    // mapa
    QSpinBox *m_mapW = nullptr, *m_mapH = nullptr, *m_mapTW = nullptr, *m_mapTH = nullptr;
    QPushButton* m_mapBg = nullptr;
    QLineEdit* m_panoramaPath = nullptr;
    QPushButton *m_panoramaChoose = nullptr, *m_panoramaRemove = nullptr;
    QCheckBox *m_panoramaVisible = nullptr, *m_panoramaFit = nullptr, *m_panoramaRepeat = nullptr;
    QSpinBox* m_panoramaOpacity = nullptr;
    QWidget* m_reflectionGroup = nullptr;
    QCheckBox* m_reflectionEnvironmentEnabled = nullptr;
    QLineEdit* m_reflectionEnvironmentPath = nullptr;
    QPushButton *m_reflectionEnvironmentChoose = nullptr, *m_reflectionEnvironmentRemove = nullptr;
    QSpinBox* m_reflectionEnvironmentOpacity = nullptr;
    QSpinBox* m_reflectionOffsetY = nullptr;
    QComboBox *m_reflectionEnvironmentFit = nullptr, *m_reflectionEnvironmentBlend = nullptr;
    QCheckBox* m_reflectionEnvironmentFlipY = nullptr;
    QLabel* m_mapInfoLabel = nullptr;

    // camada
    QLineEdit* m_layerName = nullptr;
    QLabel*    m_layerType = nullptr;
    QSlider*   m_layerOpacity = nullptr;
    QLabel*    m_layerOpacityLabel = nullptr;
    QComboBox* m_layerBlend = nullptr;
    QSpinBox  *m_layerOffX = nullptr, *m_layerOffY = nullptr;
    QCheckBox *m_layerVisible = nullptr, *m_layerLocked = nullptr, *m_layerMask = nullptr,
              *m_layerMaskBase = nullptr, *m_layerAbove = nullptr, *m_layerAlphaLock = nullptr;
    CollapsibleSection* m_imageTransformGroup = nullptr;
    QLabel* m_imageSource = nullptr;
    QDoubleSpinBox *m_imageScaleX = nullptr, *m_imageScaleY = nullptr, *m_imageRotation = nullptr;
    QComboBox* m_imageFilter = nullptr;
    QCheckBox *m_imageFlipX = nullptr, *m_imageFlipY = nullptr, *m_imageExport = nullptr;
    QCheckBox *m_imageRepeatX = nullptr, *m_imageRepeatY = nullptr;
    QPushButton *m_imageReplace = nullptr, *m_imageReset = nullptr;
    CollapsibleSection *m_parallaxGroup = nullptr, *m_parallaxMotionGroup = nullptr,
                       *m_parallaxAnimationGroup = nullptr, *m_parallaxEffectsGroup = nullptr,
                       *m_reflectionLayerGroup = nullptr;
    QFormLayout *m_parallaxForm = nullptr, *m_parallaxMotionForm = nullptr,
                *m_parallaxAnimationForm = nullptr, *m_parallaxEffectsForm = nullptr;
    QCheckBox* m_parallaxEnabled = nullptr;
    QDoubleSpinBox *m_parallaxFactorX = nullptr, *m_parallaxFactorY = nullptr,
                   *m_parallaxSpeedX = nullptr, *m_parallaxSpeedY = nullptr,
                   *m_parallaxOscillationX = nullptr, *m_parallaxOscillationY = nullptr,
                   *m_parallaxOscillationSpeed = nullptr, *m_parallaxAnimationFps = nullptr,
                   *m_parallaxEffectStrength = nullptr, *m_parallaxEffectSpeed = nullptr;
    QComboBox *m_parallaxMotionPreset = nullptr, *m_parallaxEffectPreset = nullptr;
    QCheckBox *m_parallaxRepeatX = nullptr, *m_parallaxRepeatY = nullptr,
              *m_parallaxSmoothMotion = nullptr, *m_parallaxAnimationEnabled = nullptr,
              *m_parallaxAnimationPingPong = nullptr;
    QSpinBox *m_parallaxAnimationColumns = nullptr, *m_parallaxAnimationRows = nullptr,
             *m_parallaxAnimationFrames = nullptr;
    QComboBox* m_reflectionLayerPreset = nullptr;
    QSpinBox *m_reflectionLayerOpacity = nullptr, *m_reflectionLayerBlur = nullptr,
             *m_reflectionLayerWave = nullptr;
    QPushButton *m_layerFiltersButton = nullptr, *m_maskFiltersButton = nullptr;

    // tileset
    QLabel*    m_tsInfo = nullptr;
    QLineEdit* m_tsName = nullptr;
    QListWidget* m_tsSources = nullptr;

    // objeto
    QLineEdit* m_objName = nullptr;
    QDoubleSpinBox *m_objX = nullptr, *m_objY = nullptr, *m_objW = nullptr, *m_objH = nullptr,
                   *m_objRotation = nullptr, *m_objScaleX = nullptr, *m_objScaleY = nullptr;
    QComboBox *m_objRotationFilter = nullptr, *m_objScaleFilter = nullptr;
    QSpinBox* m_objDepth = nullptr;
    QLabel* m_objInfo = nullptr;

    // aleatorio
    QListWidget* m_poolList = nullptr;
    QDoubleSpinBox* m_probSpin = nullptr;
    QLabel* m_probTarget = nullptr;
    QCheckBox* m_scatterEnabled = nullptr;
    QSpinBox* m_scatterPercent = nullptr;
    QCheckBox* m_gridFree = nullptr;
    QSpinBox* m_gridFreeJitter = nullptr;
    QSpinBox* m_gridFreeSpacing = nullptr;

    // patterns
    QListWidget* m_patternList = nullptr;
    QLabel* m_patternPreview = nullptr;
    QLabel* m_patternMeta = nullptr;
    QPushButton *m_patternRename = nullptr, *m_patternDelete = nullptr;
    bool m_patternSelectionSyncing = false;

};

} // namespace ui
