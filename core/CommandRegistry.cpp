#include "CommandRegistry.h"

#include <initializer_list>
#include <QHash>
#include <QObject>
#include <QSet>

namespace core {

QString commandDomainId(CommandDomain domain)
{
    switch(domain){case CommandDomain::Text:return QStringLiteral("text");case CommandDomain::Flow:return QStringLiteral("flow");case CommandDomain::Movement:return QStringLiteral("movement");case CommandDomain::Fog:return QStringLiteral("fog");case CommandDomain::Picture:return QStringLiteral("picture");case CommandDomain::Runtime:return QStringLiteral("runtime");case CommandDomain::Plugin:return QStringLiteral("plugin");case CommandDomain::Unknown:return QStringLiteral("unknown");}return QStringLiteral("unknown");
}

QString commandDomainLabel(CommandDomain domain)
{
    switch(domain){case CommandDomain::Text:return QObject::tr("Texto");case CommandDomain::Flow:return QObject::tr("Fluxo / lógica");case CommandDomain::Movement:return QObject::tr("Movimento");case CommandDomain::Fog:return QObject::tr("Nevoeiro");case CommandDomain::Picture:return QObject::tr("Imagem");case CommandDomain::Runtime:return QObject::tr("Jogo");case CommandDomain::Plugin:return QObject::tr("Extensão visual");case CommandDomain::Unknown:return QObject::tr("Desconhecido");}return QObject::tr("Desconhecido");
}

QString commandCatalogPolicyId(CommandCatalogPolicy policy)
{
    switch (policy) {
    case CommandCatalogPolicy::Visible: return QStringLiteral("visible");
    case CommandCatalogPolicy::Structural: return QStringLiteral("structural");
    case CommandCatalogPolicy::Dynamic: return QStringLiteral("dynamic");
    case CommandCatalogPolicy::Indirect: return QStringLiteral("indirect");
    case CommandCatalogPolicy::Legacy: return QStringLiteral("legacy");
    }
    return QStringLiteral("visible");
}
QString commandExecutionRouteId(CommandExecutionRoute route)
{
    switch (route) {
    case CommandExecutionRoute::Unknown: return QStringLiteral("unknown");
    case CommandExecutionRoute::Interpreter: return QStringLiteral("interpreter");
    case CommandExecutionRoute::GameSession: return QStringLiteral("game-session");
    case CommandExecutionRoute::PluginExpansion: return QStringLiteral("plugin-expansion");
    case CommandExecutionRoute::LegacyNoOp: return QStringLiteral("legacy-no-op");
    }
    return QStringLiteral("unknown");
}

namespace {

bool startsWithAny(const QString& type, std::initializer_list<const char*> prefixes)
{
    for (const char* prefix : prefixes)
        if (type.startsWith(QLatin1String(prefix))) return true;
    return false;
}

bool equalsAny(const QString& type, std::initializer_list<const char*> values)
{
    for (const char* value : values)
        if (type == QLatin1String(value)) return true;
    return false;
}

bool stateMutationCommand(const QString& type)
{
    return type.startsWith(QLatin1String("quest.")) ||
           type.startsWith(QLatin1String("party.")) ||
           type == QLatin1String("inventory.change") ||
           type.startsWith(QLatin1String("actor."));
}

bool pageActivationCommand(const QString& type)
{
    return type.startsWith(QLatin1String("ludo.")) &&
           type != QLatin1String("ludo.command") &&
           // Begin/End definem uma região dentro de UMA pilha executável.
           // OnPageActivated executa comandos automáticos isoladamente e,
           // portanto, não pode preservar o pareamento/nesting dos marcadores.
           type != QLatin1String("ludo.cutscene.begin") &&
           type != QLatin1String("ludo.cutscene.end") &&
           type != QLatin1String("ludo.cutscene.settings") &&
           type != QLatin1String("ludo.screen.set") &&
           type != QLatin1String("ludo.screen.clear");
}


bool awaitableCommand(const QString& type)
{
    if (equalsAny(type, {"message", "choice.show", "subtitle.show", "subtitle.enqueue", "subtitle.wait",
                         "wait", "wait.until", "parallel.begin", "move.route",
                         "picture.move", "picture.tween", "picture.zoomIn", "picture.zoomOut",
                         "picture.moveGroup",
                         "picture.timeline.play",
                         "picture.physics", "picture.negative", "picture.transitionOut", "picture.wait",
                         "map.transfer", "game.ui.open", "input.number", "input.text", "input.confirm",
                         "input.item", "input.wait", "battle.start", "shop.open",
                         "ludo.camera.move", "ludo.camera.zoomOnly", "ludo.camera.moveOnly",
                         "ludo.camera.reset", "ludo.camera.restore",
                         "ludo.sprite.fade", "ludo.sprite.offset", "ludo.sprite.clearOffset",
                         "ludo.sprite.zoom", "ludo.sprite.resetZoom", "ludo.sprite.shake",
                         "ludo.screen.flash", "ludo.screen.fade", "ludo.screen.shake",
                         "ludo.filter.chromaticAberration", "ludo.filter.noise", "ludo.filter.scanlines",
                         "ludo.filter.vignette", "ludo.filter.blur", "ludo.filter.tiltShift", "ludo.filter.clear"}))
        return true;
    return false;
}

bool cutsceneBlocking(const QString& type)
{
    if (equalsAny(type, {"message", "subtitle.show", "subtitle.enqueue", "subtitle.wait", "wait", "wait.until", "parallel.begin",
                         "move.route", "picture.wait", "picture.move", "picture.zoomIn", "picture.zoomOut",
                         "picture.moveGroup", "picture.physics", "picture.transitionOut",
                         "ludo.sprite.fade", "ludo.sprite.offset", "ludo.sprite.clearOffset",
                         "ludo.sprite.zoom", "ludo.sprite.resetZoom", "ludo.sprite.shake", "ludo.sprite.phantom",
                         "ludo.screen.flash", "ludo.screen.fade", "ludo.screen.shake",
                         "ludo.filter.chromaticAberration", "ludo.filter.noise", "ludo.filter.scanlines",
                         "ludo.filter.vignette", "ludo.filter.blur", "ludo.filter.tiltShift", "ludo.filter.clear"}))
        return true;
    return type.startsWith(QLatin1String("ludo.camera.")) &&
           type != QLatin1String("ludo.camera.release");
}

CommandSchema makeSchema(const char* type, CommandDomain domain, bool pluginSafe = true,
                         std::initializer_list<const char*> params = {})
{
    CommandSchema schema;
    schema.type = QString::fromLatin1(type);
    schema.domain = domain;
    switch (domain) {
    case CommandDomain::Plugin: schema.executionRoute = CommandExecutionRoute::PluginExpansion; break;
    case CommandDomain::Runtime: schema.executionRoute = CommandExecutionRoute::GameSession; break;
    case CommandDomain::Text:
    case CommandDomain::Flow:
    case CommandDomain::Movement:
    case CommandDomain::Fog:
    case CommandDomain::Picture: schema.executionRoute = CommandExecutionRoute::Interpreter; break;
    case CommandDomain::Unknown: schema.executionRoute = CommandExecutionRoute::Unknown; break;
    }
    schema.pluginSafe = pluginSafe;
    schema.stateMutation = stateMutationCommand(schema.type);
    schema.awaitable = awaitableCommand(schema.type);
    schema.pageActivation = pageActivationCommand(schema.type);
    schema.skipDuringCutscene = cutsceneBlocking(schema.type);
    schema.sceneBoundary = domain == CommandDomain::Runtime &&
        startsWithAny(schema.type, {"map.", "game.", "input.", "shop.", "battle."});
    if (schema.type == QLatin1String("game.ui.open") ||
        schema.type == QLatin1String("game.ui.close")) schema.sceneBoundary = false;
    for (const char* key : params) schema.parameterKeys.push_back(QString::fromLatin1(key));
    return schema;
}

CommandSchema withCatalogPolicy(CommandSchema schema, CommandCatalogPolicy policy)
{
    schema.catalogPolicy = policy;
    return schema;
}

CommandSchema withExecutionRoute(CommandSchema schema, CommandExecutionRoute route)
{
    schema.executionRoute = route;
    return schema;
}

const QVector<CommandSchema>& schemaTable()
{
    // Tipos persistidos ou aceitos pelo runtime. Aliases exclusivos da UI
    // (como text.show) ficam registrados, porém não são liberados a plugins.
    static const QVector<CommandSchema> table = {
        makeSchema("message", CommandDomain::Text, true, {"text", "speaker", "position", "offsetX", "offsetY", "fontSize", "overflow", "localizationKey", "speakerLocalizationKey", "dialogueContent"}),
        makeSchema("dialogue.fastForward", CommandDomain::Text, true, {"speed"}),
        makeSchema("dialogue.skipMode", CommandDomain::Text, true, {"enabled"}),
        makeSchema("choice.show", CommandDomain::Text, true, {"choices", "disabledChoices", "conditions", "timeLimit", "defaultChoice", "showDisabledReason", "resultVariable", "cancelValue", "default", "position", "offsetX", "offsetY", "layout", "columns", "spacingX", "spacingY", "alignment", "boxMode", "fontFamily", "fontSize", "textEffects", "textGradient", "branches", "choiceLocalizationKeys"}),
        withCatalogPolicy(makeSchema("subtitle.show", CommandDomain::Text, true, {"track", "text", "duration", "voiceFile"}), CommandCatalogPolicy::Indirect),
        makeSchema("subtitle.enqueue", CommandDomain::Text, true, {"track", "text", "localizationKey", "speaker", "speakerLocalizationKey", "position", "textAlign", "anchor", "anchorEventId", "duration", "waitForInput", "waitForEnd", "typewriter", "offsetX", "offsetY", "transitionIn", "transitionOut", "voiceFile", "textEffects", "textGradient"}),
        makeSchema("subtitle.clearQueue", CommandDomain::Text, true, {"track"}),
        makeSchema("subtitle.configure", CommandDomain::Text, true, {"style"}),
        makeSchema("subtitle.clear", CommandDomain::Text),
        makeSchema("subtitle.clearFade", CommandDomain::Text, true, {"duration"}),
        makeSchema("subtitle.wait", CommandDomain::Text),

        makeSchema("switch.set", CommandDomain::Flow, true, {"id", "value"}),
        makeSchema("selfSwitch.set", CommandDomain::Flow, true, {"letter", "value"}),
        makeSchema("variable.set", CommandDomain::Flow, true, {"id", "rangeEndId", "op", "sourceSpec"}),
        makeSchema("variable.math", CommandDomain::Flow, true, {"id", "rangeEndId", "operation", "a", "b", "c", "terms"}),
        makeSchema("string.set", CommandDomain::Flow, true, {"id", "op", "sourceSpec", "search", "index", "length"}),
        makeSchema("value.get", CommandDomain::Flow, true, {"query", "valueType", "target"}),
        makeSchema("database.get", CommandDomain::Flow, true, {"databaseId", "recordSpec", "recordId", "fieldId", "target"}),
        makeSchema("database.set", CommandDomain::Flow, true, {"databaseId", "recordSpec", "recordId", "fieldId", "sourceSpec"}),
        makeSchema("database.find", CommandDomain::Flow, true, {"databaseId", "fieldId", "operation", "sourceSpec", "target"}),
        makeSchema("database.count", CommandDomain::Flow, true, {"databaseId", "target"}),
        makeSchema("database.exists", CommandDomain::Flow, true, {"databaseId", "recordSpec", "recordId", "target"}),
        makeSchema("database.copy", CommandDomain::Flow, true, {"databaseId", "sourceRecordSpec", "sourceRecordId", "targetRecordSpec", "targetRecordId"}),
        makeSchema("database.reset", CommandDomain::Flow, true, {"databaseId", "recordSpec", "recordId"}),
        makeSchema("database.recordInfo", CommandDomain::Flow, true, {"databaseId", "recordSpec", "recordId", "info", "target"}),
        makeSchema("database.each", CommandDomain::Flow, true, {"databaseId", "target"}),
        withCatalogPolicy(makeSchema("database.each.end", CommandDomain::Flow, true), CommandCatalogPolicy::Structural),
        makeSchema("map.runtime.tile", CommandDomain::Flow, true, {"mapId", "layerId", "x", "y", "clear", "tilesetId", "tx", "ty"}),
        makeSchema("map.runtime.fill", CommandDomain::Flow, true, {"mapId", "layerId", "x", "y", "width", "height", "clear", "tilesetId", "tx", "ty"}),
        makeSchema("map.runtime.copy", CommandDomain::Flow, true, {"mapId", "sourceLayerId", "sourceX", "sourceY", "width", "height", "targetLayerId", "targetX", "targetY"}),
        makeSchema("map.runtime.passage", CommandDomain::Flow, true, {"mapId", "x", "y", "mask"}),
        makeSchema("map.runtime.terrain", CommandDomain::Flow, true, {"mapId", "x", "y", "terrain"}),
        makeSchema("map.runtime.tileset", CommandDomain::Flow, true, {"mapId", "sourceTilesetId", "targetTilesetId"}),
        makeSchema("map.runtime.reset", CommandDomain::Flow, true, {"mapId", "scope", "layerId", "x", "y", "width", "height", "sourceTilesetId"}),
        makeSchema("if", CommandDomain::Flow, true, {"conditionTree"}),
        makeSchema("else", CommandDomain::Flow, true),
        makeSchema("endIf", CommandDomain::Flow, true),
        makeSchema("loop.begin", CommandDomain::Flow, true),
        withCatalogPolicy(makeSchema("loop.end", CommandDomain::Flow, true), CommandCatalogPolicy::Structural),
        makeSchema("loop.break", CommandDomain::Flow, true),
        makeSchema("repeat.begin", CommandDomain::Flow, true, {"countSpec", "count", "indexTarget"}),
        withCatalogPolicy(makeSchema("repeat.end", CommandDomain::Flow, true), CommandCatalogPolicy::Structural),
        makeSchema("repeat.break", CommandDomain::Flow, true),
        makeSchema("label", CommandDomain::Flow, true, {"name"}),
        makeSchema("jump", CommandDomain::Flow, true, {"label"}),
        makeSchema("wait", CommandDomain::Flow, true, {"frames"}),
        makeSchema("wait.until", CommandDomain::Flow, true, {"kind", "conditionTree", "timeoutFrames"}),
        makeSchema("parallel.begin", CommandDomain::Flow, true),
        withCatalogPolicy(makeSchema("parallel.end", CommandDomain::Flow, true), CommandCatalogPolicy::Structural),
        makeSchema("common.call", CommandDomain::Flow, true, {"commonId", "number", "arguments", "returnTarget"}),
        makeSchema("common.reserve", CommandDomain::Flow, true, {"commonId", "number", "arguments", "priority"}),
        makeSchema("map.event.call", CommandDomain::Flow, true, {"mapId", "eventId", "pageIndex"}),
        makeSchema("common.local.set", CommandDomain::Flow, true, {"id", "op", "sourceSpec"}),
        makeSchema("common.return", CommandDomain::Flow, true, {"sourceSpec"}),
        makeSchema("flow.exit", CommandDomain::Flow, true, {"scope", "sourceSpec"}),
        makeSchema("comment", CommandDomain::Flow, true, {"text"}),
        makeSchema("move.route", CommandDomain::Movement, true, {"route"}),
        makeSchema("move.route.control", CommandDomain::Movement, true, {"target", "action"}),

        makeSchema("fog.show", CommandDomain::Fog, true, {"slot", "image", "source", "opacity", "blend", "scrollX", "scrollY", "zoom", "tileRepeat", "fadeIn"}),
        makeSchema("fog.enable", CommandDomain::Fog, true, {"slot"}),
        makeSchema("fog.disable", CommandDomain::Fog, true, {"slot"}),
        makeSchema("fog.remove", CommandDomain::Fog, true, {"slot", "duration"}),
        makeSchema("fog.opacity", CommandDomain::Fog, true, {"slot", "opacity", "duration"}),
        makeSchema("fog.fadeIn", CommandDomain::Fog, true, {"slot", "opacity", "duration"}),
        makeSchema("fog.fadeOut", CommandDomain::Fog, true, {"slot", "duration"}),
        makeSchema("fog.blend", CommandDomain::Fog, true, {"slot", "blend"}),
        makeSchema("fog.scroll", CommandDomain::Fog, true, {"slot", "scrollX", "scrollY"}),
        makeSchema("fog.clear", CommandDomain::Fog),

        makeSchema("picture.show", CommandDomain::Picture, true, {"number", "source", "x", "y"}),
        makeSchema("picture.showByName", CommandDomain::Picture, true, {"logicalName", "source", "x", "y"}),
        makeSchema("picture.text", CommandDomain::Picture, true, {"number", "text", "x", "y"}),
        makeSchema("picture.move", CommandDomain::Picture, true, {"number", "duration", "wait"}),
        makeSchema("picture.setGroup", CommandDomain::Picture, true, {"number", "group"}),
        makeSchema("picture.moveGroup", CommandDomain::Picture, true, {"group", "duration", "wait"}),
        makeSchema("picture.eraseGroup", CommandDomain::Picture, true, {"group"}),
        makeSchema("picture.attach", CommandDomain::Picture, true, {"number", "target", "followAxis"}),
        makeSchema("picture.detach", CommandDomain::Picture, true, {"number"}),
        makeSchema("picture.timeline.define", CommandDomain::Picture, true, {"name", "duration", "loop", "keyframes"}),
        makeSchema("picture.timeline.play", CommandDomain::Picture, true, {"number", "name", "wait"}),
        makeSchema("picture.timeline.stop", CommandDomain::Picture, true, {"number"}),
        makeSchema("picture.onTouch", CommandDomain::Picture, true, {"number", "commonEventId"}),
        makeSchema("picture.onClick", CommandDomain::Picture, true, {"number", "commonEventId"}),
        withCatalogPolicy(makeSchema("picture.tween", CommandDomain::Picture, true, {"number", "prop", "target", "duration", "wait"}), CommandCatalogPolicy::Legacy),
        makeSchema("picture.zoomIn", CommandDomain::Picture, true, {"number", "scaleX", "scaleY", "duration", "ease", "wait"}),
        makeSchema("picture.zoomOut", CommandDomain::Picture, true, {"number", "scaleX", "scaleY", "duration", "ease", "wait"}),
        makeSchema("picture.physics", CommandDomain::Picture, true, {"number", "duration", "wait"}),
        makeSchema("picture.anchor", CommandDomain::Picture, true, {"number", "anchor"}),
        makeSchema("picture.effects", CommandDomain::Picture, true, {"number"}),
        makeSchema("picture.negative", CommandDomain::Picture, true, {"number", "enabled", "strength", "duration", "wait"}),
        makeSchema("picture.flip", CommandDomain::Picture, true, {"number", "flipH", "flipV"}),
        makeSchema("picture.display", CommandDomain::Picture, true, {"number"}),
        makeSchema("picture.clearEffects", CommandDomain::Picture, true, {"number"}),
        makeSchema("picture.transitionOut", CommandDomain::Picture, true, {"number", "duration", "wait"}),
        makeSchema("picture.erase", CommandDomain::Picture, true, {"number"}),
        makeSchema("picture.eraseAll", CommandDomain::Picture, true),
        makeSchema("picture.wait", CommandDomain::Picture, true, {"number"}),

        makeSchema("map.transfer", CommandDomain::Runtime, true, {"mapId", "x", "y", "direction", "fadeFrames", "useSpawn"}),
        makeSchema("game.save", CommandDomain::Runtime, true, {"slot"}),
        makeSchema("game.load", CommandDomain::Runtime, true, {"slot"}),
        makeSchema("game.restart", CommandDomain::Runtime, true),
        makeSchema("game.checkpoint", CommandDomain::Runtime, true),
        makeSchema("game.restoreCheckpoint", CommandDomain::Runtime, true),
        makeSchema("game.autosave", CommandDomain::Runtime, true),
        makeSchema("game.ui.open", CommandDomain::Runtime, true, {"screenId", "wait"}),
        withExecutionRoute(withCatalogPolicy(makeSchema("game.ui.close", CommandDomain::Runtime, false), CommandCatalogPolicy::Legacy), CommandExecutionRoute::LegacyNoOp),
        makeSchema("game.gameOver", CommandDomain::Runtime, true),
        makeSchema("game.returnTitle", CommandDomain::Runtime, true),
        withExecutionRoute(makeSchema("localization.set", CommandDomain::Runtime, true, {"locale"}), CommandExecutionRoute::Interpreter),
        makeSchema("input.number", CommandDomain::Runtime, true, {"variableId", "minimum", "maximum", "title", "prompt", "titleLocalizationKey", "promptLocalizationKey"}),
        makeSchema("input.text", CommandDomain::Runtime, true, {"stringId", "maximumLength", "allowCancel", "replace", "title", "prompt", "titleLocalizationKey", "promptLocalizationKey"}),
        makeSchema("input.confirm", CommandDomain::Runtime, true, {"resultVariable", "defaultYes", "cancelValue", "title", "prompt", "titleLocalizationKey", "promptLocalizationKey"}),
        makeSchema("input.item", CommandDomain::Runtime, true, {"variableId", "category", "title", "prompt", "titleLocalizationKey", "promptLocalizationKey"}),
        makeSchema("input.wait", CommandDomain::Runtime, true, {"action", "state"}),
        makeSchema("battle.start", CommandDomain::Runtime, true, {"troopId", "allowEscape", "resultVariable"}),
        makeSchema("shop.open", CommandDomain::Runtime, true, {"itemIds", "purchaseOnly"}),
        makeSchema("shop.inn", CommandDomain::Runtime, true, {"cost", "removeStates", "title", "prompt", "titleLocalizationKey", "promptLocalizationKey"}),
        makeSchema("weather.set", CommandDomain::Runtime, true, {"type", "intensity", "thunderSe", "thunderVolume"}),
        makeSchema("quest.start", CommandDomain::Runtime, true, {"questId", "target"}),
        makeSchema("quest.progress", CommandDomain::Runtime, true, {"questId", "operation", "amount"}),
        makeSchema("quest.complete", CommandDomain::Runtime, true, {"questId"}),
        makeSchema("quest.fail", CommandDomain::Runtime, true, {"questId"}),
        makeSchema("party.change", CommandDomain::Runtime, true, {"actorId", "operation", "level"}),
        makeSchema("party.gold", CommandDomain::Runtime, true, {"amount", "operation"}),
        makeSchema("inventory.change", CommandDomain::Runtime, true, {"itemId", "amount", "operation"}),
        makeSchema("actor.hp", CommandDomain::Runtime, true, {"actorId", "amount", "operation"}),
        makeSchema("actor.mp", CommandDomain::Runtime, true, {"actorId", "amount", "operation"}),
        makeSchema("actor.exp", CommandDomain::Runtime, true, {"actorId", "amount", "operation"}),
        makeSchema("actor.level", CommandDomain::Runtime, true, {"actorId", "amount", "operation"}),
        makeSchema("actor.state", CommandDomain::Runtime, true, {"actorId", "stateId", "operation"}),
        makeSchema("actor.equip", CommandDomain::Runtime, true, {"actorId", "itemId", "slot"}),
        makeSchema("audio.bgm", CommandDomain::Runtime, true, {"source", "volume", "loop", "fadeInMs", "transitionMs", "pitch", "pan"}),
        makeSchema("audio.bgs", CommandDomain::Runtime, true, {"source", "volume", "loop", "fadeInMs", "transitionMs", "pitch", "pan"}),
        makeSchema("audio.me", CommandDomain::Runtime, true, {"source", "volume", "loop", "fadeInMs", "pitch", "pan"}),
        makeSchema("audio.se", CommandDomain::Runtime, true, {"source", "volume", "pitch", "pan"}),
        makeSchema("audio.footstep", CommandDomain::Runtime, true, {"surfaceId", "volume"}),
        makeSchema("audio.voice", CommandDomain::Runtime, true, {"source", "volume", "pitch", "pan"}),
        makeSchema("voice.play", CommandDomain::Runtime, true, {"speakerId", "lineId", "source", "localizationKey", "volume"}),
        makeSchema("voice.stop", CommandDomain::Runtime),
        makeSchema("voice.waitForEnd", CommandDomain::Runtime, true, {"wait"}),
        makeSchema("portrait.show", CommandDomain::Runtime, true, {"speakerId", "expression", "source", "position"}),
        makeSchema("portrait.hide", CommandDomain::Runtime, true, {"speakerId"}),
        makeSchema("portrait.setExpression", CommandDomain::Runtime, true, {"speakerId", "expression"}),
        makeSchema("bubble.show", CommandDomain::Runtime, true, {"target", "text", "localizationKey", "duration", "speaker", "speakerLocalizationKey", "maxWidth", "bgColor", "textColor", "fontSize", "tailDirection", "offsetX", "offsetY", "typewriter", "charsPerSecond"}),
        makeSchema("bubble.hide", CommandDomain::Runtime, true, {"target"}),
        makeSchema("bubble.hideAll", CommandDomain::Runtime),
        makeSchema("notification.show", CommandDomain::Runtime, true, {"text", "localizationKey", "duration", "position", "maxWidth", "bgColor", "textColor", "fontSize", "typewriter", "charsPerSecond"}),
        makeSchema("notification.hide", CommandDomain::Runtime),
        makeSchema("audio.stop", CommandDomain::Runtime, true, {"channel", "fadeOutMs"}),
        withExecutionRoute(makeSchema("ludo.command", CommandDomain::Runtime, true, {"text"}), CommandExecutionRoute::Interpreter),
        makeSchema("ludo.camera.move", CommandDomain::Runtime, true,
                   {"target", "x", "y", "zoom", "duration", "ease", "wait", "follow",
                    "followSpeed", "deadzone", "disableSmoothing"}),
        makeSchema("ludo.camera.zoomOnly", CommandDomain::Runtime, true,
                   {"zoom", "duration", "ease", "wait", "disableSmoothing"}),
        makeSchema("ludo.camera.moveOnly", CommandDomain::Runtime, true,
                   {"target", "x", "y", "duration", "ease", "wait", "follow",
                    "followSpeed", "deadzone", "disableSmoothing"}),
        makeSchema("ludo.camera.reset", CommandDomain::Runtime, true, {"duration", "ease", "wait"}),
        makeSchema("ludo.camera.save", CommandDomain::Runtime, true),
        makeSchema("ludo.camera.restore", CommandDomain::Runtime, true, {"duration", "ease", "wait"}),
        makeSchema("ludo.camera.release", CommandDomain::Runtime, true),
        makeSchema("ludo.sprite.fade", CommandDomain::Runtime, true, {"target", "opacity", "duration", "wait"}),
        makeSchema("ludo.sprite.offset", CommandDomain::Runtime, true, {"target", "x", "y", "duration", "wait"}),
        makeSchema("ludo.sprite.clearOffset", CommandDomain::Runtime, true, {"target", "duration", "wait"}),
        makeSchema("ludo.sprite.zoom", CommandDomain::Runtime, true, {"target", "scaleX", "scaleY", "duration", "wait"}),
        makeSchema("ludo.sprite.resetZoom", CommandDomain::Runtime, true, {"target", "duration", "wait"}),
        makeSchema("ludo.sprite.shake", CommandDomain::Runtime, true, {"target", "x", "y", "frequency", "duration", "wait"}),
        makeSchema("ludo.sprite.phantom", CommandDomain::Runtime, true, {"target", "mode", "near", "distance", "minimum", "maximum", "smoothness"}),
        makeSchema("ludo.sprite.clear", CommandDomain::Runtime, true, {"target"}),
        withExecutionRoute(makeSchema("ludo.cutscene.begin", CommandDomain::Runtime, true), CommandExecutionRoute::Interpreter),
        withExecutionRoute(makeSchema("ludo.cutscene.end", CommandDomain::Runtime, true), CommandExecutionRoute::Interpreter),
        withExecutionRoute(makeSchema("ludo.cutscene.settings", CommandDomain::Runtime, true,
                   {"skipAllowed", "skipAction", "nesting"}), CommandExecutionRoute::Interpreter),
        makeSchema("ludo.cutscene.enable", CommandDomain::Runtime, true),
        makeSchema("ludo.cutscene.disable", CommandDomain::Runtime, true),
        makeSchema("ludo.screen.tone", CommandDomain::Runtime, true, {"red", "green", "blue", "gray", "duration", "wait"}),
        makeSchema("ludo.screen.clearTone", CommandDomain::Runtime, true, {"duration", "wait"}),
        makeSchema("ludo.screen.flash", CommandDomain::Runtime, true, {"red", "green", "blue", "alpha", "duration", "wait"}),
        makeSchema("ludo.screen.fade", CommandDomain::Runtime, true, {"direction", "red", "green", "blue", "alpha", "duration", "wait"}),
        makeSchema("ludo.screen.shake", CommandDomain::Runtime, true, {"x", "y", "frequency", "duration", "wait"}),
        makeSchema("ludo.screen.clearEffects", CommandDomain::Runtime, true),
        makeSchema("ludo.filter.chromaticAberration", CommandDomain::Runtime, true,
                   {"slot", "mode", "intensity", "edgeStart", "falloff", "mix", "affectWorld", "affectPictures", "affectHud", "duration", "wait"}),
        makeSchema("ludo.filter.noise", CommandDomain::Runtime, true,
                   {"slot", "intensity", "size", "speed", "affectWorld", "affectPictures", "affectHud", "duration", "wait"}),
        makeSchema("ludo.filter.scanlines", CommandDomain::Runtime, true,
                   {"slot", "intensity", "spacing", "style", "thickness", "softness", "phase", "scrollSpeed", "interlaceAmount", "interlaceSpeed", "whiteSweep", "sweepSpeed", "sweepWidth", "sweepSoftness", "sweepIntensity", "sweepRed", "sweepGreen", "sweepBlue", "sweepDelay", "affectWorld", "affectPictures", "affectHud", "duration", "wait"}),
        makeSchema("ludo.filter.vignette", CommandDomain::Runtime, true,
                   {"slot", "intensity", "radius", "softness", "affectWorld", "affectPictures", "affectHud", "duration", "wait"}),
        makeSchema("ludo.filter.blur", CommandDomain::Runtime, true,
                   {"slot", "radius", "direction", "style", "quality", "strength", "edgePreservation", "angle", "affectWorld", "affectPictures", "affectHud", "duration", "wait"}),
        makeSchema("ludo.filter.tiltShift", CommandDomain::Runtime, true,
                   {"slot", "blur", "centerY", "focusWidth", "falloff", "affectWorld", "affectPictures", "affectHud", "duration", "wait"}),
        makeSchema("ludo.filter.clear", CommandDomain::Runtime, true, {"filter", "slot", "allSlots", "duration", "wait"}),
        // Comandos removidos permanecem reconhecidos para diagnóstico/migração,
        // mas não podem ser introduzidos por novos plugins.
        withExecutionRoute(withCatalogPolicy(makeSchema("ludo.screen.set", CommandDomain::Runtime, false), CommandCatalogPolicy::Legacy), CommandExecutionRoute::LegacyNoOp),
        withExecutionRoute(withCatalogPolicy(makeSchema("ludo.screen.clear", CommandDomain::Runtime, false), CommandCatalogPolicy::Legacy), CommandExecutionRoute::LegacyNoOp),

        withCatalogPolicy(makeSchema("plugin.call", CommandDomain::Plugin, false, {"pluginId", "commandId", "arguments"}), CommandCatalogPolicy::Dynamic)
    };
    return table;
}

const CommandSchema* findExact(const QString& type)
{
    // Lookup criado uma vez; `schemaTable()` é estático e nunca é mutado,
    // portanto os ponteiros permanecem válidos durante toda a execução.
    static const QHash<QString, const CommandSchema*> byType = [] {
        QHash<QString, const CommandSchema*> map;
        const auto& table = schemaTable();
        map.reserve(table.size());
        for (const CommandSchema& schema : table) map.insert(schema.type, &schema);
        return map;
    }();
    return byType.value(type, nullptr);
}

} // namespace

CommandDescriptor CommandRegistry::describe(const QString& type)
{
    CommandDescriptor out;
    out.skipDuringCutscene = cutsceneBlocking(type);

    if (const CommandSchema* schema = findExact(type)) {
        out.domain = schema->domain;
        out.sceneBoundary = schema->sceneBoundary;
        out.skipDuringCutscene = schema->skipDuringCutscene;
        out.registered = true;
        out.pluginSafe = schema->pluginSafe;
        out.stateMutation = schema->stateMutation;
        out.awaitable = schema->awaitable;
        out.pageActivation = schema->pageActivation;
        out.catalogPolicy = schema->catalogPolicy;
        out.executionRoute = schema->executionRoute;
        return out;
    }

    // Compatibilidade defensiva: um tipo futuro com prefixo conhecido ainda
    // percorre o mesmo domínio do runtime, mas `isKnown()` continua false e o
    // Validator/Interpreter o reportam. Assim não quebramos projetos futuros
    // ao mesmo tempo em que typos deixam de ser silenciosos.
    if (type.startsWith(QLatin1String("database."))) { out.domain = CommandDomain::Flow; out.executionRoute = CommandExecutionRoute::Interpreter; }
    else if (type.startsWith(QLatin1String("move.route"))) { out.domain = CommandDomain::Movement; out.executionRoute = CommandExecutionRoute::Interpreter; }
    else if (type.startsWith(QLatin1String("fog."))) { out.domain = CommandDomain::Fog; out.executionRoute = CommandExecutionRoute::Interpreter; }
    else if (type.startsWith(QLatin1String("picture."))) { out.domain = CommandDomain::Picture; out.executionRoute = CommandExecutionRoute::Interpreter; }
    else if (startsWithAny(type, {"ludo.", "map.", "game.", "battle.", "party.",
                                  "inventory.", "actor.", "shop.", "weather.",
                                  "quest.", "input.", "audio.", "localization."})) {
        out.domain = CommandDomain::Runtime;
        out.executionRoute = CommandExecutionRoute::GameSession;
        out.sceneBoundary = startsWithAny(type, {"map.", "game.", "input.", "shop.", "battle."});
        out.stateMutation = stateMutationCommand(type);
        out.awaitable = awaitableCommand(type);
    }
    return out;
}

QString CommandRegistry::contractId()
{
    return QStringLiteral("LudoEventCommandContract/%1").arg(ContractVersion);
}

QStringList CommandRegistry::contractIssues()
{
    QStringList issues;
    QSet<QString> seenTypes;
    for (const CommandSchema& schema : schemaTable()) {
        const QString type = schema.type.trimmed();
        if (type.isEmpty()) issues.push_back(QStringLiteral("schema.type.empty"));
        if (seenTypes.contains(type)) issues.push_back(QStringLiteral("schema.type.duplicate:%1").arg(type));
        seenTypes.insert(type);
        if (schema.domain == CommandDomain::Unknown)
            issues.push_back(QStringLiteral("schema.domain.unknown:%1").arg(type));
        if (schema.domain == CommandDomain::Plugin && schema.pluginSafe)
            issues.push_back(QStringLiteral("schema.plugin.recursive:%1").arg(type));
        if (schema.executionRoute == CommandExecutionRoute::Unknown)
            issues.push_back(QStringLiteral("schema.route.unknown:%1").arg(type));
        if (schema.domain == CommandDomain::Plugin && schema.executionRoute != CommandExecutionRoute::PluginExpansion)
            issues.push_back(QStringLiteral("schema.route.plugin:%1:%2").arg(type, commandExecutionRouteId(schema.executionRoute)));
        if (schema.executionRoute == CommandExecutionRoute::PluginExpansion && schema.domain != CommandDomain::Plugin)
            issues.push_back(QStringLiteral("schema.route.plugin-domain:%1").arg(type));
        if (schema.executionRoute == CommandExecutionRoute::GameSession && schema.domain != CommandDomain::Runtime)
            issues.push_back(QStringLiteral("schema.route.session-domain:%1").arg(type));
        if (schema.executionRoute == CommandExecutionRoute::LegacyNoOp &&
            (schema.catalogPolicy != CommandCatalogPolicy::Legacy || schema.pluginSafe))
            issues.push_back(QStringLiteral("schema.route.legacy:%1").arg(type));
        QSet<QString> keys;
        for (const QString& key : schema.parameterKeys) {
            if (key.trimmed().isEmpty()) issues.push_back(QStringLiteral("schema.param.empty:%1").arg(type));
            if (keys.contains(key)) issues.push_back(QStringLiteral("schema.param.duplicate:%1:%2").arg(type,key));
            keys.insert(key);
        }
    }
    issues.removeDuplicates();
    return issues;
}

QVector<CommandSchema> CommandRegistry::schemas()
{
    return schemaTable();
}

QStringList CommandRegistry::registeredTypes()
{
    QStringList result;
    result.reserve(schemaTable().size());
    for (const CommandSchema& schema : schemaTable()) result.push_back(schema.type);
    return result;
}

} // namespace core
