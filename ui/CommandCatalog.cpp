#include "CommandCatalog.h"
#include "core/Editor.h"
#include "core/CommandRegistry.h"
#include "core/NoCodePlugin.h"

#include <QObject>
#include <QHash>
#include <QSet>

namespace ui {
namespace {
CommandCatalogEntry entry(std::initializer_list<const char*> path,
                          const char* label, const char* type,
                          bool separatorBefore = false)
{
    QStringList translatedPath;
    for (const char* part : path) translatedPath.push_back(QObject::tr(part));
    return {translatedPath, QObject::tr(label), QString::fromLatin1(type), separatorBefore};
}
} // namespace

QVector<CommandCatalogEntry> builtInCommandCatalog()
{
    return {
        entry({"Diálogo"}, "Mostrar mensagem…", "message"),
        entry({"Diálogo"}, "Mostrar opções ao jogador…", "choice.show"),
        entry({"Diálogo", "Texto"}, "Acelerar revelação do texto…", "dialogue.fastForward"),
        entry({"Diálogo", "Texto"}, "Mostrar texto instantaneamente…", "dialogue.skipMode"),
        entry({"Diálogo", "Balões"}, "Mostrar balão de fala…", "bubble.show"),
        entry({"Diálogo", "Balões"}, "Ocultar balão de fala…", "bubble.hide"),
        entry({"Diálogo", "Balões"}, "Ocultar todos os balões", "bubble.hideAll"),
        entry({"Diálogo", "Notificações"}, "Mostrar notificação…", "notification.show"),
        entry({"Diálogo", "Notificações"}, "Ocultar notificação", "notification.hide"),
        entry({"Interação"}, "Pedir um número ao jogador…", "input.number"),
        entry({"Interação"}, "Pedir um texto ao jogador…", "input.text"),
        entry({"Interação"}, "Pedir confirmação…", "input.confirm"),
        entry({"Interação"}, "Pedir escolha de item…", "input.item"),
        entry({"Interação"}, "Esperar botão ou ação…", "input.wait"),
        entry({"Diálogo", "Legendas"}, "Configurar legendas…", "subtitle.configure"),
        entry({"Diálogo", "Legendas"}, "Adicionar legenda à fila…", "subtitle.enqueue"),
        entry({"Diálogo", "Legendas"}, "Limpar fila de um canal…", "subtitle.clearQueue"),
        entry({"Diálogo", "Legendas"}, "Ocultar todas as legendas", "subtitle.clear"),
        entry({"Diálogo", "Legendas"}, "Ocultar legendas suavemente", "subtitle.clearFade"),
        entry({"Diálogo", "Legendas"}, "Esperar as legendas terminarem", "subtitle.wait"),
        entry({"Texto"}, "Adicionar comentário…", "comment", true),

        entry({"Lógica", "Globais"}, "Alterar interruptor…", "switch.set"),
        entry({"Lógica"}, "Alterar interruptor deste evento…", "selfSwitch.set"),
        entry({"Lógica", "Globais"}, "Alterar variável…", "variable.set"),
        entry({"Lógica", "Globais"}, "Alterar texto global…", "string.set"),
        entry({"Lógica", "Cálculo"}, "Calcular valor da variável…", "variable.math"),
        entry({"Lógica", "Dados"}, "Consultar valor do jogo…", "value.get"),
        entry({"Lógica", "Banco de Dados"}, "Ler campo…", "database.get"),
        entry({"Lógica", "Banco de Dados"}, "Alterar campo durante o jogo…", "database.set"),
        entry({"Lógica", "Banco de Dados"}, "Procurar registro…", "database.find"),
        entry({"Lógica", "Banco de Dados"}, "Contar registros…", "database.count"),
        entry({"Lógica", "Banco de Dados"}, "Registro existe?…", "database.exists"),
        entry({"Lógica", "Banco de Dados"}, "Copiar dados entre registros…", "database.copy"),
        entry({"Lógica", "Banco de Dados"}, "Restaurar registro…", "database.reset"),
        entry({"Lógica", "Banco de Dados"}, "Consultar dados do registro…", "database.recordInfo"),
        entry({"Fluxo", "Banco de Dados"}, "Para cada registro…", "database.each"),
        entry({"Mapa", "Alterações durante o jogo"}, "Alterar Tile…", "map.runtime.tile"),
        entry({"Mapa", "Alterações durante o jogo"}, "Preencher Área…", "map.runtime.fill"),
        entry({"Mapa", "Alterações durante o jogo"}, "Copiar Área…", "map.runtime.copy"),
        entry({"Mapa", "Alterações durante o jogo"}, "Alterar Passagem…", "map.runtime.passage"),
        entry({"Mapa", "Alterações durante o jogo"}, "Alterar terreno/tag…", "map.runtime.terrain"),
        entry({"Mapa", "Alterações durante o jogo"}, "Remapear Tileset…", "map.runtime.tileset"),
        entry({"Mapa", "Alterações durante o jogo"}, "Desfazer alterações…", "map.runtime.reset"),

        entry({"Fluxo"}, "Se…", "if"),
        entry({"Fluxo"}, "Senão", "else"),
        entry({"Fluxo"}, "Fim da condição", "endIf"),
        entry({"Fluxo"}, "Repetir continuamente", "loop.begin"),
        entry({"Fluxo"}, "Repetir N vezes…", "repeat.begin"),
        entry({"Fluxo"}, "Parar repetição", "loop.break"),
        entry({"Fluxo"}, "Parar repetição", "repeat.break"),
        entry({"Fluxo"}, "Rótulo…", "label"),
        entry({"Fluxo"}, "Pular para rótulo…", "jump"),
        entry({"Fluxo"}, "Esperar…", "wait"),
        entry({"Fluxo"}, "Esperar até…", "wait.until"),
        entry({"Fluxo"}, "Executar em paralelo", "parallel.begin"),
        entry({"Fluxo"}, "Chamar evento do mapa…", "map.event.call"),
        entry({"Fluxo"}, "Chamar evento comum…", "common.call"),
        entry({"Fluxo"}, "Agendar evento comum…", "common.reserve"),
        entry({"Fluxo", "Evento Comum"}, "Mudar parâmetro/local…", "common.local.set"),
        entry({"Fluxo", "Evento Comum"}, "Retornar do Evento Comum…", "common.return"),
        entry({"Fluxo"}, "Encerrar ou retornar…", "flow.exit"),

        entry({"Mapa e jogador", "Movimento"}, "Criar rota de movimento…", "move.route"),
        entry({"Mapa e jogador", "Movimento"}, "Controlar rota em andamento…", "move.route.control"),

        entry({"Mapa e jogador"}, "Mover jogador para outro mapa…", "map.transfer"),
        entry({"Jogo"}, "Salvar partida…", "game.save"),
        entry({"Jogo"}, "Carregar partida…", "game.load"),
        entry({"Jogo"}, "Reiniciar partida", "game.restart"),
        entry({"Jogo", "Salvamento"}, "Criar Checkpoint", "game.checkpoint"),
        entry({"Jogo", "Salvamento"}, "Restaurar último Checkpoint", "game.restoreCheckpoint"),
        entry({"Jogo", "Salvamento"}, "Salvar automaticamente agora", "game.autosave"),
        entry({"Localização"}, "Alterar idioma…", "localization.set"),
        entry({"Jogo"}, "Iniciar batalha…", "battle.start"),
        entry({"Jogo"}, "Tela de Game Over", "game.gameOver"),
        entry({"Jogo"}, "Voltar à tela de título", "game.returnTitle"),
        entry({"Jogo"}, "Abrir loja…", "shop.open"),
        entry({"Jogo"}, "Abrir pousada…", "shop.inn"),
        entry({"Jogo"}, "Abrir tela da interface…", "game.ui.open"),
        entry({"Tela", "Tonalidade"}, "Aplicar tonalidade…", "ludo.screen.tone"),
        entry({"Tela", "Tonalidade"}, "Remover tonalidade…", "ludo.screen.clearTone"),
        entry({"Tela", "Efeitos"}, "Piscar a tela…", "ludo.screen.flash"),
        entry({"Tela", "Efeitos"}, "Escurecer / revelar a tela…", "ludo.screen.fade"),
        entry({"Tela", "Efeitos"}, "Parar efeitos de tela", "ludo.screen.clearEffects"),
        entry({"Tela", "Filtros"}, "Aberração cromática…", "ludo.filter.chromaticAberration"),
        entry({"Tela", "Filtros"}, "Ruído e granulação…", "ludo.filter.noise"),
        entry({"Tela", "Filtros"}, "Linhas de tela…", "ludo.filter.scanlines"),
        entry({"Tela", "Filtros"}, "Vinheta…", "ludo.filter.vignette"),
        entry({"Tela", "Filtros"}, "Desfoque…", "ludo.filter.blur"),
        entry({"Tela", "Filtros"}, "Desfoque de foco (Tilt-Shift)…", "ludo.filter.tiltShift"),
        entry({"Tela", "Filtros"}, "Remover filtro…", "ludo.filter.clear"),
        entry({"Tela", "Clima"}, "Alterar clima…", "weather.set"),

        entry({"Jogo", "Missões"}, "Iniciar missão…", "quest.start"),
        entry({"Jogo", "Missões"}, "Atualizar progresso…", "quest.progress"),
        entry({"Jogo", "Missões"}, "Concluir missão…", "quest.complete"),
        entry({"Jogo", "Missões"}, "Falhar missão…", "quest.fail"),

        entry({"Jogo", "Grupo e inventário"}, "Adicionar/remover personagem…", "party.change"),
        entry({"Jogo", "Grupo e inventário"}, "Alterar ouro…", "party.gold"),
        entry({"Jogo", "Grupo e inventário"}, "Alterar item…", "inventory.change"),
        entry({"Jogo", "Grupo e inventário"}, "Alterar HP…", "actor.hp"),
        entry({"Jogo", "Grupo e inventário"}, "Alterar MP…", "actor.mp"),
        entry({"Jogo", "Grupo e inventário"}, "Alterar experiência…", "actor.exp"),
        entry({"Jogo", "Grupo e inventário"}, "Alterar nível…", "actor.level"),
        entry({"Jogo", "Grupo e inventário"}, "Aplicar/remover estado…", "actor.state"),
        entry({"Jogo", "Grupo e inventário"}, "Trocar equipamento…", "actor.equip"),

        entry({"Áudio"}, "Tocar música de fundo (BGM)…", "audio.bgm"),
        entry({"Áudio"}, "Tocar som ambiente (BGS)…", "audio.bgs"),
        entry({"Áudio"}, "Tocar música curta (ME)…", "audio.me"),
        entry({"Áudio"}, "Tocar efeito sonoro (SE)…", "audio.se"),
        entry({"Áudio"}, "Tocar som de passo…", "audio.footstep"),
        entry({"Áudio"}, "Tocar arquivo de voz…", "audio.voice"),
        entry({"Diálogo", "Voz"}, "Tocar fala de personagem…", "voice.play"),
        entry({"Diálogo", "Voz"}, "Parar falas em andamento", "voice.stop"),
        entry({"Diálogo", "Voz"}, "Esperar a fala terminar", "voice.waitForEnd"),
        entry({"Diálogo", "Retratos"}, "Mostrar retrato do personagem…", "portrait.show"),
        entry({"Diálogo", "Retratos"}, "Ocultar retrato…", "portrait.hide"),
        entry({"Diálogo", "Retratos"}, "Mudar expressão do retrato…", "portrait.setExpression"),
        entry({"Áudio"}, "Parar áudio…", "audio.stop"),

        entry({"Câmera"}, "Mover câmera suavemente…", "ludo.camera.move"),
        entry({"Câmera"}, "Alterar zoom da câmera…", "ludo.camera.zoomOnly"),
        entry({"Câmera"}, "Mover câmera…", "ludo.camera.moveOnly"),
        entry({"Câmera"}, "Tremer tela…", "ludo.screen.shake"),
        entry({"Câmera"}, "Voltar ao jogador…", "ludo.camera.reset"),
        entry({"Câmera"}, "Guardar posição da câmera", "ludo.camera.save"),
        entry({"Câmera"}, "Voltar à posição guardada…", "ludo.camera.restore"),
        entry({"Câmera"}, "Liberar câmera", "ludo.camera.release"),

        entry({"Movimento", "Sprite Offset Shake"}, "Fade do sprite…", "ludo.sprite.fade"),
        entry({"Movimento", "Sprite Offset Shake"}, "Offset do sprite…", "ludo.sprite.offset"),
        entry({"Movimento", "Sprite Offset Shake"}, "Limpar offset do sprite…", "ludo.sprite.clearOffset"),
        entry({"Movimento", "Sprite Offset Shake"}, "Zoom do sprite…", "ludo.sprite.zoom"),
        entry({"Movimento", "Sprite Offset Shake"}, "Restaurar zoom do sprite…", "ludo.sprite.resetZoom"),
        entry({"Movimento", "Sprite Offset Shake"}, "Tremer sprite…", "ludo.sprite.shake"),
        entry({"Movimento", "Sprite Offset Shake"}, "Evento fantasma…", "ludo.sprite.phantom"),
        entry({"Movimento", "Sprite Offset Shake"}, "Limpar efeitos do sprite", "ludo.sprite.clear"),

        entry({"Jogo", "Pulador de Cena"}, "Início da cutscene pulável", "ludo.cutscene.begin"),
        entry({"Jogo", "Pulador de Cena"}, "Fim da região pulável", "ludo.cutscene.end"),
        entry({"Jogo", "Pulador de Cena"}, "Configurar próxima cutscene…", "ludo.cutscene.settings"),
        entry({"Jogo", "Pulador de Cena"}, "Ativar nesta cena", "ludo.cutscene.enable", true),
        entry({"Jogo", "Pulador de Cena"}, "Desativar nesta cena", "ludo.cutscene.disable"),
        entry({"Tela", "Névoa"}, "Mostrar névoa…", "fog.show"),
        entry({"Tela", "Névoa"}, "Ativar slot padrão…", "fog.enable"),
        entry({"Tela", "Névoa"}, "Desativar slot…", "fog.disable"),
        entry({"Tela", "Névoa"}, "Remover névoa…", "fog.remove"),
        entry({"Tela", "Névoa"}, "Alterar opacidade…", "fog.opacity"),
        entry({"Tela", "Névoa"}, "Alterar mistura…", "fog.blend"),
        entry({"Tela", "Névoa"}, "Mover névoa…", "fog.scroll"),
        entry({"Tela", "Névoa"}, "Fazer névoa aparecer…", "fog.fadeIn"),
        entry({"Tela", "Névoa"}, "Fazer névoa desaparecer…", "fog.fadeOut"),
        entry({"Tela", "Névoa"}, "Limpar todas", "fog.clear"),

        entry({"Imagem"}, "Mostrar imagem…", "picture.show"),
        entry({"Imagem", "Nomes e grupos"}, "Mostrar por nome lógico…", "picture.showByName"),
        entry({"Imagem", "Nomes e grupos"}, "Definir grupo…", "picture.setGroup"),
        entry({"Imagem", "Nomes e grupos"}, "Mover grupo…", "picture.moveGroup"),
        entry({"Imagem", "Nomes e grupos"}, "Apagar grupo…", "picture.eraseGroup"),
        entry({"Imagem", "Posição e vínculo"}, "Fazer imagem seguir um alvo…", "picture.attach"),
        entry({"Imagem", "Posição e vínculo"}, "Parar de seguir o alvo…", "picture.detach"),
        entry({"Imagem", "Animação"}, "Criar animação por quadros…", "picture.timeline.define"),
        entry({"Imagem", "Animação"}, "Reproduzir animação…", "picture.timeline.play"),
        entry({"Imagem", "Animação"}, "Parar animação…", "picture.timeline.stop"),
        entry({"Imagem", "Interação"}, "Ao clicar…", "picture.onClick"),
        entry({"Imagem", "Interação"}, "Ao tocar…", "picture.onTouch"),
        entry({"Imagem"}, "Mostrar texto como imagem…", "picture.text"),
        entry({"Imagem"}, "Mover / animar imagem…", "picture.move"),
        entry({"Imagem"}, "Aumentar imagem…", "picture.zoomIn"),
        entry({"Imagem"}, "Diminuir imagem…", "picture.zoomOut"),
        entry({"Imagem"}, "Mover automaticamente…", "picture.physics"),
        entry({"Imagem"}, "Alterar ponto de origem…", "picture.anchor"),
        entry({"Imagem"}, "Esperar a animação…", "picture.wait"),
        entry({"Imagem", "Efeitos"}, "Editar efeitos…", "picture.effects"),
        entry({"Imagem", "Efeitos"}, "Negativo / Inverter cores…", "picture.negative"),
        entry({"Imagem", "Efeitos"}, "Virar imagem…", "picture.flip"),
        entry({"Imagem"}, "Configurações de exibição…", "picture.display"),
        entry({"Imagem", "Efeitos"}, "Limpar efeitos…", "picture.clearEffects"),
        entry({"Imagem"}, "Ocultar imagem com transição…", "picture.transitionOut"),
        entry({"Imagem"}, "Apagar imagem…", "picture.erase"),
        entry({"Imagem"}, "Apagar todas as imagens", "picture.eraseAll"),

        entry({"Comando Ludo"}, "Comando Ludo…", "ludo.command", true)
    };
}


QVector<CommandCatalogEntry> commandCatalogForEditor(const core::Editor& editor)
{
    QVector<CommandCatalogEntry> entries = builtInCommandCatalog();
    for (const core::NoCodePlugin& plugin : editor.plugins) {
        if (!plugin.enabled) continue;
        for (const core::PluginCommand& command : plugin.commands) {
            if(!command.showInCatalog) continue;
            const core::PluginInvocationResolution resolution = core::resolvePluginInvocation(
                editor.plugins, plugin.id, command.id, {});
            if(!resolution.ready()) continue;
            CommandCatalogEntry entry;
            entry.path = {QObject::tr("Extensões visuais"), plugin.name};
            if (!command.category.trimmed().isEmpty() && command.category != QLatin1String("Plugins"))
                entry.path.push_back(command.category.trimmed());
            entry.label = command.name;
            entry.type = QStringLiteral("plugin.call:%1:%2").arg(plugin.id, command.id);
            entry.iconPath=command.iconPath;entry.shortcutKey=command.shortcutKey;
            entry.tags=command.tags;entry.tags.push_back(plugin.name);
            entries.push_back(entry);
        }
    }
    return entries;
}

QStringList commandCatalogTopLevelCategories(const QVector<CommandCatalogEntry>& entries)
{
    QStringList categories;
    for (const CommandCatalogEntry& entry : entries) {
        if (entry.path.isEmpty()) continue;
        const QString category = entry.path.first().trimmed();
        if (!category.isEmpty() && !categories.contains(category, Qt::CaseInsensitive)) categories << category;
    }
    categories.sort(Qt::CaseInsensitive);
    return categories;
}

QStringList commandCatalogContractIssues()
{
    QStringList issues;
    QHash<QString,int> visibleCounts;
    for (const CommandCatalogEntry& entry : builtInCommandCatalog()) {
        visibleCounts[entry.type] += 1;
        if (!core::CommandRegistry::isKnown(entry.type))
            issues.push_back(QStringLiteral("catalog.unknown:%1").arg(entry.type));
        else if (core::CommandRegistry::catalogPolicy(entry.type) != core::CommandCatalogPolicy::Visible)
            issues.push_back(QStringLiteral("catalog.policy:%1:%2").arg(
                entry.type, core::commandCatalogPolicyId(core::CommandRegistry::catalogPolicy(entry.type))));
    }
    for (const core::CommandSchema& schema : core::CommandRegistry::schemas()) {
        const int count = visibleCounts.value(schema.type);
        if (schema.catalogPolicy == core::CommandCatalogPolicy::Visible && count == 0)
            issues.push_back(QStringLiteral("catalog.missing:%1").arg(schema.type));
        if (schema.catalogPolicy == core::CommandCatalogPolicy::Visible && count > 1)
            issues.push_back(QStringLiteral("catalog.duplicate:%1").arg(schema.type));
        if (schema.catalogPolicy != core::CommandCatalogPolicy::Visible && count > 0)
            issues.push_back(QStringLiteral("catalog.hidden-visible:%1").arg(schema.type));
    }
    issues.removeDuplicates();
    return issues;
}

} // namespace ui
