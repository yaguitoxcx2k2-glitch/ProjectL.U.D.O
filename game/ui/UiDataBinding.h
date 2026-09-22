#pragma once

#include "core/Editor.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace game { class GameState; }

namespace game::ui {

/// Resultado já convertido de todos os Data Bindings de um elemento. Campos
/// `has*` distinguem "sem binding" de valores válidos como false/0/vazio.
struct UiResolvedDataBindings {
    bool hasText = false;
    QString text;

    bool hasValue = false;
    double value = 0.0;
    bool hasMaximum = false;
    double maximum = 0.0;
    bool hasProgress = false;
    double progress = 0.0;

    bool hasVisible = false;
    bool visible = true;
    bool hasEnabled = false;
    bool enabled = true;
    bool hasOpacity = false;
    double opacity = 1.0;

    bool hasImage = false;
    QString imagePath;
    bool hasColor = false;
    QColor color = Qt::white;

    QStringList warnings;
};

/// Avaliador comum entre Designer e runtime. O formato persistido continua em
/// core::Editor; esta camada apenas resolve fontes contra GameState ou Preview
/// Data. Nenhuma função depende de QWidget.
class UiDataBindingResolver
{
public:
    static QVariant runtimeSourceValue(const core::UiDataBindingSettings& binding,
                                       const core::Editor& editor,
                                       const GameState& state,
                                       bool* ok = nullptr);
    static QVariant previewSourceValue(const core::UiDataBindingSettings& binding,
                                       const core::UiDataPreviewSettings& preview,
                                       bool* ok = nullptr);

    static QString runtimeText(const core::UiDataBindingSettings& binding,
                               const core::Editor& editor,
                               const GameState& state,
                               bool* ok = nullptr);
    static QString previewText(const core::UiDataBindingSettings& binding,
                               const core::UiDataPreviewSettings& preview,
                               bool* ok = nullptr);

    static UiResolvedDataBindings resolveRuntime(const core::UiLayoutElementSettings& element,
                                                 const core::Editor& editor,
                                                 const GameState& state);
    static UiResolvedDataBindings resolvePreview(const core::UiLayoutElementSettings& element,
                                                 const core::UiDataPreviewSettings& preview);
};

} // namespace game::ui
