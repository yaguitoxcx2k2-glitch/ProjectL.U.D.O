//=============================================================================
// LudoCameraSystem.js — LUDO ENGINE
//=============================================================================
/*:
 * @target MZ
 * @plugindesc [v4.4.5] Sistema completo de câmera LUDO: foco, zoom, arrasto, presets, limites e seguimento avançado.
 * @author LUDO ENGINE
 * @help
 * LudoCameraSystem v4.4.5 - Guia de Ajuda
 *
 * PARA QUE SERVE
 * Controla alvo, zoom, deslocamento e seguimento da camera.
 * Tambem salva presets, limites e estado da camera.
 *
 * PRIMEIROS PASSOS (SEM PROGRAMAR)
 * 1. Ative o plugin.
 * 2. Use ControlarCamera para focar jogador ou evento.
 * 3. Salve um preset se quiser reutilizar a configuracao.
 *
 * ANTES DE COMECAR
 * Pode funcionar sem LudoCore.
 * Opcional: LudoCore para integracao por capability.
 * Opcional: LudoDiagonalMove para look-ahead.
 *
 * ONDE ESCREVER CADA COISA
 * A) Codigos de texto (barra invertida)
 * Use somente em campos de texto renderizados pelo plugin.
 *
 * B) Tags de Nota do Banco de Dados
 * Este plugin NAO usa tags de Nota do Banco de Dados.
 *
 * C) Tags de Nota do Mapa
 * Este plugin NAO usa tags de Nota do Mapa.
 *
 * D) Expressao de bloqueio
 * Este plugin NAO usa expressao de bloqueio.
 *
 * E) Script (JavaScript)
 * Use em Evento > Script ou em integracoes de plugins.
 *   LudoCameraSystem.focusPlayer(30)
 *
 * F) Notas de Evento / Comentarios de Evento
 * Le a Nota do Evento apenas na busca textual de alvo.
 * Nao define tag propria de Nota de Evento.
 * Este plugin NAO le Comentarios de Evento.
 *
 * CODIGOS DE TEXTO (RICH TEXT)
 * barra invertida + cm[alvo,tempo,zoom,x,y] controla camera.
 *
 * TAGS DE NOTA DO BANCO DE DADOS
 * Este plugin NAO le tags de Nota do Banco de Dados.
 *
 * TAGS DE NOTA DO MAPA
 * Este plugin NAO le tags de Nota do Mapa.
 *
 * SCRIPT / API PUBLICA
 * Camera:
 *   LudoCameraSystem.focusPlayer(30)
 *   LudoCameraSystem.focusEvent(3, 30)
 *   LudoCameraSystem.setZoom(2, 30)
 *
 * EXPRESSAO DE BLOQUEIO / CAMPO SCRIPT
 * Este plugin NAO possui campo de expressao de bloqueio.
 *
 * COMANDOS DE PLUGIN
 * Camera:
 * - ControlarCamera
 * - IgnorarZoom
 * - IniciarArrasto
 * - EncerrarArrasto
 * - RestaurarCameraInicial
 *
 * Estado e presets:
 * - SalvarEstadoCamera
 * - RestaurarEstadoCamera
 * - SalvarPreset
 * - RestaurarPreset
 * - ExcluirPreset
 *
 * Seguimento e limites:
 * - ConfigurarSeguimento
 * - DefinirLimites
 * - LimparLimites
 *
 * COMO CONVERSA COM O RESTO DO LUDO
 * Capabilities registradas por este plugin:
 * - camera
 * Integracoes opcionais sao usadas apenas se presentes.
 *
 * VALE A PENA SABER
 * A busca textual de alvo pode consultar nome e Nota do
 * evento.
 * Isso nao cria uma tag de configuracao de Nota.
 *
 * SE ALGO NAO FUNCIONAR
 * Deixe o LudoCore acima dos outros plugins LUDO.
 * - Confira as dependencias obrigatorias deste plugin.
 * - Revise os nomes, ids e campos usados no comando.
 * - Teste em um projeto sem plugins conflitantes.
 *
 * QUER SE APROFUNDAR?
 * Consulte a documentacao completa do pacote LUDO.
 * Veja tambem a API e os contratos do proprio codigo.
 * @param _ludoGroup_general
 * @text Geral
 * @desc Agrupa configuracoes de geral no Plugin Manager.
 * 
 * @param zoomPadrao
 * @text Zoom Padrão
 * @parent _ludoGroup_general
 * @desc 1.00 = tamanho normal.
 * @type number
 * @decimals 2
 * @min 0.10
 * @default 1.00
 *
 * @param fixarPictures
 * @text Fixar Pictures na Tela
 * @parent _ludoGroup_general
 * @desc Mantém Pictures sem acompanhar o zoom da câmera.
 * @type boolean
 * @default true
 *
 * @param restaurarAoTrocarMapa
 * @text Restaurar ao Trocar de Mapa
 * @parent _ludoGroup_general
 * @desc Restaura a câmera para o Player ao trocar de mapa.
 * @type boolean
 * @default true
 *
 * @param codigoMensagem
 * @text Código de Controle da Câmera
 * @parent _ludoGroup_general
 * @desc Prefixo usado em mensagens. Ex.: cm para \\cm[...].
 * @default cm
 *
 * @param _ludoGroup_rendering
 * @text Renderizacao
 * @desc Agrupa configuracoes de renderizacao no Plugin Manager.
 * 
 * @param evitarSuavizacaoSprite
 * @text Evitar Suavização do Sprite
 * @parent _ludoGroup_rendering
 * @desc Usa filtragem nearest nos sprites de personagens durante zoom.
 * @type boolean
 * @default true
 *
 * @param evitarSuavizacaoParallax
 * @text Remover blur do parallax
 * @parent _ludoGroup_rendering
 * @desc Usa pixels nítidos no parallax e nas camadas LUDO ao ampliar. Não afeta Pictures ou personagens.
 * @type boolean
 * @on Ativado
 * @off Desativado
 * @default false
 *
 * @param removerJitterEventos
 * @text Remover Jitter dos Eventos
 * @parent _ludoGroup_rendering
 * @desc Corrige a diferença de arredondamento entre Eventos e câmera em movimentos lentos. Não altera movimento ou colisão.
 * @type boolean
 * @default true
 *
 * @param grupoSeguimento
 * @text ===== SEGUIMENTO AVANÇADO =====
 *
 * @param seguimentoAvancado
 * @text Seguimento Avançado Padrão
 * @parent grupoSeguimento
 * @type boolean
 * @default false
 *
 * @param suavizacaoFrames
 * @text Suavização Padrão (frames)
 * @parent grupoSeguimento
 * @type number
 * @min 0
 * @default 0
 *
 * @param lookAheadTiles
 * @text Look-Ahead Padrão (tiles)
 * @parent grupoSeguimento
 * @type number
 * @decimals 2
 * @min 0
 * @default 0
 *
 * @param deadZonePixels
 * @text Dead Zone Padrão (px)
 * @parent grupoSeguimento
 * @type number
 * @min 0
 * @default 0
 *
 * @param grupoArrasto
 * @text ===== MODO DE ARRASTAR =====
 *
 * @param teclaArrasto
 * @text Tecla para Arrastar
 * @parent grupoArrasto
 * @type select
 * @option Nenhuma
 * @value none
 * @option Confirmar / OK
 * @value ok
 * @option Cancelar
 * @value cancel
 * @option PageUp
 * @value pageup
 * @option PageDown
 * @value pagedown
 * @option Shift
 * @value shift
 * @option Ctrl
 * @value control
 * @default shift
 *
 * @param teclaZoom
 * @text Tecla para Zoom
 * @parent grupoArrasto
 * @type select
 * @option Nenhuma
 * @value none
 * @option Confirmar / OK
 * @value ok
 * @option Cancelar
 * @value cancel
 * @option PageUp
 * @value pageup
 * @option PageDown
 * @value pagedown
 * @option Shift
 * @value shift
 * @option Ctrl
 * @value control
 * @default control
 *
 * @param _ludoGroup_debug
 * @text Depuracao
 * @desc Agrupa configuracoes de depuracao no Plugin Manager.
 * 
 * @param debug
 * @text Debug da Câmera
 * @parent _ludoGroup_debug
 * @type boolean
 * @default false
 *
 * @command ControlarCamera
 * @text ★ Controlar Câmera
 * @desc Move o foco, zoom e deslocamento da câmera.
 *
 * @arg executarAoIniciar
 * @text Executar ao Iniciar?
 * @type boolean
 * @default false
 *
 * @arg alvoNome
 * @text Alvo por Nome
 * @desc Nome ou trecho do nome do Evento. Deixe vazio para usar ID.
 *
 * @arg alvoId
 * @text Alvo por ID
 * @desc 1+=Evento; 0=Evento atual; -1=Player; -11=Seguidor 1; -101=Barco.
 * @default -1
 *
 * @arg usarCoordenada
 * @text Usar Coordenada do Mapa?
 * @type boolean
 * @default false
 *
 * @arg mapaX
 * @text Coordenada X do Mapa
 * @type number
 * @decimals 2
 * @default 0
 *
 * @arg mapaY
 * @text Coordenada Y do Mapa
 * @type number
 * @decimals 2
 * @default 0
 *
 * @arg duracao
 * @text Duração
 * @desc Ex.: 30, 1s, 30w.
 * @default 0
 *
 * @arg zoom
 * @text Zoom
 * @desc Ex.: 2, +0.5, *1.2, 2ei.
 *
 * @arg deslocamentoX
 * @text Deslocamento Horizontal
 * @desc Em pixels.
 *
 * @arg deslocamentoY
 * @text Deslocamento Vertical
 * @desc Em pixels.
 *
 * @command IgnorarZoom
 * @text ★ Ignorar Zoom em Personagens
 * @desc Mantém personagens selecionados com tamanho estável.
 *
 * @arg executarAoIniciar
 * @text Executar ao Iniciar?
 * @type boolean
 * @default false
 *
 * @arg alvoNome
 * @text Personagens por Nome
 *
 * @arg alvoId
 * @text Personagens por ID
 * @desc Separe múltiplos IDs por vírgula.
 *
 * @arg escala
 * @text Escala ao Ignorar Zoom
 * @type number
 * @decimals 2
 * @default 1
 *
 * @arg limparTudo
 * @text Limpar Todas as Exclusões?
 * @type boolean
 * @default false
 *
 * @command IniciarArrasto
 * @text ★ Iniciar Modo de Arrastar
 *
 * @arg executarAoIniciar
 * @text Executar ao Iniciar?
 * @type boolean
 * @default false
 *
 * @arg velocidadeTouch
 * @text Velocidade do Arrasto
 * @type number
 * @decimals 2
 * @default 1
 *
 * @arg inerciaTouch
 * @text Inércia do Arrasto
 * @type number
 * @default 40
 *
 * @arg velocidadeTeclas
 * @text Velocidade pelas Teclas
 * @type number
 * @decimals 2
 * @default 1
 *
 * @arg inerciaTeclas
 * @text Inércia pelas Teclas
 * @type number
 * @default 20
 *
 * @arg aceleracaoTeclas
 * @text Aceleração pelas Teclas (%)
 * @type number
 * @default 5
 *
 * @arg restaurarPosicaoAoAndar
 * @text Restaurar Posição ao Andar?
 * @type boolean
 * @default true
 *
 * @arg restaurarZoomAoAndar
 * @text Restaurar Zoom ao Andar?
 * @type boolean
 * @default false
 *
 * @arg tempoRestauracao
 * @text Tempo para Restaurar
 * @default 30
 *
 * @arg manterEntreMapas
 * @text Manter entre Mapas?
 * @type boolean
 * @default false
 *
 * @arg permitirZoom
 * @text Permitir Zoom Manual?
 * @type boolean
 * @default true
 *
 * @arg velocidadePinch
 * @text Velocidade do Zoom por Toque
 * @type number
 * @decimals 2
 * @default 1
 *
 * @arg velocidadeRoda
 * @text Velocidade do Zoom pela Roda
 * @type number
 * @decimals 2
 * @default 1
 *
 * @arg velocidadeZoomTeclas
 * @text Velocidade do Zoom pelas Teclas
 * @type number
 * @decimals 2
 * @default 1
 *
 * @arg zoomMaximo
 * @text Zoom Máximo
 * @type number
 * @decimals 2
 * @default 1.5
 *
 * @arg zoomMinimo
 * @text Zoom Mínimo
 * @type number
 * @decimals 2
 * @default 0.35
 *
 * @command EncerrarArrasto
 * @text ★ Encerrar Modo de Arrastar
 *
 * @arg executarAoIniciar
 * @text Executar ao Iniciar?
 * @type boolean
 * @default false
 *
 * @arg restaurarPosicao
 * @text Restaurar Posição?
 * @type boolean
 * @default true
 *
 * @arg restaurarZoom
 * @text Restaurar Zoom?
 * @type boolean
 * @default true
 *
 * @arg tempo
 * @text Tempo para Restaurar
 * @default 30
 *
 * @command RestaurarCameraInicial
 * @text ★ Restaurar Câmera Inicial
 *
 * @arg tempo
 * @text Tempo para Restaurar
 * @default 30
 *
 * @command SalvarEstadoCamera
 * @text ★ Salvar Estado da Câmera
 *
 * @arg salvarAlvo
 * @text Salvar Alvo?
 * @type boolean
 * @default true
 *
 * @arg salvarZoom
 * @text Salvar Zoom?
 * @type boolean
 * @default true
 *
 * @arg salvarX
 * @text Salvar Deslocamento X?
 * @type boolean
 * @default true
 *
 * @arg salvarY
 * @text Salvar Deslocamento Y?
 * @type boolean
 * @default true
 *
 * @command RestaurarEstadoCamera
 * @text ★ Restaurar Estado da Câmera
 *
 * @arg tempo
 * @text Duração da Restauração
 * @default 30
 *
 * @arg restaurarAlvo
 * @text Restaurar Alvo?
 * @type boolean
 * @default true
 *
 * @arg restaurarZoom
 * @text Restaurar Zoom?
 * @type boolean
 * @default true
 *
 * @arg restaurarX
 * @text Restaurar Deslocamento X?
 * @type boolean
 * @default true
 *
 * @arg restaurarY
 * @text Restaurar Deslocamento Y?
 * @type boolean
 * @default true
 *
 * @command ConfigurarSeguimento
 * @text ★ Configurar Seguimento
 *
 * @arg ativar
 * @text Ativar Seguimento Avançado?
 * @type boolean
 * @default true
 *
 * @arg suavizacao
 * @text Suavização (frames)
 * @type number
 * @min 0
 * @default 8
 *
 * @arg lookAhead
 * @text Look-Ahead (tiles)
 * @type number
 * @decimals 2
 * @min 0
 * @default 0
 *
 * @arg deadZone
 * @text Dead Zone (px)
 * @type number
 * @min 0
 * @default 0
 *
 * @arg seguirX
 * @text Seguir Eixo X?
 * @type boolean
 * @default true
 *
 * @arg seguirY
 * @text Seguir Eixo Y?
 * @type boolean
 * @default true
 *
 * @command SalvarPreset
 * @text ★ Salvar Preset de Câmera
 *
 * @arg nome
 * @text Nome do Preset
 * @default preset
 *
 * @command RestaurarPreset
 * @text ★ Restaurar Preset de Câmera
 *
 * @arg nome
 * @text Nome do Preset
 * @default preset
 *
 * @arg tempo
 * @text Duração
 * @default 30
 *
 * @command ExcluirPreset
 * @text ★ Excluir Preset de Câmera
 *
 * @arg nome
 * @text Nome do Preset
 * @default preset
 *
 * @command DefinirLimites
 * @text ★ Definir Limites de Câmera
 * @desc Limita o centro da câmera dentro de um retângulo em tiles.
 *
 * @arg minX
 * @text X Mínimo
 * @type number
 * @decimals 2
 * @default 0
 *
 * @arg minY
 * @text Y Mínimo
 * @type number
 * @decimals 2
 * @default 0
 *
 * @arg maxX
 * @text X Máximo
 * @type number
 * @decimals 2
 * @default 10
 *
 * @arg maxY
 * @text Y Máximo
 * @type number
 * @decimals 2
 * @default 10
 *
 * @command LimparLimites
 * @text ★ Limpar Limites de Câmera
 */

