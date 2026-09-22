#include "GameValueRegistry.h"
#include "InputMap.h"

#include <QObject>
#include <QSet>

namespace core {

const QVector<GameValueDescriptor>& gameValueDescriptors()
{
    static const QVector<GameValueDescriptor> values = {
        {QStringLiteral("player.x"), QObject::tr("Jogador · X (tile)"), CommonValueType::Number},
        {QStringLiteral("player.y"), QObject::tr("Jogador · Y (tile)"), CommonValueType::Number},
        {QStringLiteral("player.xPrecise"), QObject::tr("Jogador · X preciso (meio tile)"), CommonValueType::Number},
        {QStringLiteral("player.yPrecise"), QObject::tr("Jogador · Y preciso (meio tile)"), CommonValueType::Number},
        {QStringLiteral("player.screenX"), QObject::tr("Jogador · X na tela"), CommonValueType::Number},
        {QStringLiteral("player.screenY"), QObject::tr("Jogador · Y na tela"), CommonValueType::Number},
        {QStringLiteral("player.direction"), QObject::tr("Jogador · Direção"), CommonValueType::Number},
        {QStringLiteral("player.moving"), QObject::tr("Jogador · Está movendo?"), CommonValueType::Boolean},
        {QStringLiteral("player.steps"), QObject::tr("Jogador · Passos"), CommonValueType::Number},
        {QStringLiteral("player.opacity"), QObject::tr("Jogador · Opacidade"), CommonValueType::Number},
        {QStringLiteral("player.speed"), QObject::tr("Jogador · Velocidade (tiles/s)"), CommonValueType::Number},
        {QStringLiteral("player.transparent"), QObject::tr("Jogador · Transparente?"), CommonValueType::Boolean},
        {QStringLiteral("player.blend"), QObject::tr("Jogador · Blend"), CommonValueType::Text},
        {QStringLiteral("player.animFrame"), QObject::tr("Jogador · Frame da animação"), CommonValueType::Number},
        {QStringLiteral("player.stepProgress"), QObject::tr("Jogador · Progresso do passo (0..1000)"), CommonValueType::Number},
        {QStringLiteral("player.mapId"), QObject::tr("Jogador · ID do mapa"), CommonValueType::Text},
        {QStringLiteral("player.footstepSurface"), QObject::tr("Jogador · Superfície de passo"), CommonValueType::Text},

        {QStringLiteral("event.x"), QObject::tr("Evento · X"), CommonValueType::Number, true},
        {QStringLiteral("event.y"), QObject::tr("Evento · Y"), CommonValueType::Number, true},
        {QStringLiteral("event.page"), QObject::tr("Evento · Página ativa"), CommonValueType::Number, true},
        {QStringLiteral("event.direction"), QObject::tr("Evento · Direção"), CommonValueType::Number, true},
        {QStringLiteral("event.priority"), QObject::tr("Evento · Prioridade"), CommonValueType::Number, true},
        {QStringLiteral("event.trigger"), QObject::tr("Evento · Gatilho da página"), CommonValueType::Number, true},
        {QStringLiteral("event.distance"), QObject::tr("Evento · Distância do jogador (tiles)"), CommonValueType::Number, true},
        {QStringLiteral("event.opacity"), QObject::tr("Evento · Opacidade"), CommonValueType::Number, true},
        {QStringLiteral("event.screenX"), QObject::tr("Evento · X na tela"), CommonValueType::Number, true},
        {QStringLiteral("event.screenY"), QObject::tr("Evento · Y na tela"), CommonValueType::Number, true},
        {QStringLiteral("event.onScreen"), QObject::tr("Evento · Está na tela?"), CommonValueType::Boolean, true},
        {QStringLiteral("event.moving"), QObject::tr("Evento · Está movendo?"), CommonValueType::Boolean, true},
        {QStringLiteral("event.transparent"), QObject::tr("Evento · Transparente?"), CommonValueType::Boolean, true},
        {QStringLiteral("event.through"), QObject::tr("Evento · Atravessável?"), CommonValueType::Boolean, true},
        {QStringLiteral("event.blocksPlayer"), QObject::tr("Evento · Bloqueia jogador?"), CommonValueType::Boolean, true},
        {QStringLiteral("event.blend"), QObject::tr("Evento · Blend"), CommonValueType::Text, true},
        {QStringLiteral("event.footstepSurface"), QObject::tr("Evento · Superfície de passo"), CommonValueType::Text, true},

        {QStringLiteral("map.id"), QObject::tr("Mapa · ID"), CommonValueType::Text},
        {QStringLiteral("map.name"), QObject::tr("Mapa · Nome"), CommonValueType::Text},
        {QStringLiteral("map.width"), QObject::tr("Mapa · Largura"), CommonValueType::Number},
        {QStringLiteral("map.height"), QObject::tr("Mapa · Altura"), CommonValueType::Number},
        {QStringLiteral("map.cameraX"), QObject::tr("Mapa · Câmera X (pixels)"), CommonValueType::Number},
        {QStringLiteral("map.cameraY"), QObject::tr("Mapa · Câmera Y (pixels)"), CommonValueType::Number},
        {QStringLiteral("map.zoom"), QObject::tr("Mapa · Zoom da câmera (%)"), CommonValueType::Number},
        {QStringLiteral("map.eventCount"), QObject::tr("Mapa · Quantidade de eventos"), CommonValueType::Number},
        {QStringLiteral("map.passable"), QObject::tr("Mapa · Posição passável?"), CommonValueType::Boolean, false, false, true},
        {QStringLiteral("map.passageMask"), QObject::tr("Mapa · Máscara de passagem em X/Y"), CommonValueType::Number, false, false, true},
        {QStringLiteral("map.terrain"), QObject::tr("Mapa · Terrain/Tag em X/Y"), CommonValueType::Number, false, false, true},
        {QStringLiteral("map.runtimeChanged"), QObject::tr("Mapa · X/Y foi alterado durante o jogo?"), CommonValueType::Boolean, false, false, true},
        {QStringLiteral("map.tileTilesetId"), QObject::tr("Mapa · Tileset do tile superior em X/Y"), CommonValueType::Text, false, false, true},
        {QStringLiteral("map.tileLayerId"), QObject::tr("Mapa · Camada do tile superior em X/Y"), CommonValueType::Text, false, false, true},
        {QStringLiteral("map.tileX"), QObject::tr("Mapa · Tile X do tile superior em X/Y"), CommonValueType::Number, false, false, true},
        {QStringLiteral("map.tileY"), QObject::tr("Mapa · Tile Y do tile superior em X/Y"), CommonValueType::Number, false, false, true},
        {QStringLiteral("map.runtimeOverrideCount"), QObject::tr("Mapa · Alterações feitas durante o jogo"), CommonValueType::Number},
        {QStringLiteral("map.eventAt"), QObject::tr("Mapa · ID do evento em X/Y"), CommonValueType::Text, false, false, true},

        {QStringLiteral("picture.exists"), QObject::tr("Picture · Existe?"), CommonValueType::Boolean, false, true},
        {QStringLiteral("picture.busy"), QObject::tr("Picture · Ocupada/animando?"), CommonValueType::Boolean, false, true},
        {QStringLiteral("picture.x"), QObject::tr("Picture · X"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.y"), QObject::tr("Picture · Y"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.scaleX"), QObject::tr("Picture · Escala X (%)"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.scaleY"), QObject::tr("Picture · Escala Y (%)"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.angle"), QObject::tr("Picture · Ângulo"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.opacity"), QObject::tr("Picture · Opacidade"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.frame"), QObject::tr("Picture · Frame atual"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.frameCount"), QObject::tr("Picture · Quantidade de frames"), CommonValueType::Number, false, true},
        {QStringLiteral("picture.framePlaying"), QObject::tr("Picture · Sequência tocando?"), CommonValueType::Boolean, false, true},
        {QStringLiteral("picture.flipH"), QObject::tr("Picture · Espelhada horizontalmente?"), CommonValueType::Boolean, false, true},
        {QStringLiteral("picture.flipV"), QObject::tr("Picture · Espelhada verticalmente?"), CommonValueType::Boolean, false, true},
        {QStringLiteral("picture.layer"), QObject::tr("Picture · Layer"), CommonValueType::Text, false, true},
        {QStringLiteral("picture.blend"), QObject::tr("Picture · Blend"), CommonValueType::Text, false, true},

        {QStringLiteral("party.gold"), QObject::tr("Grupo · Ouro"), CommonValueType::Number},
        {QStringLiteral("party.size"), QObject::tr("Grupo · Quantidade de membros"), CommonValueType::Number},
        {QStringLiteral("party.hasItem"), QObject::tr("Grupo · Possui item?"), CommonValueType::Boolean},
        {QStringLiteral("actor.hp"), QObject::tr("Ator · HP atual"), CommonValueType::Number},
        {QStringLiteral("actor.mp"), QObject::tr("Ator · MP atual"), CommonValueType::Number},
        {QStringLiteral("timer.value"), QObject::tr("Timer · Valor"), CommonValueType::Number},

        {QStringLiteral("audio.bgm.source"), QObject::tr("Áudio · BGM arquivo"), CommonValueType::Text},
        {QStringLiteral("audio.bgm.volume"), QObject::tr("Áudio · BGM volume"), CommonValueType::Number},
        {QStringLiteral("audio.bgm.pitch"), QObject::tr("Áudio · BGM pitch"), CommonValueType::Number},
        {QStringLiteral("audio.bgm.pan"), QObject::tr("Áudio · BGM pan"), CommonValueType::Number},
        {QStringLiteral("audio.bgm.playing"), QObject::tr("Áudio · BGM tocando?"), CommonValueType::Boolean},
        {QStringLiteral("audio.bgs.source"), QObject::tr("Áudio · BGS arquivo"), CommonValueType::Text},
        {QStringLiteral("audio.bgs.volume"), QObject::tr("Áudio · BGS volume"), CommonValueType::Number},
        {QStringLiteral("audio.bgs.pitch"), QObject::tr("Áudio · BGS pitch"), CommonValueType::Number},
        {QStringLiteral("audio.bgs.pan"), QObject::tr("Áudio · BGS pan"), CommonValueType::Number},
        {QStringLiteral("audio.bgs.playing"), QObject::tr("Áudio · BGS tocando?"), CommonValueType::Boolean},

        {QStringLiteral("input.mouseX"), QObject::tr("Input · Mouse X"), CommonValueType::Number},
        {QStringLiteral("input.mouseY"), QObject::tr("Input · Mouse Y"), CommonValueType::Number},
        {QStringLiteral("input.mouseDeltaX"), QObject::tr("Input · Delta Mouse X"), CommonValueType::Number},
        {QStringLiteral("input.mouseDeltaY"), QObject::tr("Input · Delta Mouse Y"), CommonValueType::Number},
        {QStringLiteral("input.wheel"), QObject::tr("Input · Roda do mouse"), CommonValueType::Number},
        {QStringLiteral("input.device"), QObject::tr("Input · Último dispositivo"), CommonValueType::Text},
        {QStringLiteral("input.actionHeld"), QObject::tr("Input · Ação pressionada?"), CommonValueType::Boolean, false, false, false, true},
        {QStringLiteral("input.actionPressed"), QObject::tr("Input · Ação acabou de pressionar?"), CommonValueType::Boolean, false, false, false, true},
        {QStringLiteral("input.actionReleased"), QObject::tr("Input · Ação acabou de soltar?"), CommonValueType::Boolean, false, false, false, true},
        {QStringLiteral("input.actionHoldFrames"), QObject::tr("Input · Frames segurando ação"), CommonValueType::Number, false, false, false, true},
        {QStringLiteral("input.gamepadConnected"), QObject::tr("Input · Controle conectado?"), CommonValueType::Boolean},
        {QStringLiteral("input.axisX"), QObject::tr("Input · Analógico X (-1000..1000)"), CommonValueType::Number},
        {QStringLiteral("input.axisY"), QObject::tr("Input · Analógico Y (-1000..1000)"), CommonValueType::Number},
        {QStringLiteral("input.axisMagnitude"), QObject::tr("Input · Intensidade analógica (0..1000)"), CommonValueType::Number},
        {QStringLiteral("input.leftTrigger"), QObject::tr("Input · Gatilho esquerdo (0..1000)"), CommonValueType::Number},
        {QStringLiteral("input.rightTrigger"), QObject::tr("Input · Gatilho direito (0..1000)"), CommonValueType::Number},
        {QStringLiteral("input.mouseLeftHeld"), QObject::tr("Input · Mouse esquerdo pressionado?"), CommonValueType::Boolean},
        {QStringLiteral("input.mouseLeftPressed"), QObject::tr("Input · Mouse esquerdo acabou de pressionar?"), CommonValueType::Boolean},
        {QStringLiteral("input.mouseLeftReleased"), QObject::tr("Input · Mouse esquerdo acabou de soltar?"), CommonValueType::Boolean},
        {QStringLiteral("input.mouseRightHeld"), QObject::tr("Input · Mouse direito pressionado?"), CommonValueType::Boolean},
        {QStringLiteral("input.mouseRightPressed"), QObject::tr("Input · Mouse direito acabou de pressionar?"), CommonValueType::Boolean},
        {QStringLiteral("input.mouseRightReleased"), QObject::tr("Input · Mouse direito acabou de soltar?"), CommonValueType::Boolean},

        {QStringLiteral("system.fps"), QObject::tr("Sistema · FPS"), CommonValueType::Number},
        {QStringLiteral("system.width"), QObject::tr("Sistema · Largura lógica"), CommonValueType::Number},
        {QStringLiteral("system.height"), QObject::tr("Sistema · Altura lógica"), CommonValueType::Number},
        {QStringLiteral("system.playTime"), QObject::tr("Sistema · Tempo de jogo (s)"), CommonValueType::Number},
        {QStringLiteral("system.locale"), QObject::tr("Sistema · Idioma"), CommonValueType::Text},
        {QStringLiteral("system.version"), QObject::tr("Sistema · Versão da LUDO"), CommonValueType::Text},
        {QStringLiteral("system.platform"), QObject::tr("Sistema · Plataforma"), CommonValueType::Text},
    };
    return values;
}

const GameValueDescriptor* gameValueDescriptor(const QString& key)
{
    const auto& values = gameValueDescriptors();
    for (const GameValueDescriptor& value : values)
        if (value.key == key) return &value;
    return nullptr;
}

bool isKnownGameValue(const QString& key)
{
    return gameValueDescriptor(key) != nullptr;
}

CommonValueType gameValueType(const QString& key, CommonValueType fallback)
{
    if (const GameValueDescriptor* value = gameValueDescriptor(key)) return value->type;
    return fallback;
}

QString gameValueQueryProblem(const QVariantMap& query)
{
    const QString key = query.value(QStringLiteral("key")).toString();
    const GameValueDescriptor* descriptor = gameValueDescriptor(key);
    if (!descriptor)
        return QObject::tr("O Valor do Jogo selecionado é desconhecido.");
    if (descriptor->needsEvent) {
        const QString eventId = query.value(QStringLiteral("eventId")).toString();
        if (eventId.isEmpty())
            return QObject::tr("O Valor do Jogo requer um Evento (ou Este evento).");
    }
    if (descriptor->needsPicture && query.value(QStringLiteral("number")).toInt() <= 0)
        return QObject::tr("O Valor do Jogo requer um número de Picture válido.");
    if (descriptor->needsMapPosition &&
        (!query.contains(QStringLiteral("x")) || !query.contains(QStringLiteral("y"))))
        return QObject::tr("O Valor do Jogo requer as coordenadas X e Y do mapa.");
    if (descriptor->needsAction) {
        bool ok=false; gameActionFromId(query.value(QStringLiteral("action")).toString(), &ok);
        if (!ok) return QObject::tr("O Valor do Jogo requer uma Ação de Input válida.");
    }
    if ((key == QLatin1String("actor.hp") || key == QLatin1String("actor.mp")) &&
        query.value(QStringLiteral("actorId")).toString().isEmpty())
        return QObject::tr("O Valor do Jogo requer o ID de um Ator.");
    if (key == QLatin1String("party.hasItem") && query.value(QStringLiteral("itemId")).toString().isEmpty())
        return QObject::tr("O Valor do Jogo requer o ID de um Item.");
    if (key == QLatin1String("timer.value") && query.value(QStringLiteral("timerId")).toString().isEmpty())
        return QObject::tr("O Valor do Jogo requer o ID de um Timer.");
    return QString();
}

bool isKnownUniversalExpressionFunction(const QString& name)
{
    const QString key=name.toLower();
    static const QSet<QString> functions={QStringLiteral("v"),QStringLiteral("s"),QStringLiteral("switch"),QStringLiteral("gv"),QStringLiteral("actor.hp"),QStringLiteral("actor.mp"),QStringLiteral("party.size"),QStringLiteral("party.gold"),QStringLiteral("party.hasitem"),QStringLiteral("map.id"),QStringLiteral("map.width"),QStringLiteral("map.height"),QStringLiteral("input.pressing"),QStringLiteral("timer.value"),QStringLiteral("picture.x"),QStringLiteral("picture.y"),QStringLiteral("picture.opacity")};
    return functions.contains(key);
}

CommonValueType universalExpressionFunctionType(const QString& name)
{
    const QString key=name.toLower();
    if(key==QLatin1String("s")||key==QLatin1String("map.id"))return CommonValueType::Text;
    if(key==QLatin1String("switch")||key==QLatin1String("party.hasitem")||key==QLatin1String("input.pressing"))return CommonValueType::Boolean;
    return CommonValueType::Number;
}

QVariantMap gameValueQueryFromInline(const QString& reference, QString* error)
{
    const QString trimmed=reference.trimmed();
    const int separator=trimmed.indexOf(QLatin1Char(':'));
    QString key=separator<0?trimmed:trimmed.left(separator);
    const QString argument=separator<0?QString():trimmed.mid(separator+1);
    if(key.compare(QLatin1String("input.pressing"),Qt::CaseInsensitive)==0)key=QStringLiteral("input.actionHeld");
    QVariantMap query{{QStringLiteral("key"),key}};
    if(key.startsWith(QLatin1String("picture.")))query[QStringLiteral("number")]=argument.toInt();
    else if(key.startsWith(QLatin1String("actor.")))query[QStringLiteral("actorId")]=argument;
    else if(key==QLatin1String("party.hasItem"))query[QStringLiteral("itemId")]=argument;
    else if(key==QLatin1String("input.actionHeld"))query[QStringLiteral("action")]=argument;
    else if(key==QLatin1String("timer.value"))query[QStringLiteral("timerId")]=argument;
    if(!isKnownGameValue(key)){if(error)*error=QObject::tr("Game Value desconhecido em gv(): %1.").arg(key);return {};}
    const QString problem=gameValueQueryProblem(query);if(!problem.isEmpty()){if(error)*error=problem;return {};}
    return query;
}

QVariantMap gameValueQueryForExpressionFunction(const QString& name,const QVariantList& arguments,QString* error)
{
    const QString function=name.toLower();
    auto count=[&](int expected){if(arguments.size()==expected)return true;if(error)*error=QObject::tr("A função %1 espera %2 argumento(s).").arg(name).arg(expected);return false;};
    if(function==QLatin1String("gv")){if(!count(1))return {};return gameValueQueryFromInline(arguments.first().toString(),error);}
    if(function==QLatin1String("actor.hp")||function==QLatin1String("actor.mp")){if(!count(1))return {};return {{QStringLiteral("key"),function},{QStringLiteral("actorId"),arguments.first().toString()}};}
    if(function==QLatin1String("party.size")||function==QLatin1String("party.gold")||function==QLatin1String("map.id")||function==QLatin1String("map.width")||function==QLatin1String("map.height")){if(!count(0))return {};return {{QStringLiteral("key"),function}};}
    if(function==QLatin1String("party.hasitem")){if(!count(1))return {};return {{QStringLiteral("key"),QStringLiteral("party.hasItem")},{QStringLiteral("itemId"),arguments.first().toString()}};}
    if(function==QLatin1String("input.pressing")){if(!count(1))return {};return {{QStringLiteral("key"),QStringLiteral("input.actionHeld")},{QStringLiteral("action"),arguments.first().toString()}};}
    if(function==QLatin1String("timer.value")){if(!count(1))return {};return {{QStringLiteral("key"),QStringLiteral("timer.value")},{QStringLiteral("timerId"),arguments.first().toString()}};}
    if(function==QLatin1String("picture.x")||function==QLatin1String("picture.y")||function==QLatin1String("picture.opacity")){if(!count(1))return {};return {{QStringLiteral("key"),function},{QStringLiteral("number"),arguments.first().toInt()}};}
    return {};
}


const QVector<CommandValueFieldDescriptor>& commandValueFieldDescriptors()
{
    // Campos escalares já integrados ponta a ponta em Fluxo, Input, mapa,
    // Quest e RPG. A resolução em runtime é genérica, mas um campo só entra
    // aqui quando o editor também consegue criar e preservar ValueSpec.
    static const QVector<CommandValueFieldDescriptor> fields = {
        {QStringLiteral("wait"), QStringLiteral("frames"), CommonValueType::Number, 30},
        {QStringLiteral("wait.until"), QStringLiteral("timeoutFrames"), CommonValueType::Number, 0},
        {QStringLiteral("input.number"), QStringLiteral("minimum"), CommonValueType::Number, 0},
        {QStringLiteral("input.number"), QStringLiteral("maximum"), CommonValueType::Number, 9999},
        {QStringLiteral("input.text"), QStringLiteral("maximumLength"), CommonValueType::Number, 32},
        {QStringLiteral("map.transfer"), QStringLiteral("x"), CommonValueType::Number, 0},
        {QStringLiteral("map.transfer"), QStringLiteral("y"), CommonValueType::Number, 0},
        {QStringLiteral("map.transfer"), QStringLiteral("fadeFrames"), CommonValueType::Number, 18},
        {QStringLiteral("shop.inn"), QStringLiteral("cost"), CommonValueType::Number, 50},
        {QStringLiteral("quest.start"), QStringLiteral("target"), CommonValueType::Number, 1},
        {QStringLiteral("quest.progress"), QStringLiteral("amount"), CommonValueType::Number, 1},
        {QStringLiteral("party.change"), QStringLiteral("level"), CommonValueType::Number, 1},
        {QStringLiteral("party.gold"), QStringLiteral("amount"), CommonValueType::Number, 1},
        {QStringLiteral("inventory.change"), QStringLiteral("amount"), CommonValueType::Number, 1},
        {QStringLiteral("actor.hp"), QStringLiteral("amount"), CommonValueType::Number, 1},
        {QStringLiteral("actor.mp"), QStringLiteral("amount"), CommonValueType::Number, 1},
        {QStringLiteral("actor.exp"), QStringLiteral("amount"), CommonValueType::Number, 1},
        {QStringLiteral("actor.level"), QStringLiteral("amount"), CommonValueType::Number, 1},
        // Pictures/Fog/Audio permanecem fora desta primeira adoção de campos gerais:
        // seus diálogos ainda possuem controles especializados e entram no catálogo
        // somente quando puderem editar/preservar ValueSpec sem degradar o projeto.
    };
    return fields;
}

QVector<CommandValueFieldDescriptor> commandValueFields(const QString& commandType)
{
    QVector<CommandValueFieldDescriptor> out;
    for (const CommandValueFieldDescriptor& field : commandValueFieldDescriptors())
        if (field.commandType == commandType) out.push_back(field);
    return out;
}

const CommandValueFieldDescriptor* commandValueFieldDescriptor(const QString& commandType,
                                                                const QString& parameter)
{
    const auto& fields = commandValueFieldDescriptors();
    for (const CommandValueFieldDescriptor& field : fields)
        if (field.commandType == commandType && field.parameter == parameter) return &field;
    return nullptr;
}

bool isValueSpec(const QVariant& value)
{
    const QVariantMap spec = value.toMap();
    return !spec.isEmpty() && spec.contains(QStringLiteral("source"));
}

QVariantMap valueSpecFromLegacy(const QVariant& value, const QVariant& fallback)
{
    if (isValueSpec(value)) return value.toMap();
    return {{QStringLiteral("source"), QStringLiteral("constant")},
            {QStringLiteral("value"), value.isValid() ? value : fallback}};
}

} // namespace core
