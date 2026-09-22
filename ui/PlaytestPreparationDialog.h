// ============================================================================
// PlaytestPreparationDialog.h — apresentação Editor-only do preload RC2.53.
// A janela apenas visualiza o trabalho reportado por RuntimePreloader; nenhum
// timer artificial avança a barra.
// ============================================================================
#pragma once

#include "game/RuntimePreloader.h"

#include <memory>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

struct PlaytestPreparationResult {
    std::unique_ptr<core::Editor> runtime;
    game::RuntimePreloadReport report;
    QString error;
    bool canceled = false;
};

PlaytestPreparationResult preparePlaytestRuntime(core::Editor& source,
                                                 const game::RuntimePreloadOptions& options,
                                                 QWidget* parent = nullptr);

} // namespace ui
