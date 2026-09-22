// ============================================================================
// PicturePreviewController.h — reconstrução de estado para previews.
//
// Centraliza a regra de "como esta Picture estaria imediatamente antes deste
// comando". Os dialogs deixam de manter uma segunda mini-versão do runtime.
// ============================================================================
#pragma once

#include "core/EventModel.h"
#include "core/Picture.h"

#include <QString>
#include <QVector>
#include <optional>

namespace core { class Editor; }

namespace ui {

struct PictureReferenceEntry {
    QString label;
    core::PictureDef def;
};

QVector<PictureReferenceEntry> pictureReferences(const core::Editor& ed,
                                                  const core::EventCommand* current);

std::optional<core::PictureDef> pictureStateBeforeCommand(
    const core::Editor& ed, const core::EventCommand* current, int slot,
    bool* currentFound = nullptr);

/// Estado visual neutro usado quando o slot ainda não existia no fluxo.
core::PictureDef fallbackPreviewPicture(const core::Editor& ed, int slot);

} // namespace ui
