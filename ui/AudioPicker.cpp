#include "AudioPicker.h"
#include "UniversalAssetPicker.h"

namespace ui {

bool chooseGameAudio(core::Editor& ed, QWidget* parent, const QString& title,
                     QString& source, int& volume, const QString& context)
{
    return UniversalAssetPickerDialog::chooseAudio(ed, parent, context, title, source, volume);
}

} // namespace ui
