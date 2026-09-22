// LudoMapSystem.js v0.10.2 | RPG Maker MV
/*:
 * @target MV
 * @plugindesc LUDO Map Editor - mapas, profundidade e parallax multicamada v0.10.2-runtime-v5.
 * @author LUDO
 * @param _ludoGroup_underpass
 * @text Passagem por Baixo
 * @desc Agrupa configuracoes de passagem por baixo no Plugin Manager.
 * 
 * @param UnderpassFade
 * @text Transparência ao passar por baixo
 * @parent _ludoGroup_underpass
 * @type boolean
 * @default true
 * @param UnderpassOpacity
 * @text Opacidade da camada elevada
 * @parent _ludoGroup_underpass
 * @type number
 * @min 0
 * @max 255
 * @default 150
 * @desc 255 = opaca; 0 = invisível. Aplicada somente enquanto o jogador estiver embaixo.
 * @param UnderpassFadeFrames
 * @text Duração do fade (frames)
 * @parent _ludoGroup_underpass
 * @type number
 * @min 1
 * @max 600
 * @default 20
 * @command SetPlayerLevel
 * @text Definir nível do jogador
 * @desc Altera o nível de altura sem mudar X/Y. Use 0 para chão ou 1 para plataforma.
 * @arg level
 * @text Nível
 * @type select
 * @option 0 — Chão inferior
 * @value 0
 * @option 1 — Plataforma elevada
 * @value 1
 * @default 0
 * @arg followers
 * @text Aplicar aos seguidores
 * @type boolean
 * @default true
 * @param _ludoGroup_levels
 * @text Niveis por Regiao
 * @desc Agrupa configuracoes de niveis por regiao no Plugin Manager.
 * 
 * @param Level0Region
 * @text Região: desativar nível 1
 * @parent _ludoGroup_levels
 * @type number
 * @min 0
 * @max 255
 * @default 20
 * @desc ID da região que leva ao chão inferior. 0 desativa. Não use o mesmo ID nos dois níveis.
 * @param Level1Region
 * @text Região: ativar nível 1
 * @parent _ludoGroup_levels
 * @type number
 * @min 0
 * @max 255
 * @default 21
 * @desc ID da região que leva à plataforma. 0 desativa. Pinte somente as passagens de acesso.
 * @help
 * LudoMapSystem.js v0.10.2 - Guia de Ajuda MV
 *
 * PARA QUE SERVE
 * Executa mapas exportados pelo editor LUDO.
 * Adiciona niveis de altura, paralaxe de referencia e dados de mapa exportados.
 *
 * PRIMEIROS PASSOS (SEM PROGRAMAR)
 * 1. Exporte o mapa pelo editor LUDO.
 * 2. Coloque <LudoMap> na Nota do Mapa.
 * 3. Use <LudoLevel: 0> ou <LudoLevel: 1> nos eventos.
 *
 * ANTES DE COMECAR
 * Pode funcionar sem LudoCore.
 *
 * ONDE ESCREVER CADA COISA
 * A) Codigos de texto (barra invertida)
 * Este plugin NAO usa codigos de texto proprios.
 *
 * B) Tags de Nota do Banco de Dados
 * Este plugin NAO usa tags de Nota do Banco de Dados.
 *
 * C) Tags de Nota do Mapa
 * Use no campo Nota das propriedades do Mapa.
 *
 * D) Expressao de bloqueio
 * Este plugin NAO usa expressao de bloqueio.
 *
 * E) Script (JavaScript)
 * Use em Evento > Script ou em integracoes de plugins.
 *   LudoMapDepth.sameHeight($gamePlayer,$gameMap.event(1))
 *
 * F) Notas de Evento / Comentarios de Evento
 * Le Notas de Evento.
 * <LudoLevel: 0>
 * <LudoLevel: 1>
 * Este plugin NAO le Comentarios de Evento.
 *
 * CODIGOS DE TEXTO (RICH TEXT)
 * Este plugin NAO define codigos de Rich Text.
 *
 * TAGS DE NOTA DO BANCO DE DADOS
 * Este plugin NAO le tags de Nota do Banco de Dados.
 *
 * TAGS DE NOTA DO MAPA
 * <LudoMap>
 * <LudoReferenceParallax: Referencia>
 *
 * SCRIPT / API PUBLICA
 * Nivel:
 *   LudoMapDepth.sameHeight($gamePlayer,$gameMap.event(1))
 *
 * EXPRESSAO DE BLOQUEIO / CAMPO SCRIPT
 * Este plugin NAO possui campo de expressao de bloqueio.
 *
 * COMANDOS DE PLUGIN (MV)
 * Use Evento > Comando de Plugin:
 *   LudoMap SetPlayerLevel 0 true
 *   LudoMap SetPlayerLevel 1 true
 *   LudoMap SetVariation Pos-explosao
 *   LudoMap SetVariation Base
 *
 * COMO CONVERSA COM O RESTO DO LUDO
 * Este plugin NAO registra capability no LudoCore.
 *
 * VALE A PENA SABER
 * O nivel do jogador pode persistir no save.
 * Eventos usam nivel 0 quando nenhuma tag e informada.
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
*/

