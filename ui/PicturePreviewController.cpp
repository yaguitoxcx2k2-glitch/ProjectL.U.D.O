#include "PicturePreviewController.h"

#include "core/Editor.h"

#include <QObject>

namespace ui {

QVector<PictureReferenceEntry> pictureReferences(const core::Editor& ed,
                                                  const core::EventCommand* current)
{
    QVector<PictureReferenceEntry> out;
    auto appendCommands = [&](const QVector<core::EventCommand>& commands,
                              const QString& context) {
        for (const core::EventCommand& command : commands) {
            if (&command == current || (command.type != QLatin1String("picture.show") &&
                                        command.type != QLatin1String("picture.text")))
                continue;
            core::PictureDef def = core::PictureDef::fromParams(command.params);
            const QString name = def.rich.enabled
                ? QObject::tr("Texto")
                : (!def.assetName.isEmpty() ? def.assetName : QObject::tr("Imagem"));
            out.push_back({QObject::tr("%1 · Picture %2 · %3")
                               .arg(context).arg(def.number).arg(name), def});
        }
    };

    for (const core::MapDoc& map : ed.docs) {
        for (const core::MapEvent& event : map.events) {
            for (int page = 0; page < event.pages.size(); ++page) {
                appendCommands(event.pages[page].commands,
                    QObject::tr("%1 / %2 / página %3")
                        .arg(map.name, event.name).arg(page + 1));
            }
        }
    }
    for (const core::CommonEvent& common : ed.commonEvents) {
        appendCommands(common.commands,
            QObject::tr("Evento comum %1: %2").arg(common.number).arg(common.name));
    }
    return out;
}

std::optional<core::PictureDef> pictureStateBeforeCommand(
    const core::Editor& ed, const core::EventCommand* current, int slot,
    bool* currentFound)
{
    if (currentFound) *currentFound = false;

    auto inspect = [&](const QVector<core::EventCommand>& commands)
        -> std::optional<core::PictureDef> {
        core::PictureDef state;
        bool visible = false;
        for (const core::EventCommand& command : commands) {
            if (&command == current) {
                if (currentFound) *currentFound = true;
                return visible ? std::optional<core::PictureDef>(state) : std::nullopt;
            }

            const QVariantMap& p = command.params;
            const int number = p.value(QStringLiteral("number"), 1).toInt();
            if ((command.type == QLatin1String("picture.show") ||
                 command.type == QLatin1String("picture.text")) && number == slot) {
                state = core::PictureDef::fromParams(p);
                if (command.type == QLatin1String("picture.text")) {
                    state.rich = core::PictureRichText::fromParams(
                        p.value(QStringLiteral("rich")).toMap());
                    state.rich.enabled = true;
                }
                visible = true;
                continue;
            }
            if ((command.type == QLatin1String("picture.eraseAll") ||
                 (command.type == QLatin1String("picture.erase") && number <= 0)) && visible) {
                visible = false;
                continue;
            }
            if (command.type == QLatin1String("picture.erase") && number == slot) {
                visible = false;
                continue;
            }
            if (!visible || number != slot) continue;

            auto assign = [&](const char* key, double& value) {
                const QString k = QString::fromLatin1(key);
                if (p.contains(k)) value = p.value(k).toDouble();
            };
            if (command.type == QLatin1String("picture.move")) {
                assign("x", state.x); assign("y", state.y);
                assign("scaleX", state.scaleX); assign("scaleY", state.scaleY);
                assign("opacity", state.opacity); assign("angle", state.angle);
            } else if (command.type == QLatin1String("picture.tween")) {
                const double target = p.value(QStringLiteral("target"), 0.0).toDouble();
                switch (core::picturePropFromId(p.value(QStringLiteral("prop")).toString())) {
                case core::PictureProp::X:       state.x = target; break;
                case core::PictureProp::Y:       state.y = target; break;
                case core::PictureProp::ScaleX:  state.scaleX = target; break;
                case core::PictureProp::ScaleY:  state.scaleY = target; break;
                case core::PictureProp::Opacity: state.opacity = target; break;
                case core::PictureProp::Angle:   state.angle = target; break;
                }
            } else if (command.type == QLatin1String("picture.zoomIn") ||
                       command.type == QLatin1String("picture.zoomOut")) {
                const double fallback = command.type == QLatin1String("picture.zoomIn") ? 150.0 : 50.0;
                state.scaleX = p.value(QStringLiteral("scaleX"), fallback).toDouble();
                state.scaleY = p.value(QStringLiteral("scaleY"), fallback).toDouble();
            } else if (command.type == QLatin1String("picture.physics")) {
                const bool remove = p.value(QStringLiteral("remove"), false).toBool();
                state.floatSpeed = remove ? 0.0 : p.value(QStringLiteral("floatSpeed"), 0.0).toDouble();
                state.floatRange = p.value(QStringLiteral("floatRange"), 12.0).toDouble();
                state.swaySpeed = remove ? 0.0 : p.value(QStringLiteral("swaySpeed"), 0.0).toDouble();
                state.swayRange = p.value(QStringLiteral("swayRange"), 10.0).toDouble();
                state.spinSpeed = remove ? 0.0 : p.value(QStringLiteral("spinSpeed"), 0.0).toDouble();
                state.pulseSpeed = remove ? 0.0 : p.value(QStringLiteral("pulseSpeed"), 0.0).toDouble();
                state.pulseRange = p.value(QStringLiteral("pulseRange"), 8.0).toDouble();
            } else if (command.type == QLatin1String("picture.anchor")) {
                state.anchor = core::pictureAnchorFromId(
                    p.value(QStringLiteral("anchor")).toString());
                state.anchorX = p.value(QStringLiteral("anchorX"), 0.0).toDouble();
                state.anchorY = p.value(QStringLiteral("anchorY"), 0.0).toDouble();
            } else if (command.type == QLatin1String("picture.effects")) {
                state.fx = core::VisualEffects::fromParams(
                    p.value(QStringLiteral("fx")).toMap());
            } else if (command.type == QLatin1String("picture.flip")) {
                state.flipH = p.value(QStringLiteral("flipH"), false).toBool();
                state.flipV = p.value(QStringLiteral("flipV"), false).toBool();
            } else if (command.type == QLatin1String("picture.display")) {
                state.space = core::pictureSpaceFromId(p.value(QStringLiteral("space")).toString());
                state.layer = core::pictureLayerFromId(p.value(QStringLiteral("layer")).toString());
                state.duringBattle = p.value(QStringLiteral("duringBattle"), true).toBool();
                state.eraseOnMapChange = p.value(QStringLiteral("eraseOnMapChange"), false).toBool();
                state.affectedByTone = p.value(QStringLiteral("affectedByTone"), false).toBool();
            } else if (command.type == QLatin1String("picture.clearEffects")) {
                state.fx = core::VisualEffects();
            }
        }
        return std::nullopt;
    };

    for (const core::MapDoc& map : ed.docs) {
        for (const core::MapEvent& event : map.events) {
            for (const core::EventPage& page : event.pages) {
                const auto state = inspect(page.commands);
                if (currentFound && *currentFound) return state;
            }
        }
    }
    for (const core::CommonEvent& common : ed.commonEvents) {
        const auto state = inspect(common.commands);
        if (currentFound && *currentFound) return state;
    }
    return std::nullopt;
}

core::PictureDef fallbackPreviewPicture(const core::Editor& ed, int slot)
{
    core::PictureDef d;
    d.number = slot;
    if (!ed.pictures.isEmpty()) {
        d.assetId = ed.pictures.first().id;
        d.assetName = ed.pictures.first().name;
    }
    d.anchor = core::PictureAnchor::Center;
    d.x = ed.gameResolution.width() / 2.0;
    d.y = ed.gameResolution.height() / 2.0;
    return d;
}

} // namespace ui