(() => {
    "use strict";

    const PLUGIN_NAME = document.currentScript.src.match(/([^/]+)\.js$/i)?.[1] || "LudoCameraSystem";
    const VERSION = "4.4.5";
    const raw = PluginManager.parameters(PLUGIN_NAME);

    const bool = (v, d = false) => v == null || v === "" ? d : String(v).toLowerCase() === "true";
    const num = (v, d = 0) => Number.isFinite(Number(v)) ? Number(v) : d;
    const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
    const copy = obj => obj == null ? obj : JSON.parse(JSON.stringify(obj));
    const debug = (...args) => { if (PARAM.debug) console.log(`[${PLUGIN_NAME}]`, ...args); };

    const PARAM = {
        zoomDefault: Math.max(0.1, num(raw.zoomPadrao, 1)),
        fixPictures: bool(raw.fixarPictures, true),
        resetOnTransfer: bool(raw.restaurarAoTrocarMapa, true),
        messageCode: String(raw.codigoMensagem || "cm").toUpperCase(),
        nearestCharacters: bool(raw.evitarSuavizacaoSprite, true),
        nearestParallax: bool(raw.evitarSuavizacaoParallax, false),
        antiJitterEvents: bool(raw.removerJitterEventos, true),
        followEnabled: bool(raw.seguimentoAvancado, false),
        followSmooth: Math.max(0, num(raw.suavizacaoFrames, 0)),
        lookAhead: Math.max(0, num(raw.lookAheadTiles, 0)),
        deadZone: Math.max(0, num(raw.deadZonePixels, 0)),
        dragKey: String(raw.teclaArrasto || "shift"),
        zoomKey: String(raw.teclaZoom || "control"),
        debug: bool(raw.debug, false)
    };

    // Limit filtering to map textures, never global PIXI defaults.
    function sharpenMapSprite(sprite) {
        if (!sprite || typeof PIXI === "undefined" || !PIXI.SCALE_MODES) return;
        const bitmap = sprite.bitmap;
        if (bitmap && bitmap.smooth !== false) bitmap.smooth = false;
        const base = bitmap?.baseTexture || sprite.texture?.baseTexture;
        if (base && base.scaleMode !== PIXI.SCALE_MODES.NEAREST)
            base.scaleMode = PIXI.SCALE_MODES.NEAREST;
    }
    function updateMapTextureFiltering(spriteset) {
        if (!PARAM.nearestParallax) return;
        sharpenMapSprite(spriteset._parallax);
        // Exported LUDO maps hide the native parallax and draw these instead.
        for (const sprite of spriteset._ludoChunkSprites || []) sharpenMapSprite(sprite);
        for (const sprite of spriteset._ludoDynamicSprites || []) sharpenMapSprite(sprite);
    }

    function defaultState() {
        return {
            version: VERSION,
            target: { type: "player" },
            zoom: PARAM.zoomDefault,
            offsetX: 0,
            offsetY: 0,
            transition: null,
            focusCenterX: null,
            focusCenterY: null,
            follow: {
                enabled: PARAM.followEnabled,
                smoothFrames: PARAM.followSmooth,
                lookAheadTiles: PARAM.lookAhead,
                deadZonePixels: PARAM.deadZone,
                followX: true,
                followY: true
            },
            manual: null,
            saved: null,
            presets: {},
            bounds: null
        };
    }

    function state() {
        if (!$gameScreen) return null;
        if (!$gameScreen._ludoCameraState) $gameScreen._ludoCameraState = defaultState();
        const s = $gameScreen._ludoCameraState;
        if (!s.follow) s.follow = defaultState().follow;
        if (!s.presets) s.presets = {};
        return s;
    }

    function runtime() {
        if (!$gameTemp._ludoCameraRuntime) {
            $gameTemp._ludoCameraRuntime = {
                touchX: null,
                touchY: null,
                velocityX: 0,
                velocityY: 0,
                inertiaLeft: 0,
                inertiaMax: 1,
                keyPower: 0,
                pinchDistance: null,
                pinchDelta: 0,
                playerWasMoving: false,
                tilemapScale: null
            };
        }
        return $gameTemp._ludoCameraRuntime;
    }

    function parseTime(value) {
        const text = String(value ?? "0").trim();
        const wait = /w/i.test(text);
        let frames = 0;
        const sec = text.match(/(-?\d+(?:\.\d+)?)\s*s/i);
        if (sec) frames = Math.round(Number(sec[1]) * 60);
        else {
            const m = text.match(/-?\d+(?:\.\d+)?/);
            frames = m ? Math.round(Number(m[0])) : 0;
        }
        return { frames: Math.max(0, frames), wait };
    }

    function resolveVariableText(text) {
        return String(text ?? "").replace(/v\[(\d+)\]/gi, (_, id) => String($gameVariables ? $gameVariables.value(Number(id)) : 0));
    }

    function parseMotionValue(current, rawValue) {
        if (rawValue == null || String(rawValue).trim() === "") {
            return { value: current, easing: "e", rate: 1, repeat: 1, returnToStart: false };
        }
        let text = resolveVariableText(rawValue).trim();
        let repeat = 1;
        const repeatMatch = text.match(/_(-?\d+(?:\.\d+)?)$/);
        if (repeatMatch) {
            repeat = Number(repeatMatch[1]);
            text = text.slice(0, repeatMatch.index);
        }
        let rate = 1;
        const rateMatch = text.match(/\((-?\d+(?:\.\d+)?)\)$/);
        if (rateMatch) {
            rate = Number(rateMatch[1]);
            text = text.slice(0, rateMatch.index);
        }
        let easing = "e";
        const easeMatch = text.match(/(tn|cg|fk|cf|rd|bk|ei|eo|e)$/i);
        if (easeMatch) {
            easing = easeMatch[1].toLowerCase();
            text = text.slice(0, easeMatch.index);
        }
        text = text.trim();
        let value;
        if (/^\+\-/.test(text)) value = current - num(text.slice(2), 0);
        else if (/^\+/.test(text)) value = current + num(text.slice(1), 0);
        else if (/^\*/.test(text)) value = current * num(text.slice(1), 1);
        else if (/^\//.test(text)) {
            const d = num(text.slice(1), 1);
            value = d === 0 ? current : current / d;
        } else if (/^%/.test(text)) {
            const d = num(text.slice(1), 1);
            value = d === 0 ? current : current % d;
        } else value = num(text, current);
        return {
            value,
            easing,
            rate: Number.isFinite(rate) ? rate : 1,
            repeat: Number.isFinite(repeat) ? repeat : 1,
            returnToStart: easing === "bk"
        };
    }

    function ease(type, t, rate = 1) {
        t = clamp(t, 0, 1);
        const r = Math.max(0.01, Math.abs(rate || 1));
        switch (type) {
            case "ei": return Math.pow(t, 2 * r);
            case "eo": return 1 - Math.pow(1 - t, 2 * r);
            case "tn": {
                const u = t < 0.5 ? t * 2 : (1 - t) * 2;
                return 0.5 - Math.cos(Math.PI * u) / 2;
            }
            case "cg": {
                const s = 1.70158 * r;
                return t * t * ((s + 1) * t - s);
            }
            case "fk": {
                const s = 1.70158 * r;
                const u = t - 1;
                return 1 + u * u * ((s + 1) * u + s);
            }
            case "cf": {
                const s = 1.70158 * 1.525 * r;
                let u = t * 2;
                if (u < 1) return 0.5 * (u * u * ((s + 1) * u - s));
                u -= 2;
                return 0.5 * (u * u * ((s + 1) * u + s) + 2);
            }
            case "rd": return Math.sin(t * Math.PI / 2);
            case "bk": return 0.5 - Math.cos(Math.PI * (t < 0.5 ? t * 2 : (1 - t) * 2)) / 2;
            default: return 0.5 - Math.cos(Math.PI * t) / 2;
        }
    }

    function motionProgress(spec, baseT) {
        let repeat = spec.repeat;
        if (!Number.isFinite(repeat) || repeat === 0 || repeat < 0) repeat = 1;
        repeat = Math.max(1, Math.round(repeat));
        let cycle = baseT * repeat;
        let local = cycle - Math.floor(cycle);
        if (baseT >= 1) local = 1;
        return ease(spec.easing, local, spec.rate);
    }

    function descriptorForCharacter(ch) {
        if (!ch || ch === $gamePlayer) return { type: "player" };
        if (ch instanceof Game_Event) return { type: "event", id: ch.eventId(), mapId: $gameMap.mapId() };
        if (typeof Game_Follower !== "undefined" && ch instanceof Game_Follower) return { type: "follower", index: ch._memberIndex };
        if (typeof Game_Vehicle !== "undefined" && ch instanceof Game_Vehicle) return { type: "vehicle", vehicleType: ch._type };
        return { type: "point", x: num(ch._realX, 0), y: num(ch._realY, 0) };
    }

    function resolveDescriptor(desc) {
        if (!desc) return $gamePlayer;
        switch (desc.type) {
            case "player": return $gamePlayer;
            case "event": return desc.mapId === $gameMap.mapId() ? $gameMap.event(desc.id) : null;
            case "follower": return $gamePlayer.followers().follower(Math.max(0, Number(desc.index) - 1)) || null;
            case "vehicle": return $gameMap.vehicle(desc.vehicleType) || null;
            case "point": return { _realX: num(desc.x, 0), _realY: num(desc.y, 0), _ludoPoint: true };
            default: return $gamePlayer;
        }
    }

    function allCharacters() {
        const out = [$gamePlayer];
        if ($gamePlayer?.followers) out.push(...$gamePlayer.followers().data());
        if ($gameMap?.events) out.push(...$gameMap.events());
        if ($gameMap?.vehicles) out.push(...$gameMap.vehicles());
        return out.filter(Boolean);
    }

    function currentEventFromInterpreter(interpreter) {
        if (!interpreter || !$gameMap) return null;
        const id = interpreter.eventId ? interpreter.eventId() : interpreter._eventId;
        return id > 0 ? $gameMap.event(id) : null;
    }

    function charactersByIds(text, interpreter) {
        const ids = resolveVariableText(text).split(/[\s,]+/).map(Number).filter(Number.isFinite);
        const result = [];
        for (const id of ids) {
            if (id >= 1) {
                const ev = $gameMap.event(id); if (ev) result.push(ev);
            } else if (id === 0) {
                const ev = currentEventFromInterpreter(interpreter); if (ev) result.push(ev);
            } else if (id === -1) result.push($gamePlayer);
            else if (id === -2) result.push(...allCharacters());
            else if (id === -10) result.push(...$gamePlayer.followers().data());
            else if (id <= -11 && id > -100) {
                const f = $gamePlayer.followers().follower(Math.abs(id) - 11); if (f) result.push(f);
            } else if (id === -100) result.push(...$gameMap.vehicles());
            else if (id <= -101) {
                const types = ["boat", "ship", "airship"];
                const type = types[Math.abs(id) - 101];
                if (type) { const v = $gameMap.vehicle(type); if (v) result.push(v); }
            }
        }
        return [...new Set(result)];
    }

    function charactersByName(text) {
        const q = resolveVariableText(text).trim().toLowerCase();
        if (!q) return [];
        return $gameMap.events().filter(ev => {
            const data = ev.event();
            return String(data?.name || "").toLowerCase().includes(q) || String(data?.note || "").toLowerCase().includes(q);
        });
    }

    function chooseTarget(args, interpreter) {
        if (bool(args.usarCoordenada, false)) {
            return { type: "point", x: num(resolveVariableText(args.mapaX), 0), y: num(resolveVariableText(args.mapaY), 0) };
        }
        const byName = charactersByName(args.alvoNome || "");
        if (byName.length) return descriptorForCharacter(byName[0]);
        const byId = charactersByIds(args.alvoId ?? "-1", interpreter);
        return descriptorForCharacter(byId[0] || $gamePlayer);
    }

    function targetPoint(desc) {
        const target = resolveDescriptor(desc) || $gamePlayer;
        return { x: num(target._realX, 0), y: num(target._realY, 0), char: target };
    }

    function effectiveCameraZoom(logicalZoom) {
        return Math.max(0.1, Number(logicalZoom || 1));
    }

    function halfX(zoom) { return (Graphics.width / ($gameMap.tileWidth() * zoom) - 1) / 2; }
    function halfY(zoom) { return (Graphics.height / ($gameMap.tileHeight() * zoom) - 1) / 2; }

    function currentCameraCenter() {
        const s = state();
        if (s?.focusCenterX != null && s?.focusCenterY != null) return { x: s.focusCenterX, y: s.focusCenterY };
        const z = effectiveCameraZoom(s?.zoom || 1);
        return { x: $gameMap.displayX() + halfX(z), y: $gameMap.displayY() + halfY(z) };
    }

    function applyBounds(x, y, s) {
        if (!s.bounds) return { x, y };
        return {
            x: clamp(x, Math.min(s.bounds.minX, s.bounds.maxX), Math.max(s.bounds.minX, s.bounds.maxX)),
            y: clamp(y, Math.min(s.bounds.minY, s.bounds.maxY), Math.max(s.bounds.minY, s.bounds.maxY))
        };
    }

    function diagonalMovementProvider() {
        try {
            if (window.Ludo && typeof window.Ludo.optional === "function") {
                const provider = window.Ludo.optional("movement.diagonal", ">=1.0.0");
                if (provider) return provider;
            }
        } catch (_) {
            // Integração opcional: a câmera continua funcionando sem o módulo diagonal.
        }
        return window.LudoDiagonalMove || null;
    }

    function fallbackDirectionVector(direction) {
        const invSqrt2 = Math.SQRT1_2;
        switch (Number(direction) || 0) {
            case 1: return { x: -invSqrt2, y:  invSqrt2 };
            case 2: return { x: 0, y: 1 };
            case 3: return { x:  invSqrt2, y:  invSqrt2 };
            case 4: return { x: -1, y: 0 };
            case 6: return { x: 1, y: 0 };
            case 7: return { x: -invSqrt2, y: -invSqrt2 };
            case 8: return { x: 0, y: -1 };
            case 9: return { x:  invSqrt2, y: -invSqrt2 };
            default: return { x: 0, y: 0 };
        }
    }

    function lookDirection8(target) {
        if (!target) return 0;
        const provider = diagonalMovementProvider();
        if (provider) {
            if (typeof provider.direction8Of === "function") {
                const d = Number(provider.direction8Of(target)) || 0;
                if (d) return d;
            }
            if (typeof provider.lookDirection === "function") {
                const d = Number(provider.lookDirection(target)) || 0;
                if (d) return d;
            }
        }
        const stored = Number(target._ludoDirection8) || 0;
        if (stored) return stored;
        return target.direction ? Number(target.direction()) || 0 : 0;
    }

    function computeLookAhead(target, follow) {
        if (!target || !follow.enabled || follow.lookAheadTiles <= 0) return { x: 0, y: 0 };
        const provider = diagonalMovementProvider();
        let vector = null;
        if (provider && typeof provider.directionVectorOf === "function") {
            vector = provider.directionVectorOf(target, true);
        }
        if (!vector || !Number.isFinite(Number(vector.x)) || !Number.isFinite(Number(vector.y))) {
            vector = fallbackDirectionVector(lookDirection8(target));
        }
        const n = follow.lookAheadTiles;
        return { x: Number(vector.x) * n, y: Number(vector.y) * n };
    }

    function applyDeadZone(current, desired, pixels, axis, zoom) {
        if (current == null || pixels <= 0) return desired;
        const tile = axis === "x" ? $gameMap.tileWidth() : $gameMap.tileHeight();
        const zone = pixels / (tile * zoom);
        const delta = desired - current;
        if (Math.abs(delta) <= zone) return current;
        return desired - Math.sign(delta) * zone;
    }

    function setDisplayFromCenter(x, y, zoom, ox, oy) {
        $gameMap.setDisplayPos(
            x - halfX(zoom) + ox / $gameMap.tileWidth(),
            y - halfY(zoom) + oy / $gameMap.tileHeight()
        );
    }

    function startTransition(newTarget, durationRaw, zoomRaw, xRaw, yRaw, interpreter) {
        const s = state();
        const time = parseTime(durationRaw);
        const startCenter = currentCameraCenter();
        const zoomSpec = parseMotionValue(s.zoom, zoomRaw);
        zoomSpec.value = Math.max(0.1, zoomSpec.value);
        s.transition = {
            total: time.frames,
            remaining: time.frames,
            startCenterX: startCenter.x,
            startCenterY: startCenter.y,
            startZoom: s.zoom,
            startX: s.offsetX,
            startY: s.offsetY,
            zoomSpec,
            xSpec: parseMotionValue(s.offsetX, xRaw),
            ySpec: parseMotionValue(s.offsetY, yRaw)
        };
        if (newTarget) s.target = copy(newTarget);
        if (time.frames <= 0) {
            s.zoom = s.transition.zoomSpec.value;
            s.offsetX = s.transition.xSpec.value;
            s.offsetY = s.transition.ySpec.value;
            s.transition = null;
        }
        if (time.wait && interpreter?.wait && time.frames > 0) interpreter.wait(time.frames);
        updateCamera(true);
    }

    function updateTransition(s) {
        const tr = s.transition;
        if (!tr) return;
        if (tr.total <= 0) { s.transition = null; return; }
        const t = clamp((tr.total - tr.remaining + 1) / tr.total, 0, 1);
        s.zoom = tr.startZoom + (tr.zoomSpec.value - tr.startZoom) * motionProgress(tr.zoomSpec, t);
        s.offsetX = tr.startX + (tr.xSpec.value - tr.startX) * motionProgress(tr.xSpec, t);
        s.offsetY = tr.startY + (tr.ySpec.value - tr.startY) * motionProgress(tr.ySpec, t);
        tr.remaining--;
        if (tr.remaining <= 0) {
            s.zoom = tr.zoomSpec.returnToStart ? tr.startZoom : tr.zoomSpec.value;
            s.offsetX = tr.xSpec.returnToStart ? tr.startX : tr.xSpec.value;
            s.offsetY = tr.ySpec.returnToStart ? tr.startY : tr.ySpec.value;
            s.transition = null;
        }
    }

    function updateMapRenderArea(zoom) {
        const ss = SceneManager._scene?._spriteset;
        const tilemap = ss?._tilemap;
        if (!tilemap) return;
        const rt = runtime();
        const margin = tilemap._margin || 20;
        const w = Math.ceil(Graphics.width / zoom) + margin * 2;
        const h = Math.ceil(Graphics.height / zoom) + margin * 2;
        const key = `${w}x${h}`;
        if (rt.tilemapScale !== key) {
            tilemap.width = w;
            tilemap.height = h;
            tilemap.refresh?.();
            rt.tilemapScale = key;
        }
        ss?._parallax?.move?.(ss._parallax.x, ss._parallax.y, Math.ceil(Graphics.width / zoom * 2), Math.ceil(Graphics.height / zoom * 2));
    }

    // Estado de renderização consumido por sistemas oficiais que desenham no
    // mapa. Evita que cada plugin tente adivinhar zoom, viewport e origem.
    function parallaxAxisDelta(previous, current, size, visible, loop) {
        if (loop) {
            let delta = current - previous;
            if (size > 0) delta -= Math.round(delta / size) * size;
            return delta;
        }
        if (size <= visible) return 0;
        const half = visible / 2;
        const project = value => Math.max(half, Math.min(size - half, value));
        // Project both focus positions through the SAME current viewport.
        // Changes of zoom/viewport alone therefore contribute no motion.
        return project(current) - project(previous);
    }

    function updateParallaxCameraMotion(s, zoom) {
        const tw = $gameMap.tileWidth(), th = $gameMap.tileHeight();
        const x = (s.focusCenterX + 0.5) * tw + Number(s.offsetX || 0);
        const y = (s.focusCenterY + 0.5) * th + Number(s.offsetY || 0);
        let motion = $gameMap._ludoParallaxCameraMotion;
        if (!motion || motion.mapId !== $gameMap.mapId()) {
            motion = {mapId: $gameMap.mapId(), x: 0, y: 0, lastX: x, lastY: y};
            $gameMap._ludoParallaxCameraMotion = motion;
        }
        motion.x += parallaxAxisDelta(motion.lastX, x, $gameMap.width() * tw,
            Graphics.width / zoom, $gameMap.isLoopHorizontal());
        motion.y += parallaxAxisDelta(motion.lastY, y, $gameMap.height() * th,
            Graphics.height / zoom, $gameMap.isLoopVertical());
        motion.lastX = x;
        motion.lastY = y;
    }

    function cameraRenderState() {
        const s = state();
        const zoom = Math.max(0.1, Number($gameScreen?.zoomScale?.() || effectiveCameraZoom(s?.zoom || 1)));
        return Object.freeze({
            zoom,
            logicalZoom: Number(s?.zoom || 1),
            parallaxTravelX: Number($gameMap?._ludoParallaxCameraMotion?.x || 0),
            parallaxTravelY: Number($gameMap?._ludoParallaxCameraMotion?.y || 0),
            zoomX: Number($gameScreen?.zoomX?.() || 0),
            zoomY: Number($gameScreen?.zoomY?.() || 0),
            offsetX: Number(s?.offsetX || 0),
            offsetY: Number(s?.offsetY || 0),
            displayPixelX: Number($gameMap?.displayX?.() || 0) * Number($gameMap?.tileWidth?.() || 0),
            displayPixelY: Number($gameMap?.displayY?.() || 0) * Number($gameMap?.tileHeight?.() || 0),
            viewportWidth: Graphics.width / zoom,
            viewportHeight: Graphics.height / zoom
        });
    }

    function syncOfficialMapLayers() {
        const spriteset = SceneManager._scene?._spriteset;
        if (spriteset && typeof spriteset.syncLudoCameraTransform === "function")
            spriteset.syncLudoCameraTransform(cameraRenderState());
    }

    function updateCamera(force = false) {
        if (!$gameMap || !$gameScreen || !(SceneManager._scene instanceof Scene_Map)) return;
        const s = state();
        updateTransition(s);
        updateManual(s);

        const pt = targetPoint(s.target);
        const look = computeLookAhead(pt.char, s.follow);
        let desiredX = pt.x + look.x;
        let desiredY = pt.y + look.y;

        if (s.transition) {
            const progress = 1 - s.transition.remaining / Math.max(1, s.transition.total);
            const e = ease("e", progress, 1);
            desiredX = s.transition.startCenterX + (desiredX - s.transition.startCenterX) * e;
            desiredY = s.transition.startCenterY + (desiredY - s.transition.startCenterY) * e;
        }

        const zoom = effectiveCameraZoom(s.zoom || 1);
        desiredX = applyDeadZone(s.focusCenterX, desiredX, s.follow.deadZonePixels, "x", zoom);
        desiredY = applyDeadZone(s.focusCenterY, desiredY, s.follow.deadZonePixels, "y", zoom);

        if (s.focusCenterX == null) s.focusCenterX = desiredX;
        if (s.focusCenterY == null) s.focusCenterY = desiredY;

        if (s.follow.enabled && s.follow.smoothFrames > 0 && !force && !s.manual?.active) {
            const alpha = 1 / Math.max(1, s.follow.smoothFrames);
            if (s.follow.followX) s.focusCenterX += (desiredX - s.focusCenterX) * alpha;
            if (s.follow.followY) s.focusCenterY += (desiredY - s.focusCenterY) * alpha;
        } else {
            if (s.follow.followX || !s.follow.enabled) s.focusCenterX = desiredX;
            if (s.follow.followY || !s.follow.enabled) s.focusCenterY = desiredY;
        }

        const b = applyBounds(s.focusCenterX, s.focusCenterY, s);
        s.focusCenterX = b.x;
        s.focusCenterY = b.y;
        updateParallaxCameraMotion(s, zoom);
        $gameScreen.setZoom(0, 0, zoom);
        setDisplayFromCenter(s.focusCenterX, s.focusCenterY, zoom, s.offsetX, s.offsetY);
        updateMapRenderArea(zoom);
        syncOfficialMapLayers();
    }

    function saveSnapshot() {
        const s = state();
        return {
            target: copy(s.target), zoom: s.zoom, offsetX: s.offsetX, offsetY: s.offsetY,
            follow: copy(s.follow), bounds: copy(s.bounds)
        };
    }

    function restoreSnapshot(snapshot, timeRaw, options = {}, interpreter) {
        if (!snapshot) return false;
        const s = state();
        const target = options.target === false ? s.target : snapshot.target;
        const zoom = options.zoom === false ? "" : String(snapshot.zoom);
        const x = options.x === false ? "" : String(snapshot.offsetX);
        const y = options.y === false ? "" : String(snapshot.offsetY);
        if (snapshot.follow) s.follow = copy(snapshot.follow);
        if (snapshot.bounds !== undefined) s.bounds = copy(snapshot.bounds);
        startTransition(target, timeRaw, zoom, x, y, interpreter);
        return true;
    }

    function clearZoomExclusions() {
        for (const ch of allCharacters()) ch._ludoCameraIgnoreZoomScale = null;
    }

    function commandControl(args, interpreter) {
        startTransition(chooseTarget(args, interpreter), args.duracao || "0", args.zoom, args.deslocamentoX, args.deslocamentoY, interpreter);
    }

    function commandIgnoreZoom(args, interpreter) {
        if (bool(args.limparTudo, false)) clearZoomExclusions();
        const chars = [...charactersByName(args.alvoNome || ""), ...charactersByIds(args.alvoId || "", interpreter)];
        const scale = Math.max(0.01, num(args.escala, 1));
        for (const ch of new Set(chars)) if (ch) ch._ludoCameraIgnoreZoomScale = scale;
    }

    function startManual(args) {
        const s = state();
        s.manual = {
            active: true,
            touchSpeed: num(args.velocidadeTouch, 1),
            touchInertia: Math.max(0, num(args.inerciaTouch, 40)),
            keySpeed: num(args.velocidadeTeclas, 1),
            keyInertia: Math.max(0, num(args.inerciaTeclas, 20)),
            keyAccel: Math.max(0, num(args.aceleracaoTeclas, 5)) / 100,
            restorePositionOnMove: bool(args.restaurarPosicaoAoAndar, true),
            restoreZoomOnMove: bool(args.restaurarZoomAoAndar, false),
            restoreTime: args.tempoRestauracao || "30",
            persistMaps: bool(args.manterEntreMapas, false),
            zoomEnabled: bool(args.permitirZoom, true),
            pinchSpeed: num(args.velocidadePinch, 1),
            wheelSpeed: num(args.velocidadeRoda, 1),
            keyZoomSpeed: num(args.velocidadeZoomTeclas, 1),
            maxZoom: Math.max(0.1, num(args.zoomMaximo, 1.5)),
            minZoom: Math.max(0.1, num(args.zoomMinimo, 0.35)),
            startSnapshot: saveSnapshot()
        };
        const rt = runtime();
        rt.touchX = rt.touchY = null;
        rt.velocityX = rt.velocityY = 0;
        rt.inertiaLeft = 0;
        rt.keyPower = 0;
        debug("Modo de arrastar iniciado");
    }

    function endManual(args = {}, interpreter) {
        const s = state();
        const m = s.manual;
        if (!m?.active) return;
        const snap = m.startSnapshot;
        s.manual = null;
        runtime().touchX = runtime().touchY = null;
        const restorePos = bool(args.restaurarPosicao, true);
        const restoreZoom = bool(args.restaurarZoom, true);
        if (snap && (restorePos || restoreZoom)) {
            restoreSnapshot(snap, args.tempo || "30", { target: restorePos, x: restorePos, y: restorePos, zoom: restoreZoom }, interpreter);
        }
        debug("Modo de arrastar encerrado");
    }

    function updateManual(s) {
        const m = s.manual;
        if (!m?.active) return;
        const rt = runtime();
        let changed = false;

        const dragKey = PARAM.dragKey !== "none" && Input.isPressed(PARAM.dragKey);
        if (dragKey) {
            const dx = (Input.isPressed("right") ? 1 : 0) - (Input.isPressed("left") ? 1 : 0);
            const dy = (Input.isPressed("down") ? 1 : 0) - (Input.isPressed("up") ? 1 : 0);
            if (dx || dy) {
                rt.keyPower = Math.min(4, rt.keyPower + m.keyAccel + 0.05);
                const speed = 8 * m.keySpeed * Math.max(1, rt.keyPower);
                rt.velocityX = dx * speed;
                rt.velocityY = dy * speed;
                s.offsetX += rt.velocityX;
                s.offsetY += rt.velocityY;
                rt.inertiaLeft = m.keyInertia;
                rt.inertiaMax = Math.max(1, m.keyInertia);
                changed = true;
            }
        } else rt.keyPower = 0;

        const zoomKey = PARAM.zoomKey !== "none" && Input.isPressed(PARAM.zoomKey);
        if (m.zoomEnabled && zoomKey) {
            const dir = (Input.isPressed("up") ? 1 : 0) - (Input.isPressed("down") ? 1 : 0);
            if (dir) {
                s.zoom = clamp(s.zoom + dir * 0.01 * m.keyZoomSpeed, m.minZoom, m.maxZoom);
                changed = true;
            }
        }

        if (TouchInput.isPressed() && !dragKey) {
            const x = TouchInput.x, y = TouchInput.y;
            if (rt.touchX != null && rt.touchY != null) {
                const dx = (rt.touchX - x) * m.touchSpeed;
                const dy = (rt.touchY - y) * m.touchSpeed;
                s.offsetX += dx;
                s.offsetY += dy;
                rt.velocityX = dx;
                rt.velocityY = dy;
                rt.inertiaLeft = m.touchInertia;
                rt.inertiaMax = Math.max(1, m.touchInertia);
                changed = true;
            }
            rt.touchX = x;
            rt.touchY = y;
        } else {
            rt.touchX = rt.touchY = null;
            if (!dragKey && rt.inertiaLeft > 0) {
                const f = rt.inertiaLeft / rt.inertiaMax;
                s.offsetX += rt.velocityX * f;
                s.offsetY += rt.velocityY * f;
                rt.inertiaLeft--;
                changed = true;
            }
        }

        if (m.zoomEnabled && TouchInput.wheelY) {
            s.zoom = clamp(s.zoom - TouchInput.wheelY * 0.001 * m.wheelSpeed, m.minZoom, m.maxZoom);
            changed = true;
        }
        if (m.zoomEnabled && rt.pinchDelta) {
            s.zoom = clamp(s.zoom + rt.pinchDelta * 0.005 * m.pinchSpeed, m.minZoom, m.maxZoom);
            rt.pinchDelta = 0;
            changed = true;
        }

        const moving = $gamePlayer.isMoving();
        if (moving && !rt.playerWasMoving && (m.restorePositionOnMove || m.restoreZoomOnMove)) {
            const snap = m.startSnapshot;
            s.manual = null;
            if (snap) restoreSnapshot(snap, m.restoreTime, {
                target: m.restorePositionOnMove,
                x: m.restorePositionOnMove,
                y: m.restorePositionOnMove,
                zoom: m.restoreZoomOnMove
            });
        }
        rt.playerWasMoving = moving;
        if (changed) s.transition = null;
    }

    function registerCommands() {
        PluginManager.registerCommand(PLUGIN_NAME, "ControlarCamera", function(args) { commandControl(args, this); });
        PluginManager.registerCommand(PLUGIN_NAME, "IgnorarZoom", function(args) { commandIgnoreZoom(args, this); });
        PluginManager.registerCommand(PLUGIN_NAME, "IniciarArrasto", function(args) { startManual(args); });
        PluginManager.registerCommand(PLUGIN_NAME, "EncerrarArrasto", function(args) { endManual(args, this); });
        PluginManager.registerCommand(PLUGIN_NAME, "RestaurarCameraInicial", function(args) {
            startTransition({ type: "player" }, args.tempo || "30", String(PARAM.zoomDefault), "0", "0", this);
        });
        PluginManager.registerCommand(PLUGIN_NAME, "SalvarEstadoCamera", function(args) {
            const snap = saveSnapshot();
            const s = state();
            s.saved = {
                target: bool(args.salvarAlvo, true) ? snap.target : null,
                zoom: bool(args.salvarZoom, true) ? snap.zoom : null,
                offsetX: bool(args.salvarX, true) ? snap.offsetX : null,
                offsetY: bool(args.salvarY, true) ? snap.offsetY : null,
                follow: snap.follow,
                bounds: snap.bounds
            };
        });
        PluginManager.registerCommand(PLUGIN_NAME, "RestaurarEstadoCamera", function(args) {
            const s = state();
            if (!s.saved) return;
            const snap = {
                target: s.saved.target || s.target,
                zoom: s.saved.zoom ?? s.zoom,
                offsetX: s.saved.offsetX ?? s.offsetX,
                offsetY: s.saved.offsetY ?? s.offsetY,
                follow: s.saved.follow || s.follow,
                bounds: s.saved.bounds
            };
            restoreSnapshot(snap, args.tempo || "30", {
                target: bool(args.restaurarAlvo, true), zoom: bool(args.restaurarZoom, true),
                x: bool(args.restaurarX, true), y: bool(args.restaurarY, true)
            }, this);
        });
        PluginManager.registerCommand(PLUGIN_NAME, "ConfigurarSeguimento", function(args) {
            const f = state().follow;
            f.enabled = bool(args.ativar, true);
            f.smoothFrames = Math.max(0, num(args.suavizacao, 8));
            f.lookAheadTiles = Math.max(0, num(args.lookAhead, 0));
            f.deadZonePixels = Math.max(0, num(args.deadZone, 0));
            f.followX = bool(args.seguirX, true);
            f.followY = bool(args.seguirY, true);
        });
        PluginManager.registerCommand(PLUGIN_NAME, "SalvarPreset", function(args) {
            const name = String(args.nome || "preset").trim(); if (name) state().presets[name] = saveSnapshot();
        });
        PluginManager.registerCommand(PLUGIN_NAME, "RestaurarPreset", function(args) {
            const snap = state().presets[String(args.nome || "preset").trim()];
            if (snap) restoreSnapshot(snap, args.tempo || "30", {}, this);
        });
        PluginManager.registerCommand(PLUGIN_NAME, "ExcluirPreset", function(args) {
            delete state().presets[String(args.nome || "preset").trim()];
        });
        PluginManager.registerCommand(PLUGIN_NAME, "DefinirLimites", function(args) {
            state().bounds = { minX: num(args.minX, 0), minY: num(args.minY, 0), maxX: num(args.maxX, 10), maxY: num(args.maxY, 10) };
        });
        PluginManager.registerCommand(PLUGIN_NAME, "LimparLimites", function() { state().bounds = null; });
    }

    function runStartCommandsForEvent(event) {
        if (!event || event._ludoCameraStartCommandsDone || event._pageIndex < 0) return;
        event._ludoCameraStartCommandsDone = true;
        const list = event.list?.();
        if (!Array.isArray(list)) return;
        for (const cmd of list) {
            if (cmd?.code !== 357) continue;
            const [plugin, command, , args] = cmd.parameters || [];
            if (plugin !== PLUGIN_NAME || !args || !bool(args.executarAoIniciar, false)) continue;
            const fn = PluginManager._commands?.[`${PLUGIN_NAME}:${command}`];
            if (typeof fn === "function") {
                const ip = new Game_Interpreter();
                ip._eventId = event.eventId();
                try { fn.call(ip, args); } catch (e) { console.error(`[${PLUGIN_NAME}] Falha em comando inicial`, e); }
            }
        }
    }

    function installHooks() {
        const _Game_Map_setup = Game_Map.prototype.setup;
        Game_Map.prototype.setup = function(mapId) {
            const old = $gameScreen?._ludoCameraState;
            const keepManual = !!old?.manual?.persistMaps;
            _Game_Map_setup.apply(this, arguments);
            this._ludoParallaxCameraMotion = null;
            if (PARAM.resetOnTransfer && !keepManual && $gameScreen) {
                const fresh = defaultState();
                if (old?.presets) fresh.presets = old.presets;
                $gameScreen._ludoCameraState = fresh;
            }
            if ($gameTemp) $gameTemp._ludoCameraRuntime = null;
        };

        const _Scene_Map_start = Scene_Map.prototype.start;
        Scene_Map.prototype.start = function() {
            _Scene_Map_start.apply(this, arguments);
            updateCamera(true);
        };

        const _Spriteset_Map_update = Spriteset_Map.prototype.update;
        Spriteset_Map.prototype.update = function() {
            _Spriteset_Map_update.apply(this, arguments);
            updateMapTextureFiltering(this);
            for (const ev of $gameMap.events()) runStartCommandsForEvent(ev);
        };

        // Atualiza a câmera no final do update da cena.
        // Assim usamos a posição mais recente do Player/Evento/Seguidor e evitamos
        // que o scroll nativo do Player sobrescreva o seguimento no mesmo frame.
        const _Scene_Map_update = Scene_Map.prototype.update;
        Scene_Map.prototype.update = function() {
            _Scene_Map_update.apply(this, arguments);
            updateCamera(false);
        };

        // O MZ calcula a área visível sem considerar o zoom de Game_Screen.
        // Para uma câmera livre isso impede o mapa de rolar corretamente, sobretudo
        // em mapas menores que a janela mas maiores que a área realmente visível com zoom.
        const _Game_Map_screenTileX = Game_Map.prototype.screenTileX;
        Game_Map.prototype.screenTileX = function() {
            if (SceneManager._scene instanceof Scene_Map) {
                const z = Math.max(0.1, $gameScreen?.zoomScale?.() || state()?.zoom || 1);
                if (z !== 1) return Graphics.width / (this.tileWidth() * z);
            }
            return _Game_Map_screenTileX.apply(this, arguments);
        };

        const _Game_Map_screenTileY = Game_Map.prototype.screenTileY;
        Game_Map.prototype.screenTileY = function() {
            if (SceneManager._scene instanceof Scene_Map) {
                const z = Math.max(0.1, $gameScreen?.zoomScale?.() || state()?.zoom || 1);
                if (z !== 1) return Graphics.height / (this.tileHeight() * z);
            }
            return _Game_Map_screenTileY.apply(this, arguments);
        };

        // Mantém clique/touch coerentes com o mapa ampliado ou afastado.
        const _Game_Map_canvasToMapX = Game_Map.prototype.canvasToMapX;
        Game_Map.prototype.canvasToMapX = function(x) {
            if (SceneManager._scene instanceof Scene_Map) {
                const z = Math.max(0.1, $gameScreen?.zoomScale?.() || state()?.zoom || 1);
                if (z !== 1) {
                    const tileWidth = this.tileWidth() * z;
                    const originX = this._displayX * tileWidth;
                    return this.roundX(Math.floor((originX + x) / tileWidth));
                }
            }
            return _Game_Map_canvasToMapX.apply(this, arguments);
        };

        const _Game_Map_canvasToMapY = Game_Map.prototype.canvasToMapY;
        Game_Map.prototype.canvasToMapY = function(y) {
            if (SceneManager._scene instanceof Scene_Map) {
                const z = Math.max(0.1, $gameScreen?.zoomScale?.() || state()?.zoom || 1);
                if (z !== 1) {
                    const tileHeight = this.tileHeight() * z;
                    const originY = this._displayY * tileHeight;
                    return this.roundY(Math.floor((originY + y) / tileHeight));
                }
            }
            return _Game_Map_canvasToMapY.apply(this, arguments);
        };

        const _Scene_Map_isMapTouchOk = Scene_Map.prototype.isMapTouchOk;
        Scene_Map.prototype.isMapTouchOk = function() {
            if (state()?.manual?.active) return false;
            return _Scene_Map_isMapTouchOk.apply(this, arguments);
        };

        const _Spriteset_Base_updatePosition = Spriteset_Base.prototype.updatePosition;
        Spriteset_Base.prototype.updatePosition = function() {
            _Spriteset_Base_updatePosition.apply(this, arguments);
            if (!PARAM.fixPictures || !(SceneManager._scene instanceof Scene_Map) || !this._pictureContainer) return;
            const sx = this.scale.x || 1;
            const sy = this.scale.y || 1;
            this._pictureContainer.scale.x = 1 / sx;
            this._pictureContainer.scale.y = 1 / sy;
            this._pictureContainer.x = -this.x / sx;
            this._pictureContainer.y = -this.y / sy;
        };

        const _Sprite_Character_updatePosition = Sprite_Character.prototype.updatePosition;
        Sprite_Character.prototype.updatePosition = function() {
            if (this._ludoCameraAppliedFactor) {
                this.scale.x /= this._ludoCameraAppliedFactor;
                this.scale.y /= this._ludoCameraAppliedFactor;
                this._ludoCameraAppliedFactor = null;
            }
            _Sprite_Character_updatePosition.apply(this, arguments);
            const ch = this._character;

            // Anti-Jitter de Eventos:
            // O Tilemap do MZ arredonda a origem da câmera com Math.ceil(), mas
            // Game_CharacterBase.screenX/screenY usa Math.round() no resultado final.
            // Em câmera subpixel, os dois podem trocar de pixel em frames diferentes,
            // fazendo o Evento "tremer" em relação ao cenário.
            //
            // A correção separa os dois componentes:
            // 1) posição do Evento é arredondada em espaço de mapa;
            // 2) origem visual da câmera usa o MESMO Math.ceil() do Tilemap.
            // Assim cenário e Evento avançam o pixel no mesmo frame.
            if (PARAM.antiJitterEvents && ch instanceof Game_Event && SceneManager._scene instanceof Scene_Map) {
                const tw = $gameMap.tileWidth();
                const th = $gameMap.tileHeight();
                const cameraPxX = $gameMap.displayX() * tw;
                const cameraPxY = $gameMap.displayY() * th;

                // scrolledX/Y já tratam mapas em loop. Somamos novamente a origem
                // para obter a posição visual do personagem no espaço do mapa antes
                // de aplicar o snap da câmera.
                const rawScreenX = ch.scrolledX() * tw + tw / 2;
                const rawScreenY = ch.scrolledY() * th + th - ch.shiftY() - ch.jumpHeight();
                const worldPxX = rawScreenX + cameraPxX;
                const worldPxY = rawScreenY + cameraPxY;

                this.x = Math.round(worldPxX) - Math.ceil(cameraPxX);
                this.y = Math.round(worldPxY) - Math.ceil(cameraPxY);
            }

            if (ch?._ludoCameraIgnoreZoomScale != null) {
                const factor = ch._ludoCameraIgnoreZoomScale / Math.max(0.1, $gameScreen.zoomScale() || 1);
                this.scale.x *= factor;
                this.scale.y *= factor;
                this._ludoCameraAppliedFactor = factor;
            }
        };

        if (PARAM.nearestCharacters) {
            const _Sprite_Character_updateBitmap = Sprite_Character.prototype.updateBitmap;
            Sprite_Character.prototype.updateBitmap = function() {
                _Sprite_Character_updateBitmap.apply(this, arguments);
                const bt = this.bitmap?.baseTexture;
                if (bt && typeof PIXI !== "undefined" && PIXI.SCALE_MODES) bt.scaleMode = PIXI.SCALE_MODES.NEAREST;
            };
        }

        const _Window_Message_processEscapeCharacter = Window_Message.prototype.processEscapeCharacter;
        Window_Message.prototype.processEscapeCharacter = function(code, textState) {
            if (String(code).toUpperCase() === PARAM.messageCode) {
                const m = textState.text.slice(textState.index).match(/^\[([^\]]*)\]/);
                if (m) {
                    textState.index += m[0].length;
                    const p = m[1].split(",").map(v => v.trim());
                    commandControl({
                        alvoId: p[0] || "-1", duracao: p[1] || "0", zoom: p[2] || "",
                        deslocamentoX: p[3] || "", deslocamentoY: p[4] || "", usarCoordenada: "false"
                    }, $gameMap?._interpreter);
                    return;
                }
            }
            _Window_Message_processEscapeCharacter.apply(this, arguments);
        };

        const _TouchInput_onTouchStart = TouchInput._onTouchStart;
        TouchInput._onTouchStart = function(event) {
            _TouchInput_onTouchStart.apply(this, arguments);
            if (event.touches?.length >= 2) {
                const a = event.touches[0], b = event.touches[1];
                runtime().pinchDistance = Math.hypot(a.clientX - b.clientX, a.clientY - b.clientY);
            }
        };

        const _TouchInput_onTouchMove = TouchInput._onTouchMove;
        TouchInput._onTouchMove = function(event) {
            _TouchInput_onTouchMove.apply(this, arguments);
            if (event.touches?.length >= 2) {
                const a = event.touches[0], b = event.touches[1];
                const dist = Math.hypot(a.clientX - b.clientX, a.clientY - b.clientY);
                const rt = runtime();
                if (rt.pinchDistance != null) rt.pinchDelta += (dist - rt.pinchDistance) / 100;
                rt.pinchDistance = dist;
            }
        };

        const _TouchInput_onTouchEnd = TouchInput._onTouchEnd;
        TouchInput._onTouchEnd = function(event) {
            _TouchInput_onTouchEnd.apply(this, arguments);
            if (!event.touches || event.touches.length < 2) runtime().pinchDistance = null;
        };
    }

    function registerCore() {
        const api = {
            version: VERSION,
            focusPlayer(time = 0) { startTransition({ type: "player" }, String(time), "", "", ""); },
            focusEvent(id, time = 0) { startTransition({ type: "event", id: Number(id), mapId: $gameMap.mapId() }, String(time), "", "", ""); },
            focusPoint(x, y, time = 0) { startTransition({ type: "point", x: Number(x), y: Number(y) }, String(time), "", "", ""); },
            setZoom(value, time = 0) { startTransition(state().target, String(time), String(value), "", ""); },
            setOffset(x, y, time = 0) { startTransition(state().target, String(time), "", String(x), String(y)); },
            savePreset(name) { state().presets[String(name)] = saveSnapshot(); },
            restorePreset(name, time = 0) { return restoreSnapshot(state().presets[String(name)], String(time)); },
            setBounds(minX, minY, maxX, maxY) { state().bounds = { minX, minY, maxX, maxY }; },
            clearBounds() { state().bounds = null; },
            state: () => state(),
            renderState: () => cameraRenderState(),
            isManual: () => !!state()?.manual?.active
        };
        window.LudoCameraSystem = api;
        if (window.LudoCore?.registerModule) {
            try {
                LudoCore.registerModule({
                    id: "camera", version: VERSION,
                    provides: { "camera": api, "camera.follow": api, "camera.presets": api, "camera.bounds": api,
                                "camera.render": api }
                });
            } catch (e) { debug("LudoCore registerModule indisponível ou incompatível", e); }
        }
    }

    registerCommands();
    installHooks();
    registerCore();
})();