(() => {
    "use strict";

    const PLUGIN_NAME = "LudoMapSystem";
    const VERSION = "0.10.2";
    const FORMAT_VERSION = 5;
    const parameters = PluginManager.parameters(PLUGIN_NAME);
    const fadeEnabled = parameters.UnderpassFade !== "false";
    const fadeOpacity = Math.max(0,Math.min(255,Number((parameters.UnderpassOpacity != null ? parameters.UnderpassOpacity : 150))))/255;
    const fadeFrames = Math.max(1,Math.min(600,Number(parameters.UnderpassFadeFrames) || 20));
    function ludoFollowers(player) {
        if (!player || typeof player.followers !== "function") return [];
        const group = player.followers();
        if (!group) return [];
        // MV/MZ keep the complete follower list in _data. Some forks expose data(),
        // so keep that as a compatibility fallback without depending on it.
        if (Array.isArray(group._data)) return group._data;
        if (typeof group.data === "function") {
            const data = group.data();
            if (Array.isArray(data)) return data;
        }
        if (typeof group.visibleFollowers === "function") {
            const visible = group.visibleFollowers();
            if (Array.isArray(visible)) return visible;
        }
        return [];
    }
    function setPlayerLevel(levelValue, followersValue) {
        if (!$gamePlayer) return;
        const level = Number(levelValue) === 1 ? 1 : 0;
        $gamePlayer._ludoPerspectiveHeight = 0;
        $gamePlayer._ludoDepthLevel = level;
        if (String(followersValue == null ? "true" : followersValue).toLowerCase() !== "false") {
            const followers = ludoFollowers($gamePlayer);
            for (const follower of followers) follower._ludoDepthLevel = level;
        }
    }

    function normalizeVariationName(value) {
        return String(value == null ? "" : value).trim().toLocaleLowerCase();
    }
    function variationTarget(value) {
        const manifest = currentManifest();
        if (!manifest || !Array.isArray(manifest.variations)) return null;
        const raw = String(value == null ? "" : value).trim();
        const numeric = Number(raw);
        if (Number.isFinite(numeric) && numeric > 0) {
            return manifest.variations.find(function(item) { return Number(item && item.mapId) === numeric; }) || null;
        }
        const key = normalizeVariationName(raw);
        const wantsBase = key === "base" || key === "original" || key === "principal";
        if (wantsBase) return manifest.variations.find(function(item) { return item && item.base; }) || null;
        return manifest.variations.find(function(item) { return item && normalizeVariationName(item.name) === key; }) || null;
    }
    function setMapVariation(value, fadeValue) {
        if (!$gamePlayer || !$gameMap) return false;
        const target = variationTarget(value);
        if (!target || Number(target.mapId || 0) <= 0) return false;
        const mapId = Number(target.mapId);
        if (mapId === $gameMap.mapId()) return true;
        const fade = Math.max(0, Math.min(2, Number(fadeValue == null ? 0 : fadeValue) || 0));
        $gamePlayer.reserveTransfer(mapId, $gamePlayer.x, $gamePlayer.y, $gamePlayer.direction(), fade);
        return true;
    }

    // RPG Maker MV usa o comando de plugin legado. Sintaxe:
    //   LudoMap SetPlayerLevel 0 true
    //   LudoMap SetPlayerLevel 1 true
    // SetPerspective e mantido apenas para compatibilidade com eventos antigos.
    const _Game_Interpreter_pluginCommand = Game_Interpreter.prototype.pluginCommand;
    Game_Interpreter.prototype.pluginCommand = function(command, args) {
        _Game_Interpreter_pluginCommand.call(this, command, args);
        if (String(command || "").toLowerCase() !== "ludomap") return;
        const sub = String((args && args[0]) || "").toLowerCase();
        if (sub === "setplayerlevel") setPlayerLevel(args[1], args[2]);
        else if (sub === "setvariation") setMapVariation((args || []).slice(1).join(" "), 0);
        else if (sub === "setperspective" && $gamePlayer) $gamePlayer._ludoPerspectiveHeight = 0;
    };
    const state = {
        requestedMapId: 0,
        manifestMapId: 0,
        manifest: null,
        status: "idle",
        error: "",
        upperAlpha: 1,
        animationStart: 0
    };

    function mapFilename(mapId) {
        return "Map" + String(mapId).padStart(3, "0") + ".json";
    }

    function hasLudoTag(dataMap) {
        return !!dataMap && /<\s*LudoMap\s*>/i.test(String(dataMap.note || ""));
    }

    function ludoReferenceParallaxName(dataMap) {
        if (!dataMap) return "";
        const match = String(dataMap.note || "").match(/<\s*LudoReferenceParallax\s*:\s*([^>]+?)\s*>/i);
        return match ? String(match[1] || "").trim() : "";
    }

    function resetManifest(mapId) {
        state.requestedMapId = Number(mapId || 0);
        state.manifestMapId = 0;
        state.manifest = null;
        state.status = "idle";
        state.error = "";
        state.animationStart = 0;
        state.upperAlpha = 1;
    }

    function validateManifest(raw, mapId) {
        if (!raw || raw.format !== "ludo-map") {
            throw new Error("Manifesto LUDO invalido para o mapa " + mapId + ".");
        }
        const version = Number(raw.version || 0);
        if (version < 2 || version > FORMAT_VERSION) {
            throw new Error("Versao do mapa LUDO nao suportada: " + version + ".");
        }
        if (Number(raw.mapId || 0) !== Number(mapId)) {
            throw new Error("O manifesto LUDO pertence a outro mapa.");
        }
        if (!Array.isArray(raw.chunks)) raw.chunks = [];
        if (!Array.isArray(raw.dynamic)) raw.dynamic = [];
        if (!Array.isArray(raw.parallaxLayers)) raw.parallaxLayers = [];
        if (!Array.isArray(raw.collision)) raw.collision = [];
        // Runtime Contract v5: ordem explícita entre bandas estáticas e sprites
        // dinâmicos. Manifestos antigos continuam funcionando com ordem zero.
        raw.renderOrderCount = Math.max(1, Math.min(4096, Number(raw.renderOrderCount || 1)));
        raw.textureFiltering = String(raw.textureFiltering || "nearest").toLowerCase() === "smooth" ? "smooth" : "nearest";
        for (const item of raw.chunks) {
            item.renderOrder = Math.max(0, Math.min(raw.renderOrderCount - 1, Number(item.renderOrder || 0)));
        }
        for (const item of raw.dynamic) {
            item.renderOrder = Math.max(0, Math.min(raw.renderOrderCount - 1, Number(item.renderOrder || 0)));
            item.stackOrder = Math.max(0, Math.min(4096, Number(item.stackOrder || 0)));
        }
        // A ordem do painel do Editor é bottom -> top no modelo. O índice
        // exportado torna essa pilha determinística, sem depender do tie-break
        // interno do Tilemap quando vários sprites têm o mesmo z.
        for (const item of raw.parallaxLayers) {
            item.parallaxOrder = Math.max(0, Math.min(4096, Number(item.parallaxOrder || 0)));
        }
        // Tile Effects antigos foram removidos do editor. Ignorar dados legados
        // evita manter shaders/overlays que o exportador atual não produz mais.
        if (raw.tileEffects) delete raw.tileEffects;
        if (raw.depth && raw.depth.enabled) {
            const d = raw.depth, cells = Number(raw.width) * Number(raw.height);
            if (!Array.isArray(d.collision) || d.collision.length !== 2 ||
                d.collision.some(a => !Array.isArray(a) || a.length !== cells) ||
                !Array.isArray(d.support) || d.support.length !== cells)
                throw new Error("Dados de colisao por nivel invalidos. Exporte o mapa novamente.");
            d.scale = Math.max(.70, Math.min(1, Number(d.scale) || .90));
            d.startLevel = Number(d.startLevel) === 1 ? 1 : 0;
            if (!Array.isArray(d.transitions)) d.transitions = [];
            const occupied = new Set();
            for (const t of d.transitions) {
                if (![t.x0,t.y0,t.x1,t.y1].every(Number.isInteger) ||
                    (t.x0 === t.x1) === (t.y0 === t.y1) ||
                    Math.min(t.x0,t.x1,t.y0,t.y1) < 0 ||
                    Math.max(t.x0,t.x1) >= raw.width || Math.max(t.y0,t.y1) >= raw.height)
                    throw new Error("Escada invalida no mapa LUDO.");
                const steps = Math.abs(t.x1-t.x0)+Math.abs(t.y1-t.y0);
                for(let i=0;i<=steps;i++) {
                    const key = [t.x0+(t.x1-t.x0)*i/steps,t.y0+(t.y1-t.y0)*i/steps].join(",");
                    if(occupied.has(key)) throw new Error("Escadas LUDO sobrepostas.");
                    occupied.add(key);
                }
            }
        }
        return raw;
    }

    function requestManifest(mapId) {
        if (state.status === "loading" && state.requestedMapId === mapId) return;
        state.requestedMapId = mapId;
        state.status = "loading";
        state.error = "";
        const xhr = new XMLHttpRequest();
        const url = "data/ludoMaps/" + mapFilename(mapId);
        xhr.open("GET", url);
        xhr.overrideMimeType("application/json");
        xhr.onload = () => {
            if (xhr.status < 400) {
                try {
                    const manifest = validateManifest(JSON.parse(xhr.responseText), mapId);
                    // editorSource existe para sincronização/edição, não para o
                    // jogo. Liberamos essa cópia grande assim que o manifesto é validado.
                    if (manifest.editorSource) delete manifest.editorSource;
                    state.manifest = manifest;
                    state.manifestMapId = mapId;
                    state.status = "ready";
                    state.animationStart = performance.now();
                } catch (e) {
                    state.status = "error";
                    state.error = String(e && e.message ? e.message : e);
                }
            } else {
                state.status = "error";
                state.error = "Nao foi possivel carregar " + url + " (HTTP " + xhr.status + ").";
            }
        };
        xhr.onerror = () => {
            state.status = "error";
            state.error = "Nao foi possivel carregar " + url + ".";
        };
        xhr.send();
    }

    function currentManifest() {
        if (!state.manifest || state.status !== "ready") return null;
        if (!$gameMap || state.manifestMapId !== $gameMap.mapId()) return null;
        return state.manifest;
    }

    function noExtension(filename) {
        return String(filename || "").replace(/\.png$/i, "");
    }

    function applyBitmapFiltering(bitmap, mode) {
        if (!bitmap) return bitmap;
        const apply = () => {
            const base = bitmap.baseTexture || bitmap._baseTexture;
            if (!base || typeof PIXI === "undefined" || !PIXI.SCALE_MODES) return;
            base.scaleMode = mode === "smooth" ? PIXI.SCALE_MODES.LINEAR : PIXI.SCALE_MODES.NEAREST;
            if (typeof base.update === "function") base.update();
        };
        apply();
        if (typeof bitmap.addLoadListener === "function") bitmap.addLoadListener(apply);
        return bitmap;
    }

    function bitmapFor(filename, manifestOrMode) {
        const mode = typeof manifestOrMode === "string"
            ? manifestOrMode
            : (manifestOrMode && manifestOrMode.textureFiltering) || "nearest";
        return applyBitmapFiltering(ImageManager.loadBitmap("img/ludoMaps/", noExtension(filename)), mode);
    }

    function mapScaleX(manifest) {
        const source = Math.max(1, Number(manifest.tileWidth || 1));
        return $gameMap.tileWidth() / source;
    }

    function mapScaleY(manifest) {
        const source = Math.max(1, Number(manifest.tileHeight || 1));
        return $gameMap.tileHeight() / source;
    }

    function scrollPixelX() {
        return $gameMap.displayX() * $gameMap.tileWidth();
    }

    function scrollPixelY() {
        return $gameMap.displayY() * $gameMap.tileHeight();
    }

    function blendModeOf(name) {
        const modes = (typeof PIXI !== "undefined" && PIXI.BLEND_MODES) ? PIXI.BLEND_MODES : {};
        const key = String(name || "normal").toLowerCase();
        const aliases = {
            "lighter": "ADD", "add": "ADD", "additive": "ADD",
            "multiply": "MULTIPLY", "screen": "SCREEN", "overlay": "OVERLAY",
            "darken": "DARKEN", "lighten": "LIGHTEN", "difference": "DIFFERENCE",
            "exclusion": "EXCLUSION", "hard-light": "HARD_LIGHT", "hardlight": "HARD_LIGHT",
            "soft-light": "SOFT_LIGHT", "softlight": "SOFT_LIGHT",
            "color-dodge": "COLOR_DODGE", "colordodge": "COLOR_DODGE",
            "color-burn": "COLOR_BURN", "colorburn": "COLOR_BURN"
        };
        const enumName = aliases[key];
        if (enumName && modes[enumName] != null) return modes[enumName];
        return modes.NORMAL != null ? modes.NORMAL : 0;
    }

    function runtimeOrderZ(item, manifest, dynamicBias) {
        const version = Number(manifest && manifest.version || 0);
        const plane = String((item && (item.plane || item.mode)) || "below");
        if (version < 5) return plane === "above" ? 4 : 0;
        const count = Math.max(1, Number(manifest.renderOrderCount || 1));
        const order = Math.max(0, Math.min(count - 1, Number(item.renderOrder || 0)));
        const step = 2.2 / Math.max(1, count);
        const base = plane === "above" ? 4.05 : 0.05;
        const stack = Math.max(0, Math.min(4096, Number(item.stackOrder || 0)));
        const bias = dynamicBias ? step * 0.25 + Math.min(step * 0.20, stack * step / 8192) : 0;
        return base + order * step + bias;
    }

    function setRuntimeVisibility(sprite, padding) {
        const pad = Math.max(0, Number(padding || 48));
        const width = Math.max(1, Math.abs(Number(sprite.width || 1)));
        const height = Math.max(1, Math.abs(Number(sprite.height || 1)));
        const ax = sprite.anchor ? Number(sprite.anchor.x || 0) : 0;
        const ay = sprite.anchor ? Number(sprite.anchor.y || 0) : 0;
        const left = Number(sprite.x || 0) - ax * width;
        const top = Number(sprite.y || 0) - ay * height;
        const visible = left + width + pad >= 0 && top + height + pad >= 0 &&
                        left - pad <= Graphics.width && top - pad <= Graphics.height;
        sprite.visible = visible;
        sprite.renderable = visible;
        return visible;
    }

    function frameIndex(item, elapsedMs) {
        const frames = Array.isArray(item.frames) ? item.frames.length : 0;
        if (frames <= 1) return 0;
        const fps = Math.max(0.1, Math.min(120, Number(item.fps || 6)));
        const pingPong = !!item.pingPong;
        const sequence = pingPong ? Math.max(1, frames * 2 - 2) : frames;
        let step = Math.max(0, Math.floor((Math.max(0, elapsedMs) * fps) / 1000));
        if (!item.synchronized && sequence > 1) step += Math.max(0, Number(item.phase || 0)) % sequence;
        if (item.loop === false) step = Math.min(step, sequence - 1);
        else step %= sequence;
        let index = step;
        if (pingPong && index >= frames) index = (frames * 2 - 2) - index;
        return Math.max(0, Math.min(frames - 1, index));
    }

    function applyDynamicFrame(sprite, item, index, manifest) {
        const frames = Array.isArray(item.frames) ? item.frames : [];
        const frame = frames[index] || frames[0];
        if (!Array.isArray(frame) || frame.length < 4) return;
        const sx = Number(frame[0] || 0);
        const sy = Number(frame[1] || 0);
        const sw = Math.max(1, Number(frame[2] || 1));
        const sh = Math.max(1, Number(frame[3] || 1));
        sprite.setFrame(sx, sy, sw, sh);

        const scaleX = mapScaleX(manifest) * Math.max(0.0001, Number(item.width || sw)) / sw;
        const scaleY = mapScaleY(manifest) * Math.max(0.0001, Number(item.height || sh)) / sh;
        sprite.scale.set(scaleX, scaleY);
    }

    function createChunkSprite(item, manifest) {
        const sprite = new Sprite(bitmapFor(item.file, manifest));
        sprite._ludoItem = item;
        sprite.anchor.set(0, 0);
        sprite.z = runtimeOrderZ(item, manifest, false);
        sprite.scale.set(mapScaleX(manifest), mapScaleY(manifest));
        return sprite;
    }

    function createDynamicSprite(item, manifest) {
        const sprite = new Sprite(bitmapFor(item.atlas, manifest));
        sprite._ludoItem = item;
        sprite._ludoFrame = -1;
        sprite.blendMode = blendModeOf(item.blendMode);
        sprite.opacity = Math.round(Math.max(0, Math.min(1, Number(item.opacity == null ? 1 : item.opacity))) * 255);
        const mode = String(item.mode || "below");
        const priority = Math.max(0, Math.min(5, Number(item.priority || 0)));
        if (mode === "priority" && priority > 0) {
            sprite.z = 3;
            sprite.anchor.set(0, priority);
        } else {
            sprite.z = runtimeOrderZ(item, manifest, true);
            sprite.anchor.set(0, 0);
        }
        applyDynamicFrame(sprite, item, 0, manifest);
        sprite._ludoFrame = 0;
        return sprite;
    }

    function createParallaxSprite(item,manifest){
        const bitmap=bitmapFor(item.file,manifest),sprite=new Sprite();sprite._ludoItem=item;sprite._ludoBitmap=bitmap;
        sprite._ludoRepeatChildren=[];
        const parallaxOrder=Math.max(0,Math.min(4096,Number(item.parallaxOrder||0)));
        const parallaxBase=String(item.plane||"below")==="above"?4.02:0.01;
        // Mantém todas as imagens de parallax dentro da sua banda (abaixo ou
        // acima dos chunks), mas força a ordem do Editor: índice maior = topo.
        sprite.z=parallaxBase+Math.min(0.03,parallaxOrder*0.0001);
        sprite.opacity=Math.round(Math.max(0,Math.min(1,Number(item.opacity==null?1:item.opacity)))*255);
        const xs=item.repeatX?[-1,0,1]:[0],ys=item.repeatY?[-1,0,1]:[0];
        for(const ry of ys)for(const rx of xs){const child=new Sprite(bitmap);child.anchor.set(0,0);child.blendMode=blendModeOf(item.blendMode);child._ludoRepeatX=rx;child._ludoRepeatY=ry;sprite.addChild(child);sprite._ludoRepeatChildren.push(child);}
        return sprite;
    }

    function createParallaxBackground(manifest){
        if(!(manifest.parallaxLayers||[]).length)return null;
        const bitmap=new Bitmap(1,1);bitmap.fillAll(String(manifest.backgroundColor||"#000000"));
        const sprite=new Sprite(bitmap);sprite.anchor.set(0,0);sprite.z=-1;sprite.scale.set(Graphics.width+2,Graphics.height+2);return sprite;
    }

    function updateParallaxSprite(sprite,manifest,elapsedMs){
        const item=sprite._ludoItem,sx=mapScaleX(manifest),sy=mapScaleY(manifest);
        const fx=Math.max(-4,Math.min(4,Number(item.factorX==null?0.5:item.factorX)));
        const fy=Math.max(-4,Math.min(4,Number(item.factorY==null?0.5:item.factorY)));
        const bitmap=sprite._ludoBitmap,w=Math.max(0,Number(bitmap&&bitmap.width||0)),h=Math.max(0,Number(bitmap&&bitmap.height||0));
        for(const child of sprite._ludoRepeatChildren||[]){child.x=child._ludoRepeatX*w;child.y=child._ludoRepeatY*h;}
        sprite.scale.set(sx,sy);
        let x=Number(item.x||0)-scrollPixelX()*fx/sx+Number(item.speedX||0)*elapsedMs/1000;
        let y=Number(item.y||0)-scrollPixelY()*fy/sy+Number(item.speedY||0)*elapsedMs/1000;
        if(item.repeatX&&w>0)x=((x%w)+w)%w-w;if(item.repeatY&&h>0)y=((y%h)+h)%h-h;
        sprite.x=x*sx;sprite.y=y*sy;
        // Planos de parallax são fundos de câmera. O recorte comum usado por
        // chunks e sprites dinâmicos mede apenas a raiz da Sprite e pode
        // considerar um plano fora da tela mesmo quando uma cópia repetida ou
        // a própria imagem ainda cobre a viewport. Mantemos o plano ativo e
        // deixamos o tilemap decidir o recorte real de desenho.
        sprite.visible=true;sprite.renderable=true;
        for(const child of sprite._ludoRepeatChildren||[]){child.visible=true;child.renderable=true;}
    }

    function updateChunkSprite(sprite, manifest) {
        const item = sprite._ludoItem;
        const sx = mapScaleX(manifest);
        const sy = mapScaleY(manifest);
        sprite.scale.set(sx, sy);
        sprite.x = Number(item.x || 0) * sx - scrollPixelX();
        sprite.y = Number(item.y || 0) * sy - scrollPixelY();
        projectSprite(sprite, Number(item.level || 0), runtimeOrderZ(item, manifest, false));
        setRuntimeVisibility(sprite, 64);
    }

    function updateDynamicSprite(sprite, manifest, elapsedMs) {
        const item = sprite._ludoItem;
        const index = frameIndex(item, elapsedMs);
        if (index !== sprite._ludoFrame) {
            applyDynamicFrame(sprite, item, index, manifest);
            sprite._ludoFrame = index;
        }
        const sx = mapScaleX(manifest);
        const sy = mapScaleY(manifest);
        const mode = String(item.mode || "below");
        const priority = Math.max(0, Math.min(5, Number(item.priority || 0)));
        sprite.x = Number(item.x || 0) * sx - scrollPixelX();
        if (mode === "priority" && priority > 0) {
            sprite.y = (Number(item.y || 0) + Number(item.height || 0) * priority) * sy - scrollPixelY();
        } else {
            sprite.y = Number(item.y || 0) * sy - scrollPixelY();
        }
        const baseZ = mode === "priority" && priority > 0
            ? 3
            : runtimeOrderZ(item, manifest, true);
        projectSprite(sprite, Number(item.level || 0), baseZ);
        setRuntimeVisibility(sprite, 64);
    }

    // O MapXXX usa um panorama apenas para o editor nativo do RPG Maker MV.
    // Em runtime zeramos SOMENTE o panorama marcado pela tag LUDO, preservando
    // o arquivo no JSON para que continue visível como guia ao editar eventos.
    const _Game_Map_setupParallax = Game_Map.prototype.setupParallax;
    Game_Map.prototype.setupParallax = function() {
        _Game_Map_setupParallax.call(this);
        if (!hasLudoTag($dataMap)) return;
        const referenceName = ludoReferenceParallaxName($dataMap);
        if (referenceName && this._parallaxName === referenceName) {
            this._parallaxName = "";
            this._parallaxZero = false;
            this._parallaxLoopX = false;
            this._parallaxLoopY = false;
            this._parallaxSx = 0;
            this._parallaxSy = 0;
        }
    };

    const _DataManager_loadMapData = DataManager.loadMapData;
    DataManager.loadMapData = function(mapId) {
        resetManifest(mapId);
        _DataManager_loadMapData.call(this, mapId);
    };

    const _Scene_Map_isReady = Scene_Map.prototype.isReady;
    Scene_Map.prototype.isReady = function() {
        if (DataManager.isMapLoaded() && hasLudoTag($dataMap)) {
            const mapId = Number(state.requestedMapId || 0);
            if (state.status === "idle") requestManifest(mapId);
            if (state.status === "loading") return false;
            if (state.status === "error") throw new Error("[" + PLUGIN_NAME + "] " + state.error);
        }
        return _Scene_Map_isReady.call(this);
    };

    const _Spriteset_Map_createCharacters = Spriteset_Map.prototype.createCharacters;
    Spriteset_Map.prototype.createCharacters = function() {
        _Spriteset_Map_createCharacters.call(this);
        this.createLudoMapLayers();
    };

    Spriteset_Map.prototype.createLudoMapLayers = function() {
        this._ludoChunkSprites = [];
        this._ludoDynamicSprites = [];
        this._ludoParallaxSprites = [];
        const manifest = currentManifest();
        if (!manifest || !hasLudoTag($dataMap) || !this._tilemap) return;

        this._ludoParallaxBackground=createParallaxBackground(manifest);if(this._ludoParallaxBackground)this._tilemap.addChild(this._ludoParallaxBackground);
        for(const item of manifest.parallaxLayers||[]){const sprite=createParallaxSprite(item,manifest);this._tilemap.addChild(sprite);this._ludoParallaxSprites.push(sprite);}
        for (const item of manifest.chunks || []) {
            const sprite = createChunkSprite(item, manifest);
            this._tilemap.addChild(sprite);
            this._ludoChunkSprites.push(sprite);
        }
        for (const item of manifest.dynamic || []) {
            const sprite = createDynamicSprite(item, manifest);
            this._tilemap.addChild(sprite);
            this._ludoDynamicSprites.push(sprite);
        }
        this.updateLudoMapLayers();
    };

    Spriteset_Map.prototype.updateLudoMapLayers = function() {
        const manifest = currentManifest();
        if (!manifest) return;
        updateUnderpassFade(manifest);
        const elapsedMs = Math.max(0, performance.now() - state.animationStart);
        if(this._ludoParallaxBackground)this._ludoParallaxBackground.scale.set(Graphics.width+2,Graphics.height+2);
        for(const sprite of this._ludoParallaxSprites||[])updateParallaxSprite(sprite,manifest,elapsedMs);
        for (const sprite of this._ludoChunkSprites || []) updateChunkSprite(sprite, manifest);
        for (const sprite of this._ludoDynamicSprites || []) updateDynamicSprite(sprite, manifest, elapsedMs);
    };

    const _Spriteset_Map_update = Spriteset_Map.prototype.update;
    Spriteset_Map.prototype.update = function() {
        _Spriteset_Map_update.call(this);
        this.updateLudoMapLayers();
    };

    function collisionBitsAt(manifest, x, y) {
        const width = Math.max(0, Number(manifest.width || 0));
        const height = Math.max(0, Number(manifest.height || 0));
        if (x < 0 || y < 0 || x >= width || y >= height) return 15;
        const index = y * width + x;
        return Number((manifest.collision || [])[index] || 0) & 15;
    }

    function directionBits(d) {
        switch (d) {
        case 2: return [4, 1];   // baixo / entrada pelo topo
        case 4: return [8, 2];   // esquerda / entrada pela direita
        case 6: return [2, 8];   // direita / entrada pela esquerda
        case 8: return [1, 4];   // cima / entrada por baixo
        default: return [15, 15];
        }
    }

    const _Game_Map_isPassable = Game_Map.prototype.isPassable;
    Game_Map.prototype.isPassable = function(x, y, d) {
        const manifest = currentManifest();
        if (!manifest || !hasLudoTag($dataMap)) return _Game_Map_isPassable.call(this, x, y, d);
        if (depthConfig()) return passAt(manifest,x,y,d,$gamePlayer);
        const bits = directionBits(d);
        const x2 = this.roundXWithDirection(x, d);
        const y2 = this.roundYWithDirection(y, d);
        if (!this.isValid(x2, y2)) return false;
        if (collisionBitsAt(manifest, x, y) & bits[0]) return false;
        if (collisionBitsAt(manifest, x2, y2) & bits[1]) return false;
        return true;
    };
    // Depth is an optional v4 extension. Logical map coordinates never scale.
    function depthConfig() {
        const m = currentManifest();
        return m && m.depth && m.depth.enabled ? m.depth : null;
    }
    const clamp01 = n => Math.max(0, Math.min(1, Number(n) || 0));
    function levelOf(character) {
        if (!character) return 0;
        if (character._ludoDepthLevel != null) return clamp01(character._ludoDepthLevel);
        if (character instanceof Game_Event) {
            const match = String(character.event().note || "").match(/<LudoLevel\s*:\s*([01])\s*>/i);
            return match ? Number(match[1]) : 0;
        }
        return 0;
    }
    function stairAt(x, y) {
        const config = depthConfig();
        if (!config) return null;
        for (const t of config.transitions || []) {
            const dx = t.x1 - t.x0, dy = t.y1 - t.y0;
            const length = dx * dx + dy * dy;
            if (!length) continue;
            const progress = ((x - t.x0) * dx + (y - t.y0) * dy) / length;
            const distance = Math.abs((x - t.x0) * dy - (y - t.y0) * dx) / Math.sqrt(length);
            const transverse = dx === 0 ? x : y;
            const lo = Math.floor(transverse + 1e-7), hi = Math.ceil(transverse - 1e-7);
            const lane = dx === 0 ? t.x0 : t.y0;
            const adjacent = lo === hi || (config.transitions || []).some(other => dx === 0
                ? other.x0 === hi && other.x1 === hi && other.y0 === t.y0 && other.y1 === t.y1
                : other.y0 === hi && other.y1 === hi && other.x0 === t.x0 && other.x1 === t.x1);
            if (progress >= -0.00001 && progress <= 1.00001 && lane === lo && adjacent && distance < 1)
                return { t, progress: clamp01(progress) };
        }
        return null;
    }
    function heightOf(character) {
        const stair = stairAt(character._realX, character._realY);
        // A character below a bridge must not inherit the upper stair endpoint.
        if (stair && (stair.progress > 0 && stair.progress < 1 ||
            Math.abs(stair.progress - levelOf(character)) < 0.01)) return stair.progress;
        return levelOf(character);
    }
    function viewScale() { return 1; } // No perspective, including old saves/commands.
    function regionCell(x,y,d=0) {
        const cx = d === 6 ? Math.ceil(x-1e-7) : d === 4 ? Math.floor(x+1e-7) : Math.round(x);
        const cy = d === 2 ? Math.ceil(y-1e-7) : d === 8 ? Math.floor(y+1e-7) : Math.round(y);
        return [cx,cy];
    }
    function regionLevelAt(x,y,d=0) {
        if (!depthConfig() || !$gameMap.regionId) return null;

        const [cx,cy] = regionCell(x,y,d);
        if (!$gameMap.isValid(cx,cy)) return null;
        const id = $gameMap.regionId(cx,cy);
        const low = Number((parameters.Level0Region != null ? parameters.Level0Region : 20)), high = Number((parameters.Level1Region != null ? parameters.Level1Region : 21));
        if (low === high || !id) return null;
        return id === low && low > 0 ? 0 : id === high && high > 0 ? 1 : null;
    }
    function updateUnderpassFade(manifest) {
        const config = depthConfig();
        let under = false;
        if (fadeEnabled && config && levelOf($gamePlayer) === 0 && heightOf($gamePlayer) <= 1e-7) {
            const x = $gamePlayer._realX, y = $gamePlayer._realY;
            const stair = stairAt(x,y);
            // Reserved stair landings are an entrance, not a tunnel underneath.
            if (!stair) {
                for (const cx of new Set([Math.floor(x),Math.ceil(x)]))
                    for (const cy of new Set([Math.floor(y),Math.ceil(y)]))
                        if (cx >= 0 && cy >= 0 && cx < manifest.width && cy < manifest.height &&
                            config.support[cy*manifest.width+cx]) under = true;
            }
        }
        const target = under ? fadeOpacity : 1;
        const speed = Math.max(1e-7,(1-fadeOpacity)/fadeFrames);
        state.upperAlpha += Math.sign(target-state.upperAlpha)*Math.min(Math.abs(target-state.upperAlpha),speed);
    }
    function projectSprite(sprite, level, baseZ) {
        const config = depthConfig();
        if (!config) return;
        const factor = viewScale(level);
        const px = $gamePlayer.screenX(), py = $gamePlayer.screenY();
        sprite.x = px + (sprite.x - px) * factor;
        sprite.y = py + (sprite.y - py) * factor;
        sprite.scale.x *= factor; sprite.scale.y *= factor;
        sprite.z = level * 10 + baseZ;
        // Only map sprites fade, not characters. Restore source opacity every
        // frame so animated tiles and repeated fades never accumulate opacity.
        if (sprite._ludoItem) {
            const base = Math.max(0,Math.min(1,Number((sprite._ludoItem.opacity != null ? sprite._ludoItem.opacity : 1))));
            sprite.opacity = Math.round(255*base*(level === 1 ? state.upperAlpha : 1));
        }
    }
    function passAt(m, x, y, d, character, step = 1) {
        const config = depthConfig();
        const nx = x + (d === 6 ? step : d === 4 ? -step : 0), ny = y + (d === 2 ? step : d === 8 ? -step : 0);
        if (!$gameMap.isValid(nx, ny)) return false;
        const from = stairAt(x, y), to = stairAt(nx, ny);
        const level = levelOf(character);
        const regionSwitch = regionLevelAt(nx,ny,d);
        const middle = stair => stair && stair.progress > 0 && stair.progress < 1;
        // Interior of a stair may only connect to its own endpoints/axis.
        if (regionSwitch == null && middle(from) && (!to || to.t !== from.t)) return false;
        if (regionSwitch == null && middle(to) && (!from || from.t !== to.t ||
            from.progress === 0 && level !== 0 || from.progress === 1 && level !== 1)) return false;
        const onStair = regionSwitch == null && from && to && from.t === to.t;
        if (onStair && !middle(from) && Math.abs(from.progress - level) > .01) return false;
        const bits = directionBits(d);
        const axisCells = value => {
            const low = Math.floor(value + 1e-7), high = Math.ceil(value - 1e-7);
            return low === high ? [low] : [low,high];
        };
        const mask = (lv, ax, ay) => {
            if (ax < 0 || ay < 0 || ax >= m.width || ay >= m.height) return 15;
            return Number(config.collision[lv][ay * m.width + ax] || 0) & 15;
        };
        const supported = (ax,ay) => axisCells(ax).every(cx => axisCells(ay).every(cy =>
            cx >= 0 && cy >= 0 && cx < m.width && cy < m.height && config.support[cy*m.width+cx]));
        const regionLevel = regionLevelAt(nx,ny,d);
        const targetLevel = onStair || regionLevel == null ? level : regionLevel;
        const changingLevel = !onStair && targetLevel !== level;
        if (changingLevel) {
            const [cx,cy] = regionCell(nx,ny,d);
            if (targetLevel === 1 && !config.support[cy*m.width+cx]) return false;
        } else if (!onStair && level === 1 && ((!supported(x,y) && regionLevelAt(x,y,d) !== 1) || !supported(nx,ny))) return false;
        const levels = onStair ? [0,1] : [level];
        // Match half-tile movement's leading edge: never index arrays with .5.
        const horizontal = d === 4 || d === 6;
        const forward = d === 6 || d === 2;
        const source = horizontal ? x : y, target = horizontal ? nx : ny;
        const a = forward ? Math.floor(source + 1e-7) : Math.ceil(source - 1e-7);
        const b = forward ? Math.ceil(target - 1e-7) : Math.floor(target + 1e-7);
        return axisCells(horizontal ? y : x).every(cross => levels.every(lv => {
            const exit = horizontal ? mask(lv,a,cross) : mask(lv,cross,a);
            const entryLevel = changingLevel ? targetLevel : lv;
            const entry = horizontal ? mask(entryLevel,b,cross) : mask(entryLevel,cross,b);
            return !(exit & bits[0]) && !(entry & bits[1]);
        }));
    }
    function regionPositionKey(character) {
        return [$gameMap.mapId(),character.x,character.y].join(":");
    }
    function refreshPlayerRegion() {
        if (!depthConfig() || !$gamePlayer) return;
        if ($gamePlayer.isMoving && $gamePlayer.isMoving()) return;
        const key = regionPositionKey($gamePlayer);
        if ($gamePlayer._ludoRegionPosition === key) return;
        $gamePlayer._ludoRegionPosition = key;
        const level = regionLevelAt($gamePlayer.x,$gamePlayer.y);
        if (level != null) $gamePlayer._ludoDepthLevel = level;
    }
    const updatePlayer = Game_Player.prototype.update;
    Game_Player.prototype.update = function(...args) {
        if (updatePlayer) updatePlayer.apply(this,args);
        refreshPlayerRegion();
    };
    function commitDepthMove(character, beforeX, beforeY) {
        if (!depthConfig()) return;
        character._ludoRegionPosition = regionPositionKey(character);
        const dx = character.x-beforeX, dy = character.y-beforeY;
        if (Math.abs(dx)+Math.abs(dy) > 1e-7) {
            const d = Math.abs(dx) > Math.abs(dy) ? (dx>0?6:4) : (dy>0?2:8);
            const regionLevel = regionLevelAt(character.x,character.y,d);
            if (regionLevel != null) { character._ludoDepthLevel=regionLevel; return; }
        }
        const from = stairAt(beforeX,beforeY), to = stairAt(character.x,character.y);
        if (from && to && from.t === to.t) {
            if (to.progress <= 1e-7) character._ludoDepthLevel = 0;
            else if (to.progress >= 1-1e-7) character._ludoDepthLevel = 1;
        }
    }
    window.LudoMapDepth = {
        version: VERSION,
        canPass(character,x,y,d,step=1) {
            return depthConfig() ? passAt(currentManifest(),Number(x),Number(y),Number(d),character,step) : null;
        },
        didMove: commitDepthMove,
        sameHeight(a,b) { return !depthConfig() || sameHeight(a,b); }
    };

    const baseMapPass = Game_CharacterBase.prototype.isMapPassable;
    Game_CharacterBase.prototype.isMapPassable = function(x,y,d) {
        const m = currentManifest();
        return depthConfig() ? passAt(m,x,y,d,this) : baseMapPass.call(this,x,y,d);
    };
    // Player implementations may query map passage in both directions. A stair
    // endpoint cannot be tested as a new move from the old floor in reverse.
    // passAt already validates the source exit AND the destination entry bits.
    const playerMapPass = Game_Player.prototype.isMapPassable;
    Game_Player.prototype.isMapPassable = function(x,y,d) {
        if (!depthConfig() || (this.isInVehicle && this.isInVehicle()))
            return playerMapPass.call(this,x,y,d);
        return passAt(currentManifest(),x,y,d,this);
    };
    const baseMove = Game_CharacterBase.prototype.moveStraight;
    Game_CharacterBase.prototype.moveStraight = function(d) {
        const beforeX=this.x, beforeY=this.y;
        baseMove.call(this,d);
        if (depthConfig() && this.isMovementSucceeded()) commitDepthMove(this,beforeX,beforeY);
    };

    const moveDiagonal = Game_CharacterBase.prototype.moveDiagonally;
    if (moveDiagonal) Game_CharacterBase.prototype.moveDiagonally = function(h,v) {
        const bx=this.x, by=this.y;
        moveDiagonal.call(this,h,v);
        if (depthConfig() && this.isMovementSucceeded()) commitDepthMove(this,bx,by);
    };
    const baseDiagonal = Game_CharacterBase.prototype.canPassDiagonally;
    Game_CharacterBase.prototype.canPassDiagonally = function(x,y,h,v) {
        if (depthConfig()) {
            const nx=$gameMap.roundXWithDirection(x,h), ny=$gameMap.roundYWithDirection(y,v);
            if (stairAt(x,y) || stairAt(nx,ny) || stairAt(nx,y) || stairAt(x,ny)) return false;
        }
        return baseDiagonal.call(this,x,y,h,v);
    };
    function collisionHeight(c, x = c.x, y = c.y) {
        const d = x !== c.x ? (x>c.x?6:4) : y !== c.y ? (y>c.y?2:8) : 0;
        const regionLevel = d ? regionLevelAt(x,y,d) : null;
        if (regionLevel != null) return regionLevel;
        const target = stairAt(x,y), origin = stairAt(c.x,c.y);
        if (target && origin && target.t === origin.t &&
            (origin.progress > 0 && origin.progress < 1 || Math.abs(origin.progress-levelOf(c)) < .01))
            return target.progress;
        return levelOf(c);
    }
    function sameHeight(a,b) { return Math.abs(collisionHeight(a)-collisionHeight(b)) < .01; }
    function sameDestinationHeight(c,e,x,y) { return Math.abs(collisionHeight(c,x,y)-collisionHeight(e)) < .01; }
    const baseEventCollision = Game_CharacterBase.prototype.isCollidedWithEvents;
    Game_CharacterBase.prototype.isCollidedWithEvents = function(x,y) {
        if (!depthConfig()) return baseEventCollision.call(this,x,y);
        return $gameMap.eventsXyNt(x,y).some(e => e !== this && e.isNormalPriority() && sameDestinationHeight(this,e,x,y));
    };
    // MZ overrides this method for events, too.
    const eventCollision = Game_Event.prototype.isCollidedWithEvents;
    Game_Event.prototype.isCollidedWithEvents = function(x,y) {
        if (!depthConfig()) return eventCollision.call(this,x,y);
        return $gameMap.eventsXyNt(x,y).some(e => e !== this && sameDestinationHeight(this,e,x,y));
    };
    const playerCollision = Game_Event.prototype.isCollidedWithPlayerCharacters;
    Game_Event.prototype.isCollidedWithPlayerCharacters = function(x,y) {
        if (!depthConfig()) return playerCollision.call(this,x,y);
        return this.isNormalPriority() && [$gamePlayer, ...$gamePlayer.followers().visibleFollowers()]
            .some(c => c.pos(x,y) && sameDestinationHeight(this,c,x,y));
    };
    const startEvents = Game_Player.prototype.startMapEvent;
    Game_Player.prototype.startMapEvent = function(x,y,triggers,normal) {
        if (!depthConfig()) return startEvents.call(this,x,y,triggers,normal);
        if (!$gameMap.isEventRunning()) {
            for (const e of $gameMap.eventsXy(x,y))
                if (sameHeight(this,e) && e.isTriggerIn(triggers) && e.isNormalPriority() === normal) e.start();
        }
    };
    const eventTouch = Game_Event.prototype.checkEventTriggerTouch;
    Game_Event.prototype.checkEventTriggerTouch = function(x,y) {
        if (depthConfig() && !sameHeight(this,$gamePlayer)) return;
        return eventTouch.call(this,x,y);
    };
    const transfer = Game_Player.prototype.performTransfer;
    Game_Player.prototype.performTransfer = function() {
        const pending = this.isTransferring();
        transfer.call(this);
        if (pending) {
            this._ludoPerspectiveHeight = null;
            this._ludoRegionPosition = regionPositionKey(this);
            const initialRegion = regionLevelAt(this.x,this.y);
            this._ludoDepthLevel = initialRegion == null ? (depthConfig() ? depthConfig().startLevel : 0) : initialRegion;
            for (const follower of ludoFollowers(this)) follower._ludoDepthLevel = this._ludoDepthLevel;
        }
    };
    const syncFollower = Game_Follower.prototype.synchronize;
    Game_Follower.prototype.synchronize = function(x,y,d) {
        syncFollower.call(this,x,y,d); this._ludoDepthLevel = levelOf($gamePlayer);
    };
    const updateCharacter = Sprite_Character.prototype.update;
    Sprite_Character.prototype.update = function() {
        if (this._ludoDepthFactor) {
            this.scale.x /= this._ludoDepthFactor; this.scale.y /= this._ludoDepthFactor;
            this._ludoDepthFactor = 0;
        }
        updateCharacter.call(this);
        if (!depthConfig() || !this._character) return;
        const stair = stairAt(this._character._realX,this._character._realY);
        // The lower landing belongs visually to the stair too: otherwise the
        // elevated stair chunk hides the head before the first movement frame.
        const onStair = stair && (stair.progress > 0 && stair.progress < 1 ||
            Math.abs(stair.progress - levelOf(this._character)) < .01);
        const level = onStair ? 1 : levelOf(this._character);
        const factor = viewScale(level);
        projectSprite(this,level,this.z);
        this._ludoDepthFactor = factor;
    };
    // Click destination belongs to the active walkable plane (which stays 1:1).
    const updateDestination = Sprite_Destination.prototype.update;
    Sprite_Destination.prototype.update = function() {
        updateDestination.call(this);
        if (depthConfig()) this.z = (heightOf($gamePlayer) > 0 ? 10 : 0) + 9;
    };

    // API publica somente-leitura para integrações oficiais compatíveis.
    const LudoMapSystemAPI = Object.freeze({
        version: VERSION,
        currentManifest,
        hasLudoMap() { return !!currentManifest() && hasLudoTag($dataMap); },
        mapScaleX() { const manifest = currentManifest(); return manifest ? mapScaleX(manifest) : 1; },
        mapScaleY() { const manifest = currentManifest(); return manifest ? mapScaleY(manifest) : 1; },
        scrollPixelX,
        scrollPixelY,
        bitmapFor,
        frameIndex,
        blendModeOf,
        setVariation(name, fade = 0) { return setMapVariation(name, fade); },
        animationElapsedMs() { return Math.max(0, performance.now() - state.animationStart); }
    });
    window.LudoMapSystem = LudoMapSystemAPI;


})();
