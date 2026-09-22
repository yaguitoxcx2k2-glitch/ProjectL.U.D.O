#include "GameSession.h"

#include "core/CommandRegistry.h"
#include "game/BattleTypes.h"
#include "game/RpgSystem.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

using namespace core;

namespace game {

bool GameSession::applyStateMutationCommand(const EventCommand& command, QString* error)
{
    const QVariantMap& p = command.params;
    QString localError;
    QString* outError = error ? error : &localError;
    outError->clear();

    if (command.type.startsWith(QLatin1String("quest."))) {
        const QString questId = p.value(QStringLiteral("questId")).toString();
        if (!databaseRecord(ed, QStringLiteral("quests"), questId)) {
            *outError = QCoreApplication::translate("GameSession", "A missão escolhida não existe.");
            return false;
        }
        if (command.type == QLatin1String("quest.start"))
            m_state.startQuest(questId, p.value(QStringLiteral("target"), 1).toInt());
        else if (command.type == QLatin1String("quest.progress")) {
            if (!m_state.hasQuest(questId))
                m_state.startQuest(questId, p.value(QStringLiteral("target"), 1).toInt());
            if (p.value(QStringLiteral("operation"), QStringLiteral("add")).toString() == QLatin1String("set"))
                m_state.setQuestProgress(questId, p.value(QStringLiteral("amount"), 1).toInt());
            else
                m_state.addQuestProgress(questId, p.value(QStringLiteral("amount"), 1).toInt());
        } else if (command.type == QLatin1String("quest.complete"))
            m_state.setQuestStatus(questId, QStringLiteral("completed"));
        else if (command.type == QLatin1String("quest.fail"))
            m_state.setQuestStatus(questId, QStringLiteral("failed"));
        else
            return false;
        m_hint = databaseRecordName(ed, QStringLiteral("quests"), questId);
        return true;
    }

    if (command.type == QLatin1String("party.change")) {
        const QString actorId = p.value(QStringLiteral("actorId")).toString();
        if (p.value(QStringLiteral("operation"), QStringLiteral("add")).toString() == QLatin1String("remove")) {
            if (PartyMemberState* member = m_state.partyMember(actorId)) {
                if (!member->weaponId.isEmpty()) m_state.addItem(member->weaponId, 1);
                if (!member->armorId.isEmpty()) m_state.addItem(member->armorId, 1);
                if (!member->accessoryId.isEmpty()) m_state.addItem(member->accessoryId, 1);
            }
            return m_state.removeActor(actorId);
        }
        return m_state.addActor(ed, actorId, p.value(QStringLiteral("level"), -1).toInt());
    }

    if (command.type == QLatin1String("party.gold")) {
        const QString operation = p.value(QStringLiteral("operation"), QStringLiteral("add")).toString();
        const int amount = qMax(0, p.value(QStringLiteral("amount")).toInt());
        if (operation == QLatin1String("set")) m_state.setGold(amount);
        else m_state.addGold(operation == QLatin1String("subtract") ? -amount : amount);
        return true;
    }

    if (command.type == QLatin1String("inventory.change")) {
        const QString id = p.value(QStringLiteral("itemId")).toString();
        if (id.isEmpty()) return false;
        const QString operation = p.value(QStringLiteral("operation"), QStringLiteral("add")).toString();
        const int amount = qMax(0, p.value(QStringLiteral("amount"), 1).toInt());
        if (operation == QLatin1String("set")) m_state.setItemCount(id, amount);
        else m_state.addItem(id, operation == QLatin1String("subtract") ? -amount : amount);
        return true;
    }

    if (command.type == QLatin1String("actor.hp") ||
        command.type == QLatin1String("actor.mp") ||
        command.type == QLatin1String("actor.exp") ||
        command.type == QLatin1String("actor.level")) {
        const QString actorId = p.value(QStringLiteral("actorId")).toString();
        const QString operation = p.value(QStringLiteral("operation"), QStringLiteral("add")).toString();
        const int amount = qMax(0, p.value(QStringLiteral("amount"), 1).toInt());
        bool changed = false;
        for (PartyMemberState& member : m_state.party()) {
            if (!actorId.isEmpty() && member.actorId != actorId) continue;
            if (command.type == QLatin1String("actor.level")) {
                const int targetLevel = operation == QLatin1String("set")
                    ? amount : qMax(1, member.level + (operation == QLatin1String("subtract") ? -amount : amount));
                setExperience(ed, member, experienceForLevel(targetLevel));
            } else if (command.type == QLatin1String("actor.exp")) {
                const int value = operation == QLatin1String("subtract") ? -amount : amount;
                if (operation == QLatin1String("set")) setExperience(ed, member, amount);
                else if (value >= 0) gainExperience(ed, member, value);
                else setExperience(ed, member, member.experience + value);
            } else {
                const CombatStats stats = memberStats(ed, member);
                int& value = command.type == QLatin1String("actor.hp") ? member.hp : member.mp;
                const int maximum = command.type == QLatin1String("actor.hp") ? stats.maxHp : stats.maxMp;
                if (operation == QLatin1String("set")) value = qBound(0, amount, maximum);
                else value = qBound(0, value + (operation == QLatin1String("subtract") ? -amount : amount), maximum);
            }
            changed = true;
        }
        return changed;
    }

    if (command.type == QLatin1String("actor.state")) {
        const QString actorId = p.value(QStringLiteral("actorId")).toString();
        const QString stateId = p.value(QStringLiteral("stateId")).toString();
        const core::DatabaseRecord* state = databaseRecord(ed, QStringLiteral("states"), stateId);
        if (!state) {
            *outError = QCoreApplication::translate("GameSession", "O estado escolhido não existe.");
            return false;
        }
        bool changed = false;
        for (PartyMemberState& member : m_state.party()) {
            if (!actorId.isEmpty() && member.actorId != actorId) continue;
            if (p.value(QStringLiteral("operation"), QStringLiteral("add")).toString() == QLatin1String("remove")) {
                member.states.removeAll(stateId);
                member.stateTurns.remove(stateId);
            } else {
                if (!member.states.contains(stateId)) member.states.push_back(stateId);
                member.stateTurns[stateId] = qMax(0, state->data.value(QStringLiteral("duration"), 0).toInt());
            }
            changed = true;
        }
        return changed;
    }

    if (command.type == QLatin1String("actor.equip")) {
        PartyMemberState* member = m_state.partyMember(p.value(QStringLiteral("actorId")).toString());
        return member && equipItem(ed, m_state, *member,
                                   p.value(QStringLiteral("slot"), QStringLiteral("weapon")).toString(),
                                   p.value(QStringLiteral("itemId")).toString(), outError);
    }

    return false;
}

CommandResult GameSession::runRuntimeCommand(const EventCommand& command, bool start)
{
    if (!CommandRegistry::isRuntimeForwarded(command.type)) return CommandResult::Rejected;

    const bool waitRequested = CommandRegistry::isSceneBoundary(command.type) ||
                               command.params.value(QStringLiteral("wait"), false).toBool();

    if(command.type==QLatin1String("voice.waitForEnd"))
        return (m_voicePlaying&&m_voicePlaying())?CommandResult::Waiting:CommandResult::Completed;

    if (command.type.startsWith(QLatin1String("ludo."))) {
        const bool active = runLudoCommand(command, start);
        return active && (waitRequested || !start) ? CommandResult::Waiting
                                                   : CommandResult::Completed;
    }

    if (command.type == QLatin1String("input.wait")) {
        bool ok = false;
        const GameAction action = gameActionFromId(
            command.params.value(QStringLiteral("action")).toString(), &ok);
        if (!ok) return CommandResult::Failed;
        const bool waiting = !inputActionMatches(
            action, command.params.value(QStringLiteral("state"), QStringLiteral("pressed")).toString());
        return waiting ? CommandResult::Waiting : CommandResult::Completed;
    }

    // GameState lógico não passa pela fila de apresentação. A mesma autoridade
    // aplica Party/Inventory/Actor/Quest tanto no runtime normal quanto em
    // qualquer consumidor interno, garantindo read-after-write no mesmo tick.
    if (start && CommandRegistry::isStateMutation(command.type)) {
        QString error;
        const bool ok = applyStateMutationCommand(command, &error);
        if (!ok && !error.isEmpty()) {
            m_hint = error;
            m_hintUntil = m_clock.elapsed() + 3500;
        } else if (ok && command.type.startsWith(QLatin1String("quest."))) {
            m_hintUntil = m_clock.elapsed() + 1800;
        }
        m_overlayDirty = true;
        return ok ? CommandResult::Completed : CommandResult::Failed;
    }

    if (!start) {
        const qulonglong ticket = command.params.value(QStringLiteral("_runtimeTicket")).toULongLong();
        if (ticket != 0) {
            for (const EventCommand& pending : m_pendingRuntimeActions)
                if (pending.params.value(QStringLiteral("_runtimeTicket")).toULongLong() == ticket)
                    return CommandResult::Waiting;
        }
        // DeferredCommit usa a mesma consulta por ticket dos comandos que
        // esperam, mas não herda a semântica modal deles. Assim Abrir UI com
        // wait=false só aguarda o commit da abertura, não o fechamento da UI.
        if (!waitRequested) return CommandResult::Completed;
        if (!m_runtimeModalCommand.type.isEmpty() &&
            m_runtimeModalCommand.type == command.type &&
            m_runtimeModalCommand.params.value(QStringLiteral("_runtimeTicket")).toULongLong() == ticket &&
            m_uiModal.active())
            return CommandResult::Waiting;
        if (command.type == QLatin1String("battle.start")) {
            if (m_uiBattle.active()) return CommandResult::Waiting;
            if (m_uiBattle.finished()) finishBattleCommand(command);
            return CommandResult::Completed;
        }
        if (command.type == QLatin1String("shop.open"))
            return m_uiShop.active() ? CommandResult::Waiting : CommandResult::Completed;
        // Esta consulta só acontece quando o comando "Abrir tela" foi marcado
        // com params["wait"] = true. Nesse modo modal, o Interpreter aguarda
        // enquanto a tela permanecer aberta; no modo overlay o comando usa a
        // barreira DeferredCommit e continua após o estado ter sido aplicado.
        if (command.type == QLatin1String("game.ui.open"))
            return (m_uiMenu.customActive() || m_uiMenu.active())
                ? CommandResult::Waiting : CommandResult::Completed;
        if (command.type == QLatin1String("map.transfer"))
            return m_mapTransition.active ? CommandResult::Waiting : CommandResult::Completed;
        return CommandResult::Completed;
    }

    m_pendingRuntimeActions.push_back(command);
    return waitRequested ? CommandResult::Waiting : CommandResult::DeferredCommit;
}

bool GameSession::processPendingRuntimeActions()
{
    while (!m_pendingRuntimeActions.isEmpty()) {
        const EventCommand command = m_pendingRuntimeActions.dequeue();
        const QVariantMap& p = command.params;
        const auto localized=[this,&p](const QString& field,const QString& fallback){
            QString key;
            if (field == QLatin1String("text"))
                key = p.value(QStringLiteral("localizationKey"),
                              p.value(QStringLiteral("textLocalizationKey"))).toString();
            else
                key = p.value(field+QStringLiteral("LocalizationKey")).toString();
            return core::resolvePlayerText(ed.localization, key, fallback);
        };
        QString error;
        bool ok = false;
        bool changesMap = false;
        const int slot = qBound(1, p.value(QStringLiteral("slot"), 1).toInt(), 99);
        const int uiOpenMs = ed.gameUi.openAnimation == QLatin1String("none") ? 0 : ed.gameUi.animationMs;
        const int uiCloseMs = ed.gameUi.closeAnimation == QLatin1String("none") ? 0 : ed.gameUi.animationMs;
        if (command.type == QLatin1String("map.transfer")) {
            const int fadeFrames=qBound(0,p.value(QStringLiteral("fadeFrames"),18).toInt(),600);
            if(fadeFrames>0){m_mapTransition.active=true;m_mapTransition.transferred=false;m_mapTransition.elapsed=0;m_mapTransition.opacity=0;m_mapTransition.halfDuration=fadeFrames/60.0;m_mapTransition.command=command;m_overlayDirty=true;return true;}
            const int rawDirection = qBound(0, p.value(QStringLiteral("direction"), 0).toInt(),
                                            int(Dir::UpRight));
            ok = transferToMap(p.value(QStringLiteral("mapId")).toString(),
                               QPoint(p.value(QStringLiteral("x")).toInt(),
                                      p.value(QStringLiteral("y")).toInt()),
                               p.value(QStringLiteral("useSpawn"), false).toBool(),
                               static_cast<Dir>(rawDirection), &error);
            changesMap = ok;
        } else if(command.type==QLatin1String("game.restart")){
            ok = restartGame(&error);
            changesMap = ok;
        } else if(command.type==QLatin1String("game.gameOver")){
            m_gameOver=true;stopAllInterpreters(CancellationReason::GameOver);m_pendingRuntimeActions.clear();m_overlayDirty=true;return true;
        } else if(command.type==QLatin1String("game.returnTitle")){
            m_wantClose=true;stopAllInterpreters(CancellationReason::ReturnTitle);m_pendingRuntimeActions.clear();return true;
        } else if(command.type==QLatin1String("input.number")){
            const int variableId=p.value(QStringLiteral("variableId")).toInt();
            int minimum=p.value(QStringLiteral("minimum"),0).toInt();
            int maximum=p.value(QStringLiteral("maximum"),9999).toInt();
            if(minimum>maximum)std::swap(minimum,maximum);
            const int value=qBound(minimum,m_state.variable(variableId),maximum);
            if(variableId<=0) {
                error=QCoreApplication::translate("GameSession","A variável da entrada numérica é inválida.");
            } else {
                m_runtimeModalCommand = command;
                m_uiModal.openNumber(
                    localized(QStringLiteral("title"),p.value(QStringLiteral("title"),QCoreApplication::translate("GameSession","Digite um número")).toString()),
                    localized(QStringLiteral("prompt"),p.value(QStringLiteral("prompt"),QCoreApplication::translate("GameSession","Valor:")).toString()),
                    minimum, maximum, value, uiOpenMs, uiCloseMs);
                m_overlayDirty = true;
                return true;
            }
        } else if (command.type == QLatin1String("input.text")) {
            const int stringId = p.value(QStringLiteral("stringId")).toInt();
            if (stringId <= 0) {
                error = QCoreApplication::translate("GameSession", "A String que recebe o texto é inválida.");
            } else {
                m_runtimeModalCommand = command;
                m_uiModal.openText(
                    localized(QStringLiteral("title"), p.value(QStringLiteral("title"), QCoreApplication::translate("GameSession", "Digite um texto")).toString()),
                    localized(QStringLiteral("prompt"), p.value(QStringLiteral("prompt"), QCoreApplication::translate("GameSession", "Texto:")).toString()),
                    p.value(QStringLiteral("replace"), true).toBool() ? m_state.stringValue(stringId) : QString(),
                    qBound(1, p.value(QStringLiteral("maximumLength"), 32).toInt(), 1024),
                    p.value(QStringLiteral("allowCancel"), true).toBool(), uiOpenMs, uiCloseMs);
                m_overlayDirty = true;
                return true;
            }
        } else if (command.type == QLatin1String("input.confirm")) {
            const int variableId = p.value(QStringLiteral("resultVariable")).toInt();
            if (variableId <= 0) {
                error = QCoreApplication::translate("GameSession", "A variável que recebe a confirmação é inválida.");
            } else {
                m_runtimeModalCommand = command;
                m_uiModal.openConfirm(
                    localized(QStringLiteral("title"),p.value(QStringLiteral("title"), QCoreApplication::translate("GameSession", "Confirmar")).toString()),
                    localized(QStringLiteral("prompt"),p.value(QStringLiteral("prompt"), QCoreApplication::translate("GameSession", "Continuar?")).toString()),
                    p.value(QStringLiteral("defaultYes"), true).toBool(), uiOpenMs, uiCloseMs);
                m_overlayDirty = true;
                return true;
            }
        } else if (command.type == QLatin1String("input.item")) {
            const int variableId = p.value(QStringLiteral("variableId")).toInt();
            if (variableId <= 0) {
                error = QCoreApplication::translate("GameSession", "A variável que recebe o item é inválida.");
            } else {
                QStringList labels;
                QVector<int> values;
                QString filter = p.value(QStringLiteral("category"), QStringLiteral("items")).toString();
                if (filter != QLatin1String("items") && filter != QLatin1String("weapons") &&
                    filter != QLatin1String("armors")) filter = QStringLiteral("items");
                const QStringList categories{filter};
                for (const QString& category : categories) {
                    for (const DatabaseRecord& record : ed.database.value(category)) {
                        const int count = m_state.itemCount(record.id);
                        if (count <= 0) continue;
                        labels.push_back(QCoreApplication::translate("GameSession", "%1  ×%2")
                                             .arg(databaseRecordName(ed, category, record.id, record.name)).arg(count));
                        values.push_back(record.number);
                    }
                }
                if (labels.isEmpty()) {
                    m_state.setVariable(variableId, 0);
                    ok = true;
                } else {
                    m_runtimeModalCommand = command;
                    m_uiModal.openItems(
                        localized(QStringLiteral("title"),p.value(QStringLiteral("title"), QCoreApplication::translate("GameSession", "Selecionar item")).toString()),
                        localized(QStringLiteral("prompt"),p.value(QStringLiteral("prompt"), QCoreApplication::translate("GameSession", "Escolha um item:")).toString()),
                        labels, values, uiOpenMs, uiCloseMs);
                    m_overlayDirty = true;
                    return true;
                }
            }
        } else if (command.type == QLatin1String("game.ui.open")) {
            const QString screenId = p.value(QStringLiteral("screenId"), QStringLiteral("main")).toString();
            ok = m_uiMenu.openBuiltIn(screenId, true);
            if (!ok) error = QCoreApplication::translate("GameSession", "A janela de jogo solicitada não existe.");
        } else if (command.type == QLatin1String("game.checkpoint")) {
            ok = saveGame(qBound(1, ed.checkpointSlot, 99), &error);
        } else if (command.type == QLatin1String("game.restoreCheckpoint")) {
            ok = loadGame(qBound(1, ed.checkpointSlot, 99), &error); changesMap = ok;
        } else if (command.type == QLatin1String("game.autosave")) {
            ok = saveGame(qBound(1, ed.autosaveSlot, 99), &error);
        } else if (command.type == QLatin1String("game.save")) {
            ok = saveGame(slot, &error);
        } else if (command.type == QLatin1String("game.load")) {
            ok = loadGame(slot, &error);
            changesMap = ok;
        } else if (command.type == QLatin1String("battle.start")) {
            const QString troopId = p.value(QStringLiteral("troopId")).toString();
            const bool allowEscape = p.value(QStringLiteral("allowEscape"), true).toBool();
            m_randomEncounterBattle = false;
            if (m_battle) {
                const BattleResult result = static_cast<BattleResult>(m_battle(troopId, allowEscape));
                const int resultVariable = p.value(QStringLiteral("resultVariable"), 0).toInt();
                if (resultVariable > 0)
                    m_state.setVariable(resultVariable,
                        result == BattleResult::Victory ? 0 : result == BattleResult::Escaped ? 1 : 2);
                if (result == BattleResult::Defeat) {
                    m_gameOver = true;
                    stopAllInterpreters(CancellationReason::GameOver);
                    m_pendingRuntimeActions.clear();
                }
                ok = result != BattleResult::Aborted;
            } else if (m_uiBattle.open(troopId, allowEscape, &error)) {
                m_keys.clear();
                m_actions.clear();
                m_overlayDirty = true;
                return true;
            }
        } else if (command.type == QLatin1String("shop.open")) {
            QStringList itemIds = p.value(QStringLiteral("itemIds")).toStringList();
            if (itemIds.isEmpty())
                for (const QVariant& value : p.value(QStringLiteral("itemIds")).toList())
                    itemIds.push_back(value.toString());
            if (m_shop) { m_shop(itemIds, p.value(QStringLiteral("purchaseOnly"), false).toBool()); ok = true; }
            else { m_uiShop.open(itemIds, p.value(QStringLiteral("purchaseOnly"), false).toBool()); m_keys.clear(); m_actions.clear(); m_overlayDirty = true; return true; }
        } else if (command.type == QLatin1String("shop.inn")) {
            const int cost = qBound(0, p.value(QStringLiteral("cost"), 0).toInt(), 999999999);
            if (m_state.gold() < cost) {
                error = QCoreApplication::translate("GameSession", "Dinheiro insuficiente para descansar.");
            } else {
                m_runtimeModalCommand = command;
                m_uiModal.openConfirm(
                    localized(QStringLiteral("title"),p.value(QStringLiteral("title"), QCoreApplication::translate("GameSession", "Pousada")).toString()),
                    localized(QStringLiteral("prompt"),p.value(QStringLiteral("prompt"), QCoreApplication::translate("GameSession", "Descansar e recuperar todo o grupo por %1 G?").arg(cost)).toString()),
                    true, uiOpenMs, uiCloseMs);
                m_overlayDirty = true;
                return true;
            }
        } else if (command.type == QLatin1String("weather.set")) {
            const QString thunder = p.contains(QStringLiteral("thunderSe"))
                ? p.value(QStringLiteral("thunderSe")).toString() : m_weather.thunderSePath;
            const int thunderVolume = p.contains(QStringLiteral("thunderVolume"))
                ? p.value(QStringLiteral("thunderVolume"), 90).toInt() : m_weather.thunderVolume;
            m_weather.setConfig(p.value(QStringLiteral("type"), QStringLiteral("none")).toString(),
                                p.value(QStringLiteral("intensity"), 50).toInt(),
                                thunder, thunderVolume, true);
            m_lastThunderCycle = -1;
            m_overlayDirty = true;
            ok = true;
        } else if(command.type==QLatin1String("voice.play")){
            QString source=p.value(QStringLiteral("source")).toString().trimmed();const QString speakerId=p.value(QStringLiteral("speakerId")).toString().trimmed();const QString lineId=p.value(QStringLiteral("lineId")).toString().trimmed();const SpeakerProfile* speaker=ed.speakerDatabase.findById(speakerId);
            if(source.isEmpty()&&speaker){source=speaker->metadata.value(QStringLiteral("voiceLines")).toMap().value(lineId).toString();if(source.isEmpty())source=lineId;if(!speaker->voicePrefix.isEmpty()&&!source.isEmpty()&&!QFileInfo(source).isAbsolute())source=QDir(speaker->voicePrefix).filePath(source);}
            const QString localizationKey=p.value(QStringLiteral("localizationKey")).toString();if(!localizationKey.isEmpty())source=core::resolvePlayerText(ed.localization,localizationKey,source);
            if(source.isEmpty())error=QCoreApplication::translate("GameSession","A fala não possui arquivo de voz.");else if(m_voiceAudio){const QString path=QFileInfo(source).isAbsolute()?source:QDir(ed.projectRoot()).filePath(source);m_voiceAudio(path,qBound(0,p.value(QStringLiteral("volume"),90).toInt(),100));ok=true;}
        } else if(command.type==QLatin1String("voice.stop")){
            if(m_stopVoiceAudio)m_stopVoiceAudio();ok=true;
        } else if(command.type==QLatin1String("bubble.show")||command.type==QLatin1String("notification.show")){
            SpeechBubble bubble;bubble.notification=command.type==QLatin1String("notification.show");bubble.target=p.value(QStringLiteral("target"),QStringLiteral("player")).toString().trimmed();if(bubble.target==QLatin1String("self")){const QString eventId=p.value(QStringLiteral("_eventId")).toString();bubble.target=eventId.isEmpty()?QStringLiteral("player"):QStringLiteral("event:")+eventId;}bubble.text=localized(QStringLiteral("text"),p.value(QStringLiteral("text")).toString());bubble.speaker=localized(QStringLiteral("speaker"),p.value(QStringLiteral("speaker")).toString());bubble.duration=qBound(1,p.value(QStringLiteral("duration"),180).toInt(),36000)/60.0;bubble.maxWidth=qBound(80,p.value(QStringLiteral("maxWidth"),280).toInt(),1200);const QColor bg(p.value(QStringLiteral("bgColor"),QStringLiteral("#e60c101c")).toString());if(bg.isValid())bubble.bgColor=bg;const QColor fg(p.value(QStringLiteral("textColor"),QStringLiteral("#ffffffff")).toString());if(fg.isValid())bubble.textColor=fg;bubble.fontSize=qBound(8,p.value(QStringLiteral("fontSize"),18).toInt(),96);bubble.tailDirection=p.value(QStringLiteral("tailDirection"),QStringLiteral("down")).toString();bubble.offsetX=qBound(-2000,p.value(QStringLiteral("offsetX"),0).toInt(),2000);bubble.offsetY=qBound(-2000,p.value(QStringLiteral("offsetY"),-16).toInt(),2000);bubble.typewriter=p.value(QStringLiteral("typewriter"),false).toBool();bubble.charsPerSecond=qBound(1.0,p.value(QStringLiteral("charsPerSecond"),36.0).toDouble(),1000.0);bubble.screenPosition=p.value(QStringLiteral("position"),QStringLiteral("top-right")).toString();if(bubble.text.isEmpty())error=QCoreApplication::translate("GameSession","O texto do balão está vazio.");else{if(bubble.notification)m_bubbles.showNotification(bubble);else m_bubbles.show(bubble);ok=true;}
        } else if(command.type==QLatin1String("bubble.hide")){
            QString target=p.value(QStringLiteral("target"),QStringLiteral("player")).toString();if(target==QLatin1String("self")){const QString eventId=p.value(QStringLiteral("_eventId")).toString();target=eventId.isEmpty()?QStringLiteral("player"):QStringLiteral("event:")+eventId;}m_bubbles.hide(target);ok=true;
        } else if(command.type==QLatin1String("bubble.hideAll")){m_bubbles.hideAll();ok=true;
        } else if(command.type==QLatin1String("notification.hide")){m_bubbles.hideNotification();ok=true;
        } else if(command.type.startsWith(QLatin1String("portrait."))){
            const QString speakerId=p.value(QStringLiteral("speakerId")).toString().trimmed();SpeakerProfile* speaker=nullptr;for(SpeakerProfile& candidate:ed.speakerDatabase.speakers)if(candidate.id.compare(speakerId,Qt::CaseInsensitive)==0){speaker=&candidate;break;}
            if(!speaker)error=QCoreApplication::translate("GameSession","O perfil de personagem do retrato não existe.");else if(command.type==QLatin1String("portrait.hide")){speaker->metadata[QStringLiteral("portraitHidden")]=true;ok=true;}else{speaker->metadata[QStringLiteral("portraitHidden")]=false;const QString expression=p.value(QStringLiteral("expression")).toString().trimmed();if(!expression.isEmpty())speaker->defaultExpression=expression;const QString source=p.value(QStringLiteral("source")).toString().trimmed();if(!source.isEmpty()){if(expression.isEmpty())speaker->portrait=source;else speaker->expressionPortraits[expression]=source;}const QString position=p.value(QStringLiteral("position")).toString().toLower();if(position=="left"||position=="right")speaker->portraitPosition=position;ok=true;}
        } else if (CommandRegistry::isStateMutation(command.type)) {
            // Compatibilidade defensiva: a fila normal já não recebe estas
            // mutações, mas um comando pendente de uma revisão/hot-reload usa
            // exatamente a mesma autoridade síncrona, sem duplicar regras.
            ok = applyStateMutationCommand(command, &error);
        } else if (command.type == QLatin1String("audio.footstep")) {
            const QPoint half = m_world.playerHalfCell();
            playFootstep(QString(), QPointF(half.x()/2.0, half.y()/2.0),
                         p.value(QStringLiteral("surfaceId")).toString(),
                         qBound(0,p.value(QStringLiteral("volume"),100).toInt(),100));
            ok = true;
        } else if (command.type.startsWith(QLatin1String("audio."))) {
            const QString channel = command.type.mid(6);
            if (channel == QLatin1String("stop")) {
                const QString stopped=p.value(QStringLiteral("channel"), QStringLiteral("bgm")).toString();
                m_audioChannels.remove(stopped);
                if (m_stopChannelAudio) m_stopChannelAudio(stopped, qBound(0, p.value(QStringLiteral("fadeOutMs"), 0).toInt(), 60000));
            } else if (m_channelAudio) {
                const QString source = p.value(QStringLiteral("source")).toString();
                const QString path = QFileInfo(source).isAbsolute()
                    ? source : QDir(ed.projectRoot()).filePath(source);
                const int volume=qBound(0,p.value(QStringLiteral("volume"),90).toInt(),100);
                const bool loop=p.value(QStringLiteral("loop"),channel==QLatin1String("bgm")||channel==QLatin1String("bgs")).toBool();
                const int fadeInMs = qBound(0, p.value(QStringLiteral("fadeInMs"), 0).toInt(), 60000);
                const int transitionMs = qBound(0, p.value(QStringLiteral("transitionMs"), 0).toInt(), 60000);
                const int pitch=qBound(50,p.value(QStringLiteral("pitch"),100).toInt(),200);
                const int pan=qBound(-100,p.value(QStringLiteral("pan"),0).toInt(),100);
                m_audioChannels[channel]=AudioChannelState{source,volume,loop,pitch,pan};
                m_channelAudio(channel,path,volume,loop,fadeInMs,transitionMs,pitch,pan);
            }
            ok = true;
        }

        if (command.type == QLatin1String("game.save") && ok)
            m_hint = QCoreApplication::translate("GameSession", "Partida salva no slot %1.").arg(slot);
        else if (command.type == QLatin1String("game.load") && ok)
            m_hint = QCoreApplication::translate("GameSession", "Partida carregada do slot %1.").arg(slot);
        else if (!ok && !error.isEmpty())
            m_hint = error;
        m_hintUntil = m_clock.elapsed() + (ok ? 1800 : 3500);
        m_overlayDirty = true;

        if (changesMap) {
            m_pendingRuntimeActions.clear();
            return true;
        }
    }
    return false;
}


void GameSession::finishBattleCommand(const EventCommand& command)
{
    const BattleResult result = m_uiBattle.takeResult();
    const int resultVariable = command.params.value(QStringLiteral("resultVariable"), 0).toInt();
    if (resultVariable > 0)
        m_state.setVariable(resultVariable,
            result == BattleResult::Victory ? 0 : result == BattleResult::Escaped ? 1 : 2);
    if (result == BattleResult::Defeat) {
        m_gameOver = true;
        stopAllInterpreters(CancellationReason::GameOver);
        m_pendingRuntimeActions.clear();
    }
    m_overlayDirty = true;
}


void GameSession::finishUiModal()
{
    if (m_runtimeModalCommand.type.isEmpty()) {
        m_uiModal.clear();
        return;
    }
    const EventCommand command = m_runtimeModalCommand;
    const QVariantMap p = command.params;
    const bool accepted = m_uiModal.accepted();
    const int selectedValue = m_uiModal.selectedValue();

    if (command.type == QLatin1String("input.number")) {
        const int variableId = p.value(QStringLiteral("variableId")).toInt();
        if (accepted && variableId > 0) m_state.setVariable(variableId, m_uiModal.numberValue());
    } else if (command.type == QLatin1String("input.text")) {
        const int stringId = p.value(QStringLiteral("stringId")).toInt();
        if (accepted && stringId > 0) m_state.setStringValue(stringId, m_uiModal.textValue().left(65535));
    } else if (command.type == QLatin1String("input.confirm")) {
        const int variableId = p.value(QStringLiteral("resultVariable")).toInt();
        if (variableId > 0) {
            const int cancelValue = p.value(QStringLiteral("cancelValue"), 0).toInt();
            m_state.setVariable(variableId, accepted ? selectedValue : cancelValue);
        }
    } else if (command.type == QLatin1String("input.item")) {
        const int variableId = p.value(QStringLiteral("variableId")).toInt();
        if (variableId > 0) m_state.setVariable(variableId, accepted ? selectedValue : 0);
    } else if (command.type == QLatin1String("shop.inn")) {
        if (accepted && selectedValue == 1) {
            const int cost = qBound(0, p.value(QStringLiteral("cost"), 0).toInt(), 999999999);
            if (m_state.gold() >= cost) {
                m_state.addGold(-cost);
                for (PartyMemberState& member : m_state.party()) {
                    const CombatStats stats = memberStats(ed, member);
                    member.hp = stats.maxHp;
                    member.mp = stats.maxMp;
                    if (p.value(QStringLiteral("removeStates"), true).toBool()) {
                        member.states.clear();
                        member.stateTurns.clear();
                    }
                }
            }
        }
    }

    m_uiModal.clear();
    m_runtimeModalCommand = EventCommand();
    m_overlayDirty = true;
}


} // namespace game
