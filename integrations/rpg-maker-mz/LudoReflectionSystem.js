/*:
 * @target MZ
 * @plugindesc LUDO Reflection System v1.7.0 - reflexos por Camada, Tileset/Autotile e máscara alpha.
 * @author LUDO
 *
 * @param Enabled
 * @text Ativar reflexos
 * @type boolean
 * @default true
 *
 * @param ReflectionRegion
 * @text Regiao refletiva (legado)
 * @type number
 * @min 0
 * @max 255
 * @default 20
 * @desc Fallback para mapas sem configuracao exportada pelo LUDO Editor.
 *
 * @param Opacity
 * @text Opacidade (legado)
 * @type number
 * @min 0
 * @max 255
 * @default 120
 *
 * @param OffsetY
 * @text Ajuste vertical
 * @type number
 * @min -96
 * @max 96
 * @default 0
 *
 * @param ReflectEvents
 * @text Refletir eventos
 * @type boolean
 * @default true
 *
 * @param ReflectFollowers
 * @text Refletir seguidores
 * @type boolean
 * @default true
 *
 * @param ReflectObjects
 * @text Refletir objetos LUDO marcados
 * @type boolean
 * @default true
 *
 * @param ReflectLights
 * @text Refletir luzes dinamicas
 * @type boolean
 * @default true
 *
 * @param FadeInFrames
 * @text Entrada suave
 * @type number
 * @min 0
 * @max 120
 * @default 8
 *
 * @param FadeOutFrames
 * @text Saida suave
 * @type number
 * @min 0
 * @max 120
 * @default 10
 *
 * @param VerticalFadeStrength
 * @text Fade vertical (legado)
 * @type number
 * @min 0
 * @max 100
 * @default 65
 *
 * @param EdgeSoftness
 * @text Suavizar borda (legado)
 * @type number
 * @min 0
 * @max 12
 * @default 3
 *
 * @param TintColor
 * @text Cor (legado)
 * @type string
 * @default #FFFFFF
 *
 * @param MaskMode
 * @text Mascara (legado)
 * @type select
 * @option Regiao
 * @value region
 * @option Alpha do Tile / Autotile
 * @value tileAlpha
 * @option Regiao + Alpha do Tile
 * @value regionTileAlpha
 * @default region
 *
 * @param TileAlphaThreshold
 * @text Limiar de alpha do tile
 * @type number
 * @min 0
 * @max 254
 * @default 4
 * @desc Pixels com alpha igual ou menor que este valor nao entram na mascara.
 *
 * @param TileMaskLayer
 * @text Camada usada na mascara
 * @type select
 * @option Automatico - tile mais alto
 * @value -1
 * @option Camada 1
 * @value 0
 * @option Camada 2
 * @value 1
 * @option Camada 3
 * @value 2
 * @option Camada 4
 * @value 3
 * @default -1
 *
 * @param RenderMode
 * @text Renderizacao padrao
 * @type select
 * @option Automatico
 * @value auto
 * @option Shader
 * @value shader
 * @option Faixas compativeis
 * @value strips
 * @default auto
 *
 * @param Quality
 * @text Qualidade padrao
 * @type select
 * @option Performance
 * @value performance
 * @option Equilibrado
 * @value balanced
 * @option Alta qualidade
 * @value quality
 * @default balanced
 *
 * @param MaxReflections
 * @text Maximo visivel
 * @type number
 * @min 1
 * @max 128
 * @default 32
 *
 * @param CullingMargin
 * @text Margem de culling
 * @type number
 * @min 0
 * @max 1024
 * @default 160
 *
 * @param WeatherIntegration
 * @text Integrar clima LUDO
 * @type boolean
 * @default true
 *
 * @param WavesEnabled
 * @text Ondulacao no fallback
 * @type boolean
 * @default true
 *
 * @param WavePreset
 * @text Ondulacao fallback
 * @type select
 * @option Suave
 * @value soft
 * @option Forte
 * @value strong
 * @option Personalizado
 * @value custom
 * @default soft
 *
 * @param WaveIntensityX
 * @text Intensidade horizontal
 * @type number
 * @decimals 1
 * @min 0
 * @max 12
 * @default 2.0
 *
 * @param WaveIntensityY
 * @text Intensidade vertical
 * @type number
 * @decimals 1
 * @min 0
 * @max 3
 * @default 0.2
 *
 * @param WaveSpeed
 * @text Velocidade
 * @type number
 * @min 0
 * @max 100
 * @default 15
 *
 * @param WaveFrequency
 * @text Frequencia
 * @type number
 * @min 1
 * @max 100
 * @default 35
 *
 * @command Refresh
 * @text Atualizar reflexos
 * @desc Rele o manifesto e recria as camadas de reflexo do mapa atual.
 *
 * @help
 * LudoReflectionSystem.js v1.6.0
 *
 * Sistema consolidado de reflexos para mapas exportados pelo LUDO Editor.
 *
 * RECOMENDADO
 * - Use LudoMapSystem.js v0.8.0 ou superior.
 * - Para agua/pocas, configure o Tile ou Autotile no Gerenciador de Tilesets.
 * - Use RPG Maker MZ > Reflexos do mapa para opcoes globais e legado por Region.
 * - Marque objetos individuais no Inspetor quando quiser refleti-los.
 *
 * OFFSET DE SPRITE
 * - O Offset base é salvo por mapa em Mapa > Reflexos.
 * - Para um Evento específico use <ReflectionOffsetY: N> na Nota ou em um
 *   Comentário da página. O valor individual é somado ao Offset base.
 * - Via script, Game_CharacterBase possui setReflectionOffsetY(valor).
 *
 * BLUR POR TILE / AUTOTILE
 * - Configure no Gerenciador de Tilesets > Efeitos.
 * - Modos: desativado, horizontal, vertical ou ambos.
 * - Intensidade: 0 a 24 px. O filtro só é ativado quando necessário.
 *
 * PRESETS NATIVOS
 * - Agua parada
 * - Agua suja
 * - Agua agitada
 * - Poca
 * - Piso molhado
 * - Espelho / superficie limpa
 *
 * MASCARAS DE SUPERFICIE
 * - Regiao: comportamento classico, ocupa a celula inteira da Region.
 * - Alpha do Tile: uma Region funciona como etiqueta de aprendizado. O sistema
 *   identifica o Tile ou a familia do Autotile nessa Region e passa a usar o
 *   alpha desse grafico como mascara em todas as ocorrencias iguais do mapa.
 * - Regiao + Alpha: recomendado para pocas. A Region escolhe o preset e o
 *   alpha do Tile/Autotile recorta o reflexo pixel a pixel dentro da celula.
 *
 * Autotiles usam a forma real montada pelo RPG Maker MZ. O cache e feito por
 * tileset + tileId, portanto o alpha nao e relido a cada frame.
 *
 * O modo Automatico usa shader quando WebGL/PIXI.Filter esta disponivel e
 * cai para faixas horizontais quando necessario. O modo antigo continua
 * disponivel como fallback de compatibilidade.
 *
 * CEU / AMBIENTE
 * O LUDO Editor pode exportar uma imagem de ceu/ambiente diferente para cada
 * mapa. Ela e desenhada atras dos personagens/objetos refletidos e recortada
 * pela mesma mascara alpha das superficies. O arquivo e copiado automaticamente
 * para img/ludoMaps; o runtime nao depende do caminho original do computador.
 * A opacidade configurada no Tile/Autotile multiplica essa mascara, portanto
 * afeta o ceu e todos os outros elementos refletidos naquela superficie.
 *
 * EVENTOS
 * Para impedir o reflexo de uma pagina de evento use um Comentario:
 * <NoReflection>
 * <ReflectionOffsetY: 8>
 * Ajusta somente aquele Evento quando o pé visual do sprite não coincide com
 * o ponto de contato. O valor é somado ao Offset base configurado no mapa.
 * A tag tambem pode ser colocada na Nota do evento.
 *
 * CLIMA
 * Se LudoWeatherSystem estiver instalado, chuva e tempestade aumentam de
 * forma automatica a ondulacao. O vento influencia o deslocamento lateral.
 *
 * LUZES
 * Com LudoLightEngine, luzes dinamicas de posicao/personagem recebem um
 * brilho refletido simples, limitado pela mesma mascara das superficies.
 *
 * PERFORMANCE
 * O sistema usa culling por viewport, limite de reflexos, mascara em cache,
 * texturas compartilhadas, sprites reutilizados e qualidade configuravel.
 * O plugin nao altera js/plugins.js automaticamente.
 */

(() => {
    "use strict";

    const PLUGIN_NAME = "LudoReflectionSystem";
    const VERSION = "1.7.0";
    const parameters = PluginManager.parameters(PLUGIN_NAME);
    const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
    const boolParam = (name, fallback = true) => parameters[name] == null ? fallback : parameters[name] !== "false";

    function normalizeMaskMode(value, fallback = "region") {
        const key = String(value == null ? "" : value).trim().toLowerCase();
        if (key === "tilealpha" || key === "tile") return "tileAlpha";
        if (key === "regiontilealpha" || key === "region+tile" || key === "hybrid") return "regionTileAlpha";
        return fallback;
    }

    function parseColor(value, fallback = 0xffffff) {
        const text = String(value == null ? "" : value).trim();
        let parsed = NaN;
        if (/^#[0-9a-f]{6}$/i.test(text)) parsed = Number.parseInt(text.slice(1), 16);
        else if (/^0x[0-9a-f]{6}$/i.test(text)) parsed = Number.parseInt(text.slice(2), 16);
        else if (/^[0-9a-f]{6}$/i.test(text)) parsed = Number.parseInt(text, 16);
        else if (/^\d+$/.test(text)) parsed = Number(text);
        return Number.isFinite(parsed) ? clamp(Math.floor(parsed), 0, 0xffffff) : fallback;
    }

    const legacy = {
        enabled: boolParam("Enabled", true),
        regionId: clamp(Number(parameters.ReflectionRegion) || 0, 0, 255),
        opacity: clamp(Number(parameters.Opacity) || 120, 0, 255),
        offsetY: clamp(Number(parameters.OffsetY) || 0, -96, 96),
        reflectEvents: boolParam("ReflectEvents", true),
        reflectFollowers: boolParam("ReflectFollowers", true),
        reflectObjects: boolParam("ReflectObjects", true),
        reflectLights: boolParam("ReflectLights", true),
        fadeInFrames: clamp(Number(parameters.FadeInFrames) || 0, 0, 120),
        fadeOutFrames: clamp(Number(parameters.FadeOutFrames) || 0, 0, 120),
        verticalFadeStrength: clamp(Number(parameters.VerticalFadeStrength) || 0, 0, 100),
        edgeSoftness: clamp(Number(parameters.EdgeSoftness) || 0, 0, 12),
        tint: parseColor(parameters.TintColor, 0xffffff),
        maskMode: normalizeMaskMode(parameters.MaskMode, "region"),
        tileAlphaThreshold: clamp(Number(parameters.TileAlphaThreshold) || 0, 0, 254),
        tileMaskLayer: clamp(Number(parameters.TileMaskLayer == null ? -1 : parameters.TileMaskLayer), -1, 3),
        renderMode: String(parameters.RenderMode || "auto").toLowerCase(),
        quality: String(parameters.Quality || "balanced").toLowerCase(),
        maxReflections: clamp(Number(parameters.MaxReflections) || 32, 1, 128),
        cullingMargin: clamp(Number(parameters.CullingMargin) || 160, 0, 1024),
        weatherIntegration: boolParam("WeatherIntegration", true),
        wavesEnabled: boolParam("WavesEnabled", true),
        wavePreset: String(parameters.WavePreset || "soft").toLowerCase(),
        waveIntensityX: clamp(Number(parameters.WaveIntensityX) || 0, 0, 12),
        waveIntensityY: clamp(Number(parameters.WaveIntensityY) || 0, 0, 3),
        waveSpeed: clamp(Number(parameters.WaveSpeed) || 0, 0, 100),
        waveFrequency: clamp(Number(parameters.WaveFrequency) || 1, 1, 100)
    };

    const PRESETS = Object.freeze({
        still:  Object.freeze({ id:"still",  opacity:132, tint:0xdcefff, waveX:1.25, waveY:0.10, speed:0.85, frequency:2.8, verticalFade:62, edge:2 }),
        dirty:  Object.freeze({ id:"dirty",  opacity:112, tint:0x9aaa86, waveX:1.65, waveY:0.14, speed:0.80, frequency:3.1, verticalFade:72, edge:3 }),
        rough:  Object.freeze({ id:"rough",  opacity:122, tint:0xc9dced, waveX:4.80, waveY:0.42, speed:1.75, frequency:4.7, verticalFade:76, edge:4 }),
        puddle: Object.freeze({ id:"puddle", opacity:148, tint:0xd6e1e7, waveX:0.75, waveY:0.06, speed:0.72, frequency:2.2, verticalFade:48, edge:2 }),
        wet:    Object.freeze({ id:"wet",    opacity:92,  tint:0xc8d1d6, waveX:0.28, waveY:0.02, speed:0.45, frequency:1.5, verticalFade:28, edge:1 }),
        mirror: Object.freeze({ id:"mirror", opacity:205, tint:0xffffff, waveX:0.08, waveY:0.00, speed:0.25, frequency:1.1, verticalFade:5,  edge:1 })
    });

    const QUALITY = Object.freeze({
        performance: Object.freeze({ stripes:6,  edgeScale:0.65, lightLimit:6 }),
        balanced:    Object.freeze({ stripes:12, edgeScale:1.00, lightLimit:10 }),
        quality:     Object.freeze({ stripes:18, edgeScale:1.35, lightLimit:16 })
    });

    let runtimeDirty = true;
    let runtimeCache = null;
    let runtimeMapId = -1;
    let globalClock = 0;
    const gradientCache = new Map();
    let weatherCacheFrame = -1;
    let weatherCache = null;
    const tileGraphicCache = new Map();
    const watchedTilesetBitmaps = new WeakSet();
    let tileAlphaRevision = 0;

    function mapApi() {
        return window.LudoMapSystem && typeof window.LudoMapSystem.currentManifest === "function"
            ? window.LudoMapSystem : null;
    }

    function currentManifest() {
        const api = mapApi();
        return api ? api.currentManifest() : null;
    }

    function legacyWaveStyle() {
        if (!legacy.wavesEnabled) return { waveX:0, waveY:0, speed:0, frequency:1 };
        if (legacy.wavePreset === "strong") return { waveX:4.5, waveY:0.45, speed:1.65, frequency:4.5 };
        if (legacy.wavePreset === "custom") {
            return {
                waveX:legacy.waveIntensityX,
                waveY:legacy.waveIntensityY,
                speed:legacy.waveSpeed / 15,
                frequency:Math.max(0.5, legacy.waveFrequency / 10)
            };
        }
        return { waveX:2.0, waveY:0.15, speed:1.0, frequency:3.1 };
    }

    function styleFromSurface(raw) {
        const presetId = String(raw && raw.preset || "still").toLowerCase();
        const preset = PRESETS[presetId] || PRESETS.still;
        return {
            id:preset.id,
            opacity:clamp(Number(raw && raw.opacity != null ? raw.opacity : preset.opacity), 0, 255),
            tint:parseColor(raw && raw.tint, preset.tint),
            waveX:clamp(Number(raw && raw.waveX != null ? raw.waveX : preset.waveX), 0, 16),
            waveY:clamp(Number(raw && raw.waveY != null ? raw.waveY : preset.waveY), 0, 4),
            speed:clamp(Number(raw && raw.speed != null ? raw.speed : preset.speed), 0, 8),
            frequency:clamp(Number(raw && raw.frequency != null ? raw.frequency : preset.frequency), 0.2, 12),
            verticalFade:clamp(Number(raw && raw.verticalFade != null ? raw.verticalFade : preset.verticalFade), 0, 100),
            edge:clamp(Number(raw && raw.edge != null ? raw.edge : preset.edge), 0, 12),
            blurMode:["horizontal","vertical","both"].includes(String(raw && raw.blurMode || "").toLowerCase())
                ? String(raw.blurMode).toLowerCase() : "none",
            blurStrength:clamp(Number(raw && raw.blurStrength != null ? raw.blurStrength : 0), 0, 24)
        };
    }

    function fallbackSurface() {
        const w = legacyWaveStyle();
        return {
            regionId:legacy.regionId,
            preset:"legacy",
            maskMode:legacy.maskMode,
            style:{
                id:"legacy", opacity:legacy.opacity, tint:legacy.tint,
                waveX:w.waveX, waveY:w.waveY, speed:w.speed, frequency:w.frequency,
                verticalFade:legacy.verticalFadeStrength, edge:legacy.edgeSoftness, blurMode:"none", blurStrength:0
            }
        };
    }

    function resolveRuntime() {
        const mapId = $gameMap && typeof $gameMap.mapId === "function" ? $gameMap.mapId() : 0;
        if (!runtimeDirty && runtimeCache && runtimeMapId === mapId) return runtimeCache;

        const manifest = currentManifest();
        const exported = manifest && manifest.reflection && typeof manifest.reflection === "object"
            ? manifest.reflection : null;
        const surfaces = [];
        const byRegion = new Map();
        const byPreset = new Map();
        const tileCellSurfaces = new Map();
        const tileMaskPieces = [];
        const rawTileSurfaces = exported && Array.isArray(exported.tileSurfaces) ? exported.tileSurfaces : [];
        for (const group of rawTileSurfaces) {
            const presetId = String(group && group.preset || "puddle");
            const surface = { regionId:0, preset:presetId, maskMode:"exportedMask", style:styleFromSurface(group) };
            surfaces.push(surface);
            if (!byPreset.has(presetId)) byPreset.set(presetId, surface);
            for (const cell of (Array.isArray(group && group.cells) ? group.cells : [])) {
                const key = String(cell || ""); if (key) tileCellSurfaces.set(key, surface);
            }
            for (const mask of (Array.isArray(group && group.masks) ? group.masks : [])) {
                if (!mask || !mask.file) continue;
                tileMaskPieces.push({
                    file:String(mask.file), x:Number(mask.x)||0, y:Number(mask.y)||0,
                    width:Math.max(1,Number(mask.width)||1), height:Math.max(1,Number(mask.height)||1), surface
                });
            }
        }
        // Quando o mapa possui superficies vindas do Gerenciador de Tilesets,
        // elas sao a fonte primaria. Regions antigas so entram se nao houver
        // bindings por recurso (ou se um manifesto futuro pedir explicitamente).
        // Isso evita que uma Region retangular volte a preencher as bordas
        // transparentes de uma poca/autotile e anule a mascara por alpha.
        const useRegionSurfaces = rawTileSurfaces.length === 0 || !!(exported && exported.useRegionsWithTileset);
        const rawSurfaces = useRegionSurfaces && exported && Array.isArray(exported.surfaces) ? exported.surfaces : [];
        for (const raw of rawSurfaces) {
            const regionId = clamp(Number(raw && raw.regionId) || 0, 0, 255);
            if (regionId <= 0 || byRegion.has(regionId)) continue;
            const presetId = String(raw.preset || "still");
            const surface = {
                regionId, preset:presetId,
                maskMode:normalizeMaskMode(raw && raw.maskMode, "region"),
                style:styleFromSurface(raw)
            };
            surfaces.push(surface); byRegion.set(regionId, surface);
            if (!byPreset.has(presetId)) byPreset.set(presetId, surface);
        }
        if (surfaces.length === 0 && legacy.regionId > 0) {
            const surface = fallbackSurface();
            surfaces.push(surface); byRegion.set(surface.regionId, surface); byPreset.set(surface.preset,surface);
        }

        const rawEnvironment = exported && exported.environment && typeof exported.environment === "object"
            ? exported.environment : null;
        const environmentBlend = String(rawEnvironment && rawEnvironment.blendMode || "normal").toLowerCase();
        const environmentFit = String(rawEnvironment && rawEnvironment.fit || "cover").toLowerCase();
        const environment = rawEnvironment && rawEnvironment.enabled !== false && rawEnvironment.file
            ? {
                enabled:true,
                file:String(rawEnvironment.file),
                opacity:clamp(Number(rawEnvironment.opacity == null ? 112 : rawEnvironment.opacity), 0, 255),
                fit:environmentFit === "stretch" ? "stretch" : "cover",
                flipY:rawEnvironment.flipY !== false,
                blendMode:["normal","screen","add"].includes(environmentBlend) ? environmentBlend : "normal"
            }
            : { enabled:false, file:"", opacity:0, fit:"cover", flipY:true, blendMode:"normal" };

        const qualityKey = String(exported && exported.quality || legacy.quality).toLowerCase();
        const quality = QUALITY[qualityKey] ? qualityKey : "balanced";
        const renderKey = String(exported && exported.renderMode || legacy.renderMode).toLowerCase();
        const renderMode = ["auto", "shader", "strips"].includes(renderKey) ? renderKey : "auto";
        const objects = exported && Array.isArray(exported.objects) ? exported.objects : [];
        const runtime = {
            mapId,
            enabled:(exported ? exported.enabled !== false : legacy.enabled) && surfaces.length > 0,
            reflectEvents:exported && exported.reflectEvents != null ? !!exported.reflectEvents : legacy.reflectEvents,
            reflectFollowers:exported && exported.reflectFollowers != null ? !!exported.reflectFollowers : legacy.reflectFollowers,
            reflectObjects:exported && exported.reflectObjects != null ? !!exported.reflectObjects : legacy.reflectObjects,
            reflectLights:exported && exported.reflectLights != null ? !!exported.reflectLights : legacy.reflectLights,
            weatherIntegration:exported && exported.weatherIntegration != null ? !!exported.weatherIntegration : legacy.weatherIntegration,
            quality,
            profile:QUALITY[quality],
            renderMode,
            maxReflections:clamp(Number(exported && exported.maxReflections) || legacy.maxReflections, 1, 128),
            cullingMargin:clamp(Number(exported && exported.cullingMargin) || legacy.cullingMargin, 0, 1024),
            tileAlphaThreshold:clamp(Number(exported && exported.tileAlphaThreshold != null ? exported.tileAlphaThreshold : legacy.tileAlphaThreshold), 0, 254),
            tileMaskLayer:clamp(Number(exported && exported.tileMaskLayer != null ? exported.tileMaskLayer : legacy.tileMaskLayer), -1, 3),
            offsetY:clamp(Number(exported && exported.offsetY != null ? exported.offsetY : legacy.offsetY), -96, 96),
            fadeInFrames:legacy.fadeInFrames,
            fadeOutFrames:legacy.fadeOutFrames,
            surfaces,
            byRegion,
            byPreset,
            tileCellSurfaces,
            tileMaskPieces,
            objects,
            environment,
            manifest
        };
        runtime.tileBindings = null;
        runtime.tileBindingConflicts = null;
        runtime.edgeSoftness = surfaces.reduce((m, s) => Math.max(m, s.style.edge), 0) * runtime.profile.edgeScale;
        runtimeDirty = false;
        runtimeCache = runtime;
        runtimeMapId = mapId;
        return runtime;
    }

    function shaderSupported() {
        if (typeof PIXI === "undefined" || typeof PIXI.Filter !== "function") return false;
        const renderer = Graphics && Graphics.app && Graphics.app.renderer ? Graphics.app.renderer : null;
        if (!renderer) return true;
        if (PIXI.RENDERER_TYPE && renderer.type != null) return renderer.type === PIXI.RENDERER_TYPE.WEBGL;
        return true;
    }

    function effectiveRenderMode(runtime) {
        if (runtime.renderMode === "strips") return "strips";
        if (runtime.renderMode === "shader") return shaderSupported() ? "shader" : "strips";
        return shaderSupported() ? "shader" : "strips";
    }

    function updateBitmapTexture(bitmap) {
        if (!bitmap) return;
        const base = bitmap.baseTexture || bitmap._baseTexture;
        if (base && typeof base.update === "function") base.update();
    }

    function gradientBitmap(strength) {
        const key = clamp(Math.round(strength), 0, 100);
        if (gradientCache.has(key)) return gradientCache.get(key);
        const bitmap = new Bitmap(8, 256);
        const ctx = bitmap.context || bitmap._context;
        if (ctx) {
            ctx.clearRect(0, 0, 8, 256);
            const endAlpha = 1 - key / 100;
            const gradient = ctx.createLinearGradient(0, 0, 0, 256);
            gradient.addColorStop(0, "rgba(255,255,255,1)");
            gradient.addColorStop(0.10, "rgba(255,255,255,1)");
            gradient.addColorStop(1, `rgba(255,255,255,${endAlpha})`);
            ctx.fillStyle = gradient;
            ctx.fillRect(0, 0, 8, 256);
            updateBitmapTexture(bitmap);
        }
        gradientCache.set(key, bitmap);
        return bitmap;
    }

    const SHADER_FRAGMENT = `
        varying vec2 vTextureCoord;
        uniform sampler2D uSampler;
        uniform float uTime;
        uniform float uWaveX;
        uniform float uWaveY;
        uniform float uFrequency;
        uniform float uSpeed;
        uniform float uWind;
        uniform float uBlurX;
        uniform float uBlurY;
        void main(void) {
            vec2 uv = vTextureCoord;
            float phase = uv.y * 6.28318530718 * uFrequency + uTime * uSpeed + uWind;
            float a = sin(phase);
            float b = sin(phase * 0.57 + 1.37);
            uv.x += (a * 0.72 + b * 0.28) * uWaveX;
            uv.y += sin(phase * 0.81 + 0.65) * uWaveY;
            uv = clamp(uv, vec2(0.001), vec2(0.999));

            vec4 color = texture2D(uSampler, uv) * 0.40;
            float weight = 0.40;
            if (uBlurX > 0.000001) {
                color += texture2D(uSampler, clamp(uv + vec2(uBlurX, 0.0), vec2(0.001), vec2(0.999))) * 0.20;
                color += texture2D(uSampler, clamp(uv - vec2(uBlurX, 0.0), vec2(0.001), vec2(0.999))) * 0.20;
                color += texture2D(uSampler, clamp(uv + vec2(uBlurX * 2.0, 0.0), vec2(0.001), vec2(0.999))) * 0.10;
                color += texture2D(uSampler, clamp(uv - vec2(uBlurX * 2.0, 0.0), vec2(0.001), vec2(0.999))) * 0.10;
                weight += 0.60;
            }
            if (uBlurY > 0.000001) {
                color += texture2D(uSampler, clamp(uv + vec2(0.0, uBlurY), vec2(0.001), vec2(0.999))) * 0.20;
                color += texture2D(uSampler, clamp(uv - vec2(0.0, uBlurY), vec2(0.001), vec2(0.999))) * 0.20;
                color += texture2D(uSampler, clamp(uv + vec2(0.0, uBlurY * 2.0), vec2(0.001), vec2(0.999))) * 0.10;
                color += texture2D(uSampler, clamp(uv - vec2(0.0, uBlurY * 2.0), vec2(0.001), vec2(0.999))) * 0.10;
                weight += 0.60;
            }
            gl_FragColor = color / max(weight, 0.001);
        }
    `;
    function createShaderFilter() {
        if (!shaderSupported()) return null;
        try {
            const filter = new PIXI.Filter(null, SHADER_FRAGMENT, {
                uTime:0, uWaveX:0, uWaveY:0, uFrequency:2.5, uSpeed:1, uWind:0, uBlurX:0, uBlurY:0
            });
            filter.padding = 8;
            return filter;
        } catch (error) {
            console.warn(`[${PLUGIN_NAME}] Shader indisponivel; usando fallback em faixas.`, error);
            return null;
        }
    }

    function makeReflectionEntry(kind, runtime) {
        const container = new PIXI.Container();
        container.visible = false;
        container.z = 2.9;
        container._ludoReflectionContainer = true;

        const inner = new PIXI.Container();
        container.addChild(inner);
        const content = new PIXI.Container();
        inner.addChild(content);

        const sprite = new Sprite();
        sprite.anchor.set(0.5, 1);
        sprite.visible = false;
        content.addChild(sprite);

        const waveContainer = new PIXI.Container();
        waveContainer.visible = false;
        content.addChild(waveContainer);

        const objectVisual = new PIXI.Container();
        objectVisual.visible = false;
        content.addChild(objectVisual);

        const fadeMask = new Sprite(gradientBitmap(legacy.verticalFadeStrength));
        fadeMask.anchor.set(0.5, 0);
        inner.addChild(fadeMask);
        content.mask = fadeMask;

        const mode = effectiveRenderMode(runtime);
        const shader = mode === "shader" ? createShaderFilter() : null;
        const actualMode = shader ? "shader" : "strips";
        if (shader) content.filters = [shader];

        return {
            kind, container, inner, content, sprite, waveContainer, objectVisual, fadeMask,
            shader, blurFilter:null, mode:actualMode, waveStripes:[], objectParts:[], objectData:null,
            alpha:0, targetAlpha:0, surface:null, width:1, height:1
        };
    }

    function ensureWaveStripes(entry, count) {
        if (entry.kind !== "character") return;
        while (entry.waveStripes.length < count) {
            const stripe = new Sprite();
            stripe.anchor.set(0.5, 1);
            stripe.visible = false;
            entry.waveContainer.addChild(stripe);
            entry.waveStripes.push(stripe);
        }
        for (let i = 0; i < entry.waveStripes.length; ++i) entry.waveStripes[i].visible = i < count;
    }

    function createCanvas(width, height) {
        if (typeof document === "undefined" || !document.createElement) return null;
        const canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        return canvas;
    }

    function makeRegionMask() {
        const width = Math.max(1, Math.ceil(Graphics.width));
        const height = Math.max(1, Math.ceil(Graphics.height));
        const bitmap = new Bitmap(width, height);
        const mask = new Sprite(bitmap);
        mask.x = 0;
        mask.y = 0;
        mask.z = 0;
        mask._ludoReflectionMask = true;
        mask._ludoMaskMapId = -1;
        mask._ludoMaskDisplayX = NaN;
        mask._ludoMaskDisplayY = NaN;
        mask._ludoMaskWidth = width;
        mask._ludoMaskHeight = height;
        mask._ludoBinaryCanvas = createCanvas(width, height);
        mask._ludoRuntimeSignature = "";
        return mask;
    }

    function ensureMaskSize(mask) {
        const width = Math.max(1, Math.ceil(Graphics.width));
        const height = Math.max(1, Math.ceil(Graphics.height));
        if (mask._ludoMaskWidth === width && mask._ludoMaskHeight === height) return;
        mask.bitmap = new Bitmap(width, height);
        mask._ludoMaskWidth = width;
        mask._ludoMaskHeight = height;
        mask._ludoBinaryCanvas = createCanvas(width, height);
        mask._ludoMaskMapId = -1;
    }

    function runtimeSignature(runtime) {
        return runtime.surfaces.map(s => `${s.regionId}:${s.preset}:${s.maskMode}`).join("|") +
            `:${runtime.edgeSoftness.toFixed(2)}:${runtime.tileAlphaThreshold}:${runtime.tileMaskLayer}:${runtime.tileMaskPieces ? runtime.tileMaskPieces.length : 0}:${tileAlphaRevision}`;
    }

    function mapTileId(x, y, z) {
        if ($gameMap && typeof $gameMap.tileId === "function") return Number($gameMap.tileId(x, y, z)) || 0;
        const map = typeof $dataMap !== "undefined" ? $dataMap : null;
        if (!map || !Array.isArray(map.data)) return 0;
        const w = Number(map.width || 0), h = Number(map.height || 0);
        if (x < 0 || y < 0 || x >= w || y >= h || z < 0 || z > 3) return 0;
        return Number(map.data[(z * h + y) * w + x]) || 0;
    }

    function maskTileIdAt(runtime, x, y) {
        const layer = runtime ? runtime.tileMaskLayer : -1;
        if (layer >= 0 && layer <= 3) return mapTileId(x, y, layer);
        for (let z = 3; z >= 0; --z) {
            const tileId = mapTileId(x, y, z);
            if (tileId > 0) return tileId;
        }
        return 0;
    }

    function autotileKind(tileId) {
        return tileId >= 2048 ? Math.floor((tileId - 2048) / 48) : -1;
    }

    function tileBindingKey(tileId) {
        if (!(tileId > 0)) return "";
        const kind = autotileKind(tileId);
        return kind >= 0 ? `a:${kind}` : `t:${tileId}`;
    }

    function ensureTileBindings(runtime) {
        if (!runtime || runtime.tileBindings) return runtime ? runtime.tileBindings : new Map();
        const bindings = new Map();
        const conflicts = new Set();
        const needsBinding = runtime.surfaces.some(surface => surface.maskMode === "tileAlpha");
        if (needsBinding) {
            const width = Math.max(0, Number($gameMap.width ? $gameMap.width() : 0));
            const height = Math.max(0, Number($gameMap.height ? $gameMap.height() : 0));
            for (let y = 0; y < height; ++y) {
                for (let x = 0; x < width; ++x) {
                    const surface = runtime.byRegion.get($gameMap.regionId(x, y));
                    if (!surface || surface.maskMode !== "tileAlpha") continue;
                    const key = tileBindingKey(maskTileIdAt(runtime, x, y));
                    if (!key) continue;
                    if (!bindings.has(key)) bindings.set(key, surface);
                    else if (bindings.get(key) !== surface) conflicts.add(key);
                }
            }
        }
        runtime.tileBindings = bindings;
        runtime.tileBindingConflicts = conflicts;
        if (conflicts.size > 0) {
            console.warn(`[${PLUGIN_NAME}] O mesmo Tile/Autotile foi etiquetado para presets diferentes: ${Array.from(conflicts).join(", ")}. A primeira configuracao sera usada.`);
        }
        return bindings;
    }

    function surfaceAtMapTile(runtime, x, y) {
        if (!runtime || !runtime.enabled || !$gameMap.isValid(x, y)) return null;
        const exportedSurface = runtime.tileCellSurfaces && runtime.tileCellSurfaces.get(`${x},${y}`);
        if (exportedSurface) return exportedSurface;
        const regionSurface = runtime.byRegion.get($gameMap.regionId(x, y));
        if (regionSurface && regionSurface.maskMode !== "tileAlpha") return regionSurface;
        const tileId = maskTileIdAt(runtime, x, y);
        const key = tileBindingKey(tileId);
        if (!key) return regionSurface || null;
        const bound = ensureTileBindings(runtime).get(key);
        return bound || regionSurface || null;
    }

    function tilesetNames() {
        const tileset = $gameMap && typeof $gameMap.tileset === "function" ? $gameMap.tileset() : null;
        return tileset && Array.isArray(tileset.tilesetNames) ? tileset.tilesetNames : [];
    }

    function watchTilesetBitmap(bitmap) {
        if (!bitmap || watchedTilesetBitmaps.has(bitmap)) return;
        watchedTilesetBitmaps.add(bitmap);
        if (typeof bitmap.addLoadListener === "function") {
            bitmap.addLoadListener(() => { tileAlphaRevision++; });
        }
    }

    function loadTilesetBitmap(index) {
        const names = tilesetNames();
        const name = String(names[index] || "");
        if (!name || typeof ImageManager === "undefined") return null;
        let bitmap = null;
        if (typeof ImageManager.loadTileset === "function") bitmap = ImageManager.loadTileset(name);
        else if (typeof ImageManager.loadBitmap === "function") bitmap = ImageManager.loadBitmap("img/tilesets/", name);
        watchTilesetBitmap(bitmap);
        return bitmap;
    }

    const exportedMaskBitmapCache = new Map();
    function loadExportedMaskBitmap(file) {
        const key=String(file||""); if(!key) return null;
        if(exportedMaskBitmapCache.has(key)) return exportedMaskBitmapCache.get(key);
        const name=key.replace(/\.png$/i,"");
        const bitmap = typeof ImageManager !== "undefined" && typeof ImageManager.loadBitmap === "function"
            ? ImageManager.loadBitmap("img/ludoMaps/", name) : null;
        if(bitmap && typeof bitmap.addLoadListener === "function") bitmap.addLoadListener(()=>{ tileAlphaRevision++; });
        exportedMaskBitmapCache.set(key,bitmap); return bitmap;
    }

    const environmentBitmapCache = new Map();
    function loadEnvironmentBitmap(file) {
        const key = String(file || "");
        if (!key) return null;
        if (environmentBitmapCache.has(key)) return environmentBitmapCache.get(key);
        const name = key.replace(/\.png$/i, "");
        const bitmap = typeof ImageManager !== "undefined" && typeof ImageManager.loadBitmap === "function"
            ? ImageManager.loadBitmap("img/ludoMaps/", name) : null;
        environmentBitmapCache.set(key, bitmap);
        return bitmap;
    }

    function bitmapDrawable(bitmap) {
        if (!bitmap) return null;
        if (typeof bitmap.isReady === "function" && !bitmap.isReady()) return null;
        return bitmap._canvas || bitmap.canvas || bitmap._image || null;
    }

    function drawSourceRect(ctx, bitmapIndex, sx, sy, sw, sh, dx, dy, dw, dh) {
        const bitmap = loadTilesetBitmap(bitmapIndex);
        const source = bitmapDrawable(bitmap);
        if (!source) return false;
        try { ctx.drawImage(source, sx, sy, sw, sh, dx, dy, dw, dh); return true; }
        catch (_) { return false; }
    }

    function tileGraphicCanvas(tileId, tileWidth, tileHeight, threshold) {
        tileId = Number(tileId) || 0;
        if (tileId <= 0) return null;
        const tilesetId = $gameMap && typeof $gameMap.tilesetId === "function" ? $gameMap.tilesetId() : 0;
        const key = `${tilesetId}:${tileWidth}x${tileHeight}:${tileId}:${threshold}`;
        if (tileGraphicCache.has(key)) return tileGraphicCache.get(key);
        const canvas = createCanvas(tileWidth, tileHeight);
        const ctx = canvas && canvas.getContext ? canvas.getContext("2d") : null;
        if (!ctx) return null;
        ctx.clearRect(0, 0, tileWidth, tileHeight);
        let drawn = false;

        if (tileId < 2048) {
            const setNumber = tileId >= 1536 ? 4 : 5 + Math.floor(tileId / 256);
            const sx = ((Math.floor(tileId / 128) % 2) * 8 + tileId % 8) * tileWidth;
            const sy = (Math.floor((tileId % 256) / 8) % 16) * tileHeight;
            drawn = drawSourceRect(ctx, setNumber, sx, sy, tileWidth, tileHeight, 0, 0, tileWidth, tileHeight);
        } else {
            const kind = Math.floor((tileId - 2048) / 48);
            const shape = (tileId - 2048) % 48;
            const tx = kind % 8;
            const ty = Math.floor(kind / 8);
            let setNumber = -1, bx = 0, by = 0;
            let table = typeof Tilemap !== "undefined" ? Tilemap.FLOOR_AUTOTILE_TABLE : null;
            if (tileId < 2816) {
                setNumber = 0;
                if (kind < 4) { bx = kind < 2 ? 0 : 6; by = (kind % 2) * 3; }
                else {
                    bx = Math.floor(tx / 4) * 8;
                    by = ty * 6 + (Math.floor(tx / 2) % 2) * 3;
                    if (kind % 2) { bx += 6; table = typeof Tilemap !== "undefined" ? Tilemap.WATERFALL_AUTOTILE_TABLE : table; }
                }
            } else if (tileId < 4352) {
                setNumber = 1; bx = tx * 2; by = (ty - 2) * 3;
            } else if (tileId < 5888) {
                setNumber = 2; bx = tx * 2; by = (ty - 6) * 2;
                table = typeof Tilemap !== "undefined" ? Tilemap.WALL_AUTOTILE_TABLE : table;
            } else {
                setNumber = 3; bx = tx * 2; by = Math.floor((ty - 10) * 2.5 + (ty % 2 ? 0.5 : 0));
                if (ty % 2) table = typeof Tilemap !== "undefined" ? Tilemap.WALL_AUTOTILE_TABLE : table;
            }
            const pieces = table && table[shape];
            if (setNumber >= 0 && Array.isArray(pieces)) {
                const halfW = tileWidth / 2, halfH = tileHeight / 2;
                const flags = $gameMap && typeof $gameMap.tilesetFlags === "function" ? $gameMap.tilesetFlags() : [];
                const isTable = setNumber === 1 && flags && ((Number(flags[tileId]) || 0) & 0x80) !== 0;
                drawn = true;
                for (let q = 0; q < 4 && q < pieces.length; ++q) {
                    const point = pieces[q];
                    if (!point || point.length < 2) continue;
                    const qsx = Number(point[0]) || 0, qsy = Number(point[1]) || 0;
                    const dx = (q % 2) * halfW, dy = Math.floor(q / 2) * halfH;
                    if (isTable && (qsy === 1 || qsy === 5)) {
                        const tableX = qsy === 1 ? (4 - qsx) % 4 : qsx;
                        const ok1 = drawSourceRect(ctx, setNumber, (bx * 2 + tableX) * halfW, (by * 2 + 3) * halfH, halfW, halfH, dx, dy, halfW, halfH);
                        const ok2 = drawSourceRect(ctx, setNumber, (bx * 2 + qsx) * halfW, (by * 2 + qsy) * halfH, halfW, halfH / 2, dx, dy + halfH / 2, halfW, halfH / 2);
                        drawn = drawn && (ok1 || ok2);
                    } else {
                        drawn = drawSourceRect(ctx, setNumber, (bx * 2 + qsx) * halfW, (by * 2 + qsy) * halfH, halfW, halfH, dx, dy, halfW, halfH) && drawn;
                    }
                }
            }
        }

        if (!drawn) return null;
        if (threshold > 0 && typeof ctx.getImageData === "function" && typeof ctx.putImageData === "function") {
            try {
                const image = ctx.getImageData(0, 0, tileWidth, tileHeight);
                const data = image.data;
                for (let i = 3; i < data.length; i += 4) if (data[i] <= threshold) data[i] = 0;
                ctx.putImageData(image, 0, 0);
            } catch (_) {}
        }
        tileGraphicCache.set(key, canvas);
        return canvas;
    }

    function maskNeedsRebuild(mask, runtime) {
        ensureMaskSize(mask);
        return mask._ludoMaskMapId !== runtime.mapId ||
            mask._ludoMaskDisplayX !== $gameMap.displayX() ||
            mask._ludoMaskDisplayY !== $gameMap.displayY() ||
            mask._ludoRuntimeSignature !== runtimeSignature(runtime);
    }

    function drawRegionMask(ctx, width, height, runtime) {
        ctx.clearRect(0, 0, width, height);
        if (!runtime.enabled) return;
        if (runtime.tileMaskPieces && runtime.tileMaskPieces.length > 0) {
            const manifest = runtime.manifest || {};
            const sx = $gameMap.tileWidth() / Math.max(1, Number(manifest.tileWidth || $gameMap.tileWidth()));
            const sy = $gameMap.tileHeight() / Math.max(1, Number(manifest.tileHeight || $gameMap.tileHeight()));
            const scrollX = $gameMap.displayX() * $gameMap.tileWidth();
            const scrollY = $gameMap.displayY() * $gameMap.tileHeight();
            for (const piece of runtime.tileMaskPieces) {
                const bitmap=loadExportedMaskBitmap(piece.file); const source=bitmapDrawable(bitmap); if(!source) continue;
                const px=Math.round(piece.x*sx-scrollX), py=Math.round(piece.y*sy-scrollY);
                const pw=Math.round(piece.width*sx), ph=Math.round(piece.height*sy);
                if(px>width||py>height||px+pw<0||py+ph<0) continue;
                try { ctx.drawImage(source,0,0,piece.width,piece.height,px,py,pw,ph); } catch(_) {}
            }
        }
        ensureTileBindings(runtime);
        const tw = $gameMap.tileWidth();
        const th = $gameMap.tileHeight();
        const startX = Math.floor($gameMap.displayX()) - 2;
        const startY = Math.floor($gameMap.displayY()) - 2;
        const countX = Math.ceil(width / Math.max(1, tw)) + 4;
        const countY = Math.ceil(height / Math.max(1, th)) + 4;
        ctx.fillStyle = "rgba(255,255,255,1)";
        for (let sy = 0; sy < countY; ++sy) {
            const mapY = $gameMap.roundY(startY + sy);
            for (let sx = 0; sx < countX; ++sx) {
                const mapX = $gameMap.roundX(startX + sx);
                if (!$gameMap.isValid(mapX, mapY)) continue;
                const regionSurface = runtime.byRegion.get($gameMap.regionId(mapX, mapY));
                const tileId = maskTileIdAt(runtime, mapX, mapY);
                let surface = regionSurface;
                if (!surface || surface.maskMode === "tileAlpha") {
                    const key = tileBindingKey(tileId);
                    const learned = key ? runtime.tileBindings.get(key) : null;
                    if (learned) surface = learned;
                }
                if (!surface) continue;
                if (surface.maskMode === "tileAlpha" && runtime.tileBindings.get(tileBindingKey(tileId)) !== surface) continue;
                const px = Math.round($gameMap.adjustX(mapX) * tw);
                const py = Math.round($gameMap.adjustY(mapY) * th);
                if (surface.maskMode === "region") {
                    ctx.fillRect(px, py, tw + 1, th + 1);
                    continue;
                }
                const graphic = tileGraphicCanvas(tileId, tw, th, runtime.tileAlphaThreshold);
                if (graphic) ctx.drawImage(graphic, px, py, tw, th);
                else if (regionSurface) ctx.fillRect(px, py, tw + 1, th + 1);
            }
        }
    }

    function rebuildRegionMask(mask, runtime, force = false) {
        if (!mask || !$gameMap || (!force && !maskNeedsRebuild(mask, runtime))) return;
        ensureMaskSize(mask);
        mask._ludoMaskMapId = runtime.mapId;
        mask._ludoMaskDisplayX = $gameMap.displayX();
        mask._ludoMaskDisplayY = $gameMap.displayY();
        mask._ludoRuntimeSignature = runtimeSignature(runtime);
        const bitmap = mask.bitmap;
        const ctx = bitmap && (bitmap.context || bitmap._context);
        if (!ctx) return;
        const width = mask._ludoMaskWidth;
        const height = mask._ludoMaskHeight;
        const binary = mask._ludoBinaryCanvas;
        const binaryCtx = binary && binary.getContext ? binary.getContext("2d") : null;
        ctx.clearRect(0, 0, width, height);
        if (!runtime.enabled) { updateBitmapTexture(bitmap); return; }
        if (binaryCtx) {
            drawRegionMask(binaryCtx, width, height, runtime);
            ctx.save();
            if (runtime.edgeSoftness > 0 && "filter" in ctx) ctx.filter = `blur(${runtime.edgeSoftness}px)`;
            ctx.drawImage(binary, 0, 0);
            ctx.restore();
        } else drawRegionMask(ctx, width, height, runtime);
        updateBitmapTexture(bitmap);
    }

    function playerSpriteOf(spriteset) {
        return (spriteset._characterSprites || []).find(s => s && s._character === $gamePlayer) || null;
    }

    function isEvent(character) {
        return typeof Game_Event !== "undefined" && character instanceof Game_Event;
    }

    function isFollower(character) {
        return typeof Game_Follower !== "undefined" && character instanceof Game_Follower;
    }

    function frameValid(source) {
        const f = source && source._frame;
        return !!(f && f.width > 0 && f.height > 0);
    }

    function eventReflectionOffsetY(event) {
        if (!event) return 0;
        if (event._ludoReflectionOffsetCache !== undefined) return event._ludoReflectionOffsetCache;
        let value = 0;
        const read = text => {
            const match = /<\s*ReflectionOffsetY\s*:\s*(-?\d+(?:\.\d+)?)\s*>/i.exec(String(text || ""));
            if (match) value = clamp(Number(match[1]) || 0, -96, 96);
        };
        const data = typeof event.event === "function" ? event.event() : null;
        if (data) read(data.note);
        const list = typeof event.list === "function" ? event.list() : null;
        if (Array.isArray(list)) {
            for (const command of list) {
                if (!command || (command.code !== 108 && command.code !== 408)) continue;
                const text = Array.isArray(command.parameters) ? String(command.parameters[0] || "") : "";
                read(text);
            }
        }
        event._ludoReflectionOffsetCache = value;
        return value;
    }

    function sourceReflectionOffsetY(source) {
        const character = source && source._character;
        if (!character) return 0;
        let value = Number(character._ludoReflectionOffsetY);
        if (!Number.isFinite(value)) value = 0;
        if (isEvent(character)) value += eventReflectionOffsetY(character);
        return clamp(value, -192, 192);
    }

    function pageBlocksReflection(event) {
        if (!event) return true;
        if (event._ludoReflectionBlockCache !== undefined) return event._ludoReflectionBlockCache;
        let blocked = false;
        const data = typeof event.event === "function" ? event.event() : null;
        if (data && /<\s*NoReflection\s*>/i.test(String(data.note || ""))) blocked = true;
        const list = !blocked && typeof event.list === "function" ? event.list() : null;
        if (Array.isArray(list)) {
            for (const command of list) {
                if (!command || (command.code !== 108 && command.code !== 408)) continue;
                const text = Array.isArray(command.parameters) ? String(command.parameters[0] || "") : "";
                if (/<\s*NoReflection\s*>/i.test(text)) { blocked = true; break; }
            }
        }
        event._ludoReflectionBlockCache = blocked;
        return blocked;
    }

    if (typeof Game_CharacterBase !== "undefined") {
        Game_CharacterBase.prototype.setReflectionOffsetY = function(value) {
            this._ludoReflectionOffsetY = clamp(Number(value) || 0, -96, 96);
        };
        Game_CharacterBase.prototype.reflectionOffsetY = function() {
            return clamp(Number(this._ludoReflectionOffsetY) || 0, -96, 96);
        };
    }

    if (typeof Game_Event !== "undefined") {
        const _setupPage = Game_Event.prototype.setupPage;
        Game_Event.prototype.setupPage = function() {
            this._ludoReflectionBlockCache = undefined;
            this._ludoReflectionOffsetCache = undefined;
            _setupPage.call(this);
            this._ludoReflectionBlockCache = undefined;
            this._ludoReflectionOffsetCache = undefined;
        };
    }

    function scanSurface(runtime, centerX, baseY, halfTiles, depthTiles) {
        if (!runtime.enabled) return null;
        const cx = Math.round(centerX);
        const by = Math.floor(baseY);
        let best = null;
        let bestScore = Infinity;
        for (let dy = 0; dy <= depthTiles; ++dy) {
            const y = $gameMap.roundY(by + dy);
            for (let dx = -halfTiles; dx <= halfTiles; ++dx) {
                const x = $gameMap.roundX(cx + dx);
                if (!$gameMap.isValid(x, y)) continue;
                const surface = surfaceAtMapTile(runtime, x, y);
                if (!surface) continue;
                const score = dy * 8 + Math.abs(dx);
                if (score < bestScore) { best = surface; bestScore = score; }
            }
        }
        return best;
    }

    function surfaceForCharacter(runtime, source) {
        if (!source || !source._character || !frameValid(source)) return null;
        const c = source._character;
        const frame = source._frame;
        const tw = Math.max(1, $gameMap.tileWidth());
        const th = Math.max(1, $gameMap.tileHeight());
        const scaleX = source.scale ? Math.abs(source.scale.x || 1) : 1;
        const scaleY = source.scale ? Math.abs(source.scale.y || 1) : 1;
        const realX = Number.isFinite(c._realX) ? c._realX : Number(c.x || 0);
        const realY = Number.isFinite(c._realY) ? c._realY : Number(c.y || 0);
        const halfTiles = Math.max(0, Math.ceil(frame.width * scaleX * 0.5 / tw));
        const effectiveOffset = runtime.offsetY + sourceReflectionOffsetY(source);
        const depthTiles = Math.max(1, Math.ceil((frame.height * scaleY + Math.abs(effectiveOffset)) / th));
        return scanSurface(runtime, realX, realY, halfTiles, depthTiles);
    }

    function surfaceForObject(runtime, item) {
        const manifest = runtime.manifest;
        if (!manifest || !item) return null;
        const tw = Math.max(1, Number(manifest.tileWidth || $gameMap.tileWidth()));
        const th = Math.max(1, Number(manifest.tileHeight || $gameMap.tileHeight()));
        const centerX = (Number(item.x || 0) + Number(item.width || 0) * 0.5) / tw;
        const baseY = (Number(item.y || 0) + Math.max(0, Number(item.height || 0) - 1)) / th;
        const halfTiles = Math.max(0, Math.ceil(Number(item.width || tw) * 0.5 / tw));
        const depthTiles = Math.max(1, Math.ceil(Number(item.height || th) / th));
        return scanSurface(runtime, centerX, baseY, halfTiles, depthTiles);
    }

    function onScreen(x, y, width, height, margin) {
        return x + width >= -margin && x - width <= Graphics.width + margin &&
               y + height >= -margin && y - height <= Graphics.height + margin;
    }

    function weatherModifier(runtime) {
        const frame = typeof Graphics !== "undefined" ? Number(Graphics.frameCount || 0) : globalClock;
        if (weatherCacheFrame === frame && weatherCache) return weatherCache;
        const base = { amplitude:1, speed:1, wind:0, opacity:1 };
        if (runtime.weatherIntegration && window.LudoWeatherSystem && typeof window.LudoWeatherSystem.current === "function") {
            let state = null;
            try { state = window.LudoWeatherSystem.current("reflection"); } catch (_) {}
            if (state && state.enabled !== false) {
                const power = clamp(Number(state.power) || 0, 0, 9) / 9;
                const type = String(state.type || "none").toLowerCase();
                if (type === "rain") {
                    base.amplitude = 1 + 0.45 * power;
                    base.speed = 1 + 0.55 * power;
                } else if (type === "storm") {
                    base.amplitude = 1 + 1.15 * power;
                    base.speed = 1 + 1.05 * power;
                    base.opacity = 0.95;
                }
                base.wind = clamp(Number(state.windX) || 0, -10, 10) * 0.12 +
                    clamp(Number(state.windY) || 0, -10, 10) * 0.035;
            }
        }
        weatherCacheFrame = frame;
        weatherCache = base;
        return base;
    }

    function advanceAlpha(entry, target, runtime) {
        entry.targetAlpha = target ? 1 : 0;
        const frames = target ? runtime.fadeInFrames : runtime.fadeOutFrames;
        if (frames <= 0) entry.alpha = entry.targetAlpha;
        else if (target) entry.alpha = Math.min(1, entry.alpha + 1 / frames);
        else entry.alpha = Math.max(0, entry.alpha - 1 / frames);
    }

    function applySurfaceToEntry(entry, surface, runtime) {
        if (!surface) return;
        entry.surface = surface;
        entry.fadeMask.bitmap = gradientBitmap(surface.style.verticalFade);
        if (entry.shader) {
            const weather = weatherModifier(runtime);
            entry.shader.uniforms.uTime = globalClock / 60;
            entry.shader.uniforms.uFrequency = surface.style.frequency;
            entry.shader.uniforms.uSpeed = surface.style.speed * weather.speed;
            entry.shader.uniforms.uWind = weather.wind * globalClock / 60;
            entry.shader.uniforms.uWaveX = surface.style.waveX * weather.amplitude / Math.max(32, entry.width) * 0.9;
            entry.shader.uniforms.uWaveY = surface.style.waveY * weather.amplitude / Math.max(32, entry.height) * 0.9;
            const blur = clamp(Number(surface.style.blurStrength) || 0, 0, 24);
            const blurMode = String(surface.style.blurMode || "none");
            entry.shader.uniforms.uBlurX = (blurMode === "horizontal" || blurMode === "both") ? (blur * 0.5) / Math.max(32, entry.width) : 0;
            entry.shader.uniforms.uBlurY = (blurMode === "vertical" || blurMode === "both") ? (blur * 0.5) / Math.max(32, entry.height) : 0;
            entry.shader.padding = Math.ceil(Math.max(surface.style.waveX * weather.amplitude, blur) + 4);
        }
        if (entry.mode === "strips") {
            const blur = clamp(Number(surface.style.blurStrength) || 0, 0, 24);
            const blurMode = String(surface.style.blurMode || "none");
            const needsBlur = blur > 0 && blurMode !== "none" && shaderSupported();
            if (needsBlur && !entry.blurFilter) entry.blurFilter = createShaderFilter();
            if (entry.blurFilter && needsBlur) {
                entry.blurFilter.uniforms.uWaveX = 0;
                entry.blurFilter.uniforms.uWaveY = 0;
                entry.blurFilter.uniforms.uBlurX = (blurMode === "horizontal" || blurMode === "both") ? (blur * 0.5) / Math.max(32, entry.width) : 0;
                entry.blurFilter.uniforms.uBlurY = (blurMode === "vertical" || blurMode === "both") ? (blur * 0.5) / Math.max(32, entry.height) : 0;
                entry.blurFilter.padding = Math.ceil(blur + 4);
                entry.content.filters = [entry.blurFilter];
            } else if (!entry.shader) {
                entry.content.filters = null;
            }
        }
    }

    function updateCharacterStripes(entry, source, surface, runtime, sourceOpacity) {
        const frame = source._frame;
        const count = runtime.profile.stripes;
        ensureWaveStripes(entry, count);
        const weather = weatherModifier(runtime);
        const style = surface.style;
        const phaseBase = globalClock * 0.045 * style.speed * weather.speed + Number(source.x || 0) * 0.0025 + weather.wind * globalClock * 0.01;
        for (let i = 0; i < entry.waveStripes.length; ++i) {
            const stripe = entry.waveStripes[i];
            if (i >= count) { stripe.visible = false; continue; }
            const y0 = Math.floor(i * frame.height / count);
            const y1 = Math.floor((i + 1) * frame.height / count);
            const h = Math.max(1, y1 - y0);
            const phase = phaseBase + i * (style.frequency / Math.max(1, count * 0.7));
            stripe.bitmap = source.bitmap;
            stripe.setFrame(frame.x, frame.y + y0, frame.width, h);
            stripe.anchor.x = source.anchor ? source.anchor.x : 0.5;
            stripe.anchor.y = 1;
            stripe.scale.x = 1;
            stripe.scale.y = -1;
            stripe.tint = style.tint;
            stripe.blendMode = source.blendMode;
            stripe.opacity = Math.round(style.opacity * sourceOpacity * entry.alpha * weather.opacity);
            stripe.x = Math.sin(phase) * style.waveX * weather.amplitude;
            stripe.y = (source.anchor ? source.anchor.y : 1) * frame.height - y0 - h + Math.sin(phase * 0.83 + 0.6) * style.waveY * weather.amplitude;
            stripe.visible = stripe.opacity > 0;
        }
    }

    function updateCharacterVisual(entry, source, surface, runtime) {
        const frame = source._frame;
        const style = surface.style;
        const weather = weatherModifier(runtime);
        entry.width = Math.max(1, frame.width * Math.abs(source.scale ? source.scale.x || 1 : 1));
        entry.height = Math.max(1, frame.height * Math.abs(source.scale ? source.scale.y || 1 : 1));
        applySurfaceToEntry(entry, surface, runtime);
        entry.container.x = source.x;
        entry.container.y = source.y + runtime.offsetY + sourceReflectionOffsetY(source);
        entry.container.rotation = source.rotation || 0;
        entry.container.skew.x = source.skew ? source.skew.x : 0;
        entry.container.skew.y = source.skew ? source.skew.y : 0;
        entry.container.z = Number(source.z || 3) - 0.1;
        entry.inner.scale.x = source.scale ? source.scale.x : 1;
        entry.inner.scale.y = source.scale ? Math.abs(source.scale.y) : 1;

        entry.fadeMask.anchor.x = source.anchor ? source.anchor.x : 0.5;
        entry.fadeMask.anchor.y = 0;
        entry.fadeMask.x = 0;
        entry.fadeMask.y = -(1 - (source.anchor ? source.anchor.y : 1)) * frame.height;
        entry.fadeMask.scale.x = frame.width / Math.max(1, entry.fadeMask.bitmap.width);
        entry.fadeMask.scale.y = frame.height / Math.max(1, entry.fadeMask.bitmap.height);

        const sourceOpacity = clamp(Number(source.opacity) || 0, 0, 255) / 255;
        if (entry.mode === "shader") {
            entry.sprite.bitmap = source.bitmap;
            entry.sprite.setFrame(frame.x, frame.y, frame.width, frame.height);
            entry.sprite.anchor.x = source.anchor ? source.anchor.x : 0.5;
            entry.sprite.anchor.y = source.anchor ? source.anchor.y : 1;
            entry.sprite.scale.set(1, -1);
            entry.sprite.tint = style.tint;
            entry.sprite.blendMode = source.blendMode;
            entry.sprite.opacity = Math.round(style.opacity * sourceOpacity * entry.alpha * weather.opacity);
            entry.sprite.visible = entry.sprite.opacity > 0;
            entry.waveContainer.visible = false;
            for (const stripe of entry.waveStripes) stripe.visible = false;
        } else {
            entry.sprite.visible = false;
            entry.waveContainer.visible = true;
            updateCharacterStripes(entry, source, surface, runtime, sourceOpacity);
        }
        entry.objectVisual.visible = false;
    }

    function attachEntry(spriteset, entry, mask) {
        entry.container.mask = mask;
        spriteset._tilemap.addChild(entry.container);
    }

    function environmentBlendMode(name) {
        if (typeof PIXI === "undefined" || !PIXI.BLEND_MODES) return 0;
        if (name === "screen" && PIXI.BLEND_MODES.SCREEN != null) return PIXI.BLEND_MODES.SCREEN;
        if (name === "add" && PIXI.BLEND_MODES.ADD != null) return PIXI.BLEND_MODES.ADD;
        return PIXI.BLEND_MODES.NORMAL != null ? PIXI.BLEND_MODES.NORMAL : 0;
    }

    function environmentWaveStyle(runtime) {
        const surfaces = runtime && Array.isArray(runtime.surfaces) ? runtime.surfaces : [];
        if (surfaces.length === 0) return { waveX:0, waveY:0, speed:0, frequency:1 };
        let waveX = 0, waveY = 0, speed = 0, frequency = 0;
        for (const surface of surfaces) {
            const style = surface && surface.style ? surface.style : PRESETS.still;
            waveX += Number(style.waveX) || 0;
            waveY += Number(style.waveY) || 0;
            speed += Number(style.speed) || 0;
            frequency += Number(style.frequency) || 1;
        }
        const count = surfaces.length;
        return { waveX:waveX/count, waveY:waveY/count, speed:speed/count, frequency:frequency/count };
    }

    function environmentBlurStyle(runtime) {
        const surfaces = runtime && Array.isArray(runtime.surfaces) ? runtime.surfaces : [];
        let blurX = 0, blurY = 0;
        for (const surface of surfaces) {
            const style = surface && surface.style ? surface.style : null;
            if (!style) continue;
            const amount = clamp(Number(style.blurStrength) || 0, 0, 24);
            const mode = String(style.blurMode || "none");
            if (mode === "horizontal" || mode === "both") blurX = Math.max(blurX, amount);
            if (mode === "vertical" || mode === "both") blurY = Math.max(blurY, amount);
        }
        return { blurX, blurY };
    }

    function createEnvironmentReflection(spriteset, runtime, mask) {
        const env = runtime && runtime.environment;
        if (!env || !env.enabled || !env.file || !spriteset._tilemap) return null;
        const container = new PIXI.Container();
        container.z = 2.72;
        container._ludoReflectionEnvironment = true;
        container.mask = mask;

        const sprite = new Sprite(loadEnvironmentBitmap(env.file));
        sprite.anchor.set(0.5, 0.5);
        sprite.blendMode = environmentBlendMode(env.blendMode);
        container.addChild(sprite);

        const useWaves = effectiveRenderMode(runtime) === "shader";
        const shader = shaderSupported() ? createShaderFilter() : null;
        if (shader) container.filters = [shader];

        spriteset._tilemap.addChild(container);
        return { container, sprite, shader, useWaves, file:env.file };
    }

    function updateEnvironmentReflection(state, runtime) {
        if (!state || !state.container || !state.sprite) return;
        const env = runtime && runtime.environment;
        if (!env || !env.enabled || !env.file) {
            state.container.visible = false;
            return;
        }
        if (state.file !== env.file) {
            state.file = env.file;
            state.sprite.bitmap = loadEnvironmentBitmap(env.file);
        }
        const bitmap = state.sprite.bitmap;
        const ready = bitmap && (typeof bitmap.isReady !== "function" || bitmap.isReady());
        const bw = ready ? Math.max(1, Number(bitmap.width) || 1) : 0;
        const bh = ready ? Math.max(1, Number(bitmap.height) || 1) : 0;
        if (!ready || bw <= 0 || bh <= 0) {
            state.container.visible = false;
            return;
        }

        const vw = Math.max(1, Number(Graphics.width) || 1);
        const vh = Math.max(1, Number(Graphics.height) || 1);
        let sx = vw / bw;
        let sy = vh / bh;
        if (env.fit !== "stretch") {
            const cover = Math.max(sx, sy);
            sx = cover;
            sy = cover;
        }
        if (env.flipY) sy *= -1;

        state.sprite.x = vw * 0.5;
        state.sprite.y = vh * 0.5;
        state.sprite.scale.set(sx, sy);
        state.sprite.opacity = clamp(Number(env.opacity) || 0, 0, 255);
        state.sprite.blendMode = environmentBlendMode(env.blendMode);

        if (state.shader) {
            const style = environmentWaveStyle(runtime);
            const weather = weatherModifier(runtime);
            state.shader.uniforms.uTime = globalClock / 60;
            state.shader.uniforms.uFrequency = style.frequency;
            state.shader.uniforms.uSpeed = style.speed * weather.speed;
            state.shader.uniforms.uWind = weather.wind * globalClock / 60;
            state.shader.uniforms.uWaveX = state.useWaves ? style.waveX * weather.amplitude / vw * 0.65 : 0;
            state.shader.uniforms.uWaveY = state.useWaves ? style.waveY * weather.amplitude / vh * 0.65 : 0;
            const blur = environmentBlurStyle(runtime);
            state.shader.uniforms.uBlurX = blur.blurX > 0 ? (blur.blurX * 0.5) / vw : 0;
            state.shader.uniforms.uBlurY = blur.blurY > 0 ? (blur.blurY * 0.5) / vh : 0;
            state.shader.padding = Math.ceil(Math.max(style.waveX * weather.amplitude, blur.blurX, blur.blurY) + 4);
        }
        state.container.visible = state.sprite.opacity > 0;
    }

    function syncCharacterEntry(entry, source, allowed, runtime, budgetAllowed) {
        const visual = !!(source && source.bitmap && source.visible && frameValid(source));
        const boundsOk = visual && onScreen(Number(source.x || 0), Number(source.y || 0),
            entry.width || source._frame.width, entry.height || source._frame.height, runtime.cullingMargin);
        const surface = allowed && budgetAllowed && visual && boundsOk ? surfaceForCharacter(runtime, source) : null;
        advanceAlpha(entry, !!surface, runtime);
        if (!visual || entry.alpha <= 0.001) { entry.container.visible = false; return false; }
        const useSurface = surface || entry.surface;
        if (!useSurface) { entry.container.visible = false; return false; }
        updateCharacterVisual(entry, source, useSurface, runtime);
        entry.container.visible = entry.alpha > 0.001;
        return !!surface;
    }

    function mapScaleX(runtime) {
        const api = mapApi();
        if (api && typeof api.mapScaleX === "function") return api.mapScaleX();
        const source = Math.max(1, Number(runtime.manifest && runtime.manifest.tileWidth || $gameMap.tileWidth()));
        return $gameMap.tileWidth() / source;
    }

    function mapScaleY(runtime) {
        const api = mapApi();
        if (api && typeof api.mapScaleY === "function") return api.mapScaleY();
        const source = Math.max(1, Number(runtime.manifest && runtime.manifest.tileHeight || $gameMap.tileHeight()));
        return $gameMap.tileHeight() / source;
    }

    function atlasBitmap(name) {
        const api = mapApi();
        if (api && typeof api.bitmapFor === "function") return api.bitmapFor(name);
        return ImageManager.loadBitmap("img/ludoMaps/", String(name || "").replace(/\.png$/i, ""));
    }

    function localFrameIndex(part, elapsedMs) {
        const api = mapApi();
        if (api && typeof api.frameIndex === "function") return api.frameIndex(part, elapsedMs);
        const frames = Array.isArray(part.frames) ? part.frames.length : 0;
        if (frames <= 1) return 0;
        const fps = clamp(Number(part.fps) || 6, 0.1, 120);
        const pingPong = !!part.pingPong;
        const sequence = pingPong ? Math.max(1, frames * 2 - 2) : frames;
        let step = Math.max(0, Math.floor(elapsedMs * fps / 1000));
        if (!part.synchronized && sequence > 1) step += Math.max(0, Number(part.phase) || 0) % sequence;
        if (part.loop === false) step = Math.min(step, sequence - 1); else step %= sequence;
        return pingPong && step >= frames ? frames * 2 - 2 - step : step;
    }

    function prepareObjectEntry(entry, item, runtime) {
        entry.objectData = item;
        entry.objectParts.length = 0;
        entry.objectVisual.removeChildren();
        const sx = mapScaleX(runtime), sy = mapScaleY(runtime);
        const widthPx = Math.max(1, Number(item.width || 1) * sx);
        const heightPx = Math.max(1, Number(item.height || 1) * sy);
        for (const part of Array.isArray(item.parts) ? item.parts : []) {
            const sprite = new Sprite(atlasBitmap(part.atlas));
            sprite.anchor.set(0, 0);
            entry.objectVisual.addChild(sprite);
            entry.objectParts.push({ sprite, data:part, frame:-1 });
        }
        entry.width = widthPx;
        entry.height = heightPx;
        entry.objectVisual.scale.y = -1;
        entry.objectVisual.visible = true;
    }

    function updateObjectVisual(entry, surface, runtime) {
        const item = entry.objectData;
        if (!item) return;
        const sx = mapScaleX(runtime), sy = mapScaleY(runtime);
        const widthPx = Math.max(1, Number(item.width || 1) * sx);
        const heightPx = Math.max(1, Number(item.height || 1) * sy);
        entry.width = widthPx;
        entry.height = heightPx;
        applySurfaceToEntry(entry, surface, runtime);
        const scrollX = $gameMap.displayX() * $gameMap.tileWidth();
        const scrollY = $gameMap.displayY() * $gameMap.tileHeight();
        const left = Number(item.x || 0) * sx - scrollX;
        const top = Number(item.y || 0) * sy - scrollY;
        entry.container.x = left + widthPx * 0.5;
        entry.container.y = top + heightPx + runtime.offsetY + clamp(Number(item.reflectionOffsetY) || 0, -96, 96);
        entry.container.rotation = Number(item.rotation || 0) * Math.PI / 180;
        entry.container.z = 2.85;
        entry.inner.scale.set(1, 1);
        entry.objectVisual.scale.set(1, -1);
        entry.objectVisual.visible = true;
        entry.sprite.visible = false;
        entry.waveContainer.visible = false;

        const style = surface.style;
        const weather = weatherModifier(runtime);
        const elapsed = mapApi() && typeof mapApi().animationElapsedMs === "function" ? mapApi().animationElapsedMs() : globalClock * (1000 / 60);
        for (const partState of entry.objectParts) {
            const part = partState.data;
            const frames = Array.isArray(part.frames) ? part.frames : [];
            const index = localFrameIndex(part, elapsed);
            const frame = frames[index] || frames[0];
            if (!Array.isArray(frame) || frame.length < 4) { partState.sprite.visible = false; continue; }
            const sw = Math.max(1, Number(frame[2] || 1));
            const sh = Math.max(1, Number(frame[3] || 1));
            const desiredW = Math.max(1, Number(part.width || sw) * sx);
            const desiredH = Math.max(1, Number(part.height || sh) * sy);
            const sprite = partState.sprite;
            sprite.setFrame(Number(frame[0] || 0), Number(frame[1] || 0), sw, sh);
            sprite.x = Number(part.x || 0) * sx - widthPx * 0.5;
            sprite.y = Number(part.y || 0) * sy - heightPx;
            sprite.scale.set(desiredW / sw, desiredH / sh);
            sprite.tint = style.tint;
            sprite.opacity = Math.round(style.opacity * clamp(Number(item.opacity == null ? 1 : item.opacity), 0, 1) * entry.alpha * weather.opacity);
            sprite.visible = sprite.opacity > 0;
        }
        entry.fadeMask.anchor.set(0.5, 0);
        entry.fadeMask.x = 0;
        entry.fadeMask.y = 0;
        entry.fadeMask.scale.x = widthPx / Math.max(1, entry.fadeMask.bitmap.width);
        entry.fadeMask.scale.y = heightPx / Math.max(1, entry.fadeMask.bitmap.height);
    }

    function syncObjectEntry(entry, runtime, budgetAllowed) {
        const item = entry.objectData;
        if (!item) return false;
        const sx = mapScaleX(runtime), sy = mapScaleY(runtime);
        const left = Number(item.x || 0) * sx - $gameMap.displayX() * $gameMap.tileWidth();
        const top = Number(item.y || 0) * sy - $gameMap.displayY() * $gameMap.tileHeight();
        const width = Math.max(1, Number(item.width || 1) * sx);
        const height = Math.max(1, Number(item.height || 1) * sy);
        const boundsOk = onScreen(left + width * 0.5, top + height, width, height, runtime.cullingMargin);
        const surface = budgetAllowed && boundsOk ? surfaceForObject(runtime, item) : null;
        advanceAlpha(entry, !!surface, runtime);
        if (entry.alpha <= 0.001) { entry.container.visible = false; return false; }
        const useSurface = surface || entry.surface;
        if (!useSurface) { entry.container.visible = false; return false; }
        updateObjectVisual(entry, useSurface, runtime);
        entry.container.visible = true;
        return !!surface;
    }

    function lightGradientBitmap() {
        if (lightGradientBitmap._bitmap) return lightGradientBitmap._bitmap;
        const bitmap = new Bitmap(96, 192);
        const ctx = bitmap.context || bitmap._context;
        if (ctx) {
            ctx.clearRect(0, 0, 96, 192);
            const g = ctx.createRadialGradient(48, 8, 2, 48, 32, 90);
            g.addColorStop(0, "rgba(255,255,255,0.95)");
            g.addColorStop(0.16, "rgba(255,255,255,0.55)");
            g.addColorStop(1, "rgba(255,255,255,0)");
            ctx.fillStyle = g;
            ctx.fillRect(0, 0, 96, 192);
            updateBitmapTexture(bitmap);
        }
        lightGradientBitmap._bitmap = bitmap;
        return bitmap;
    }

    function characterFromLightId(id) {
        const n = Number(id);
        if (n === -1) return $gamePlayer;
        if (n > 0 && $gameMap) return $gameMap.event(n);
        return null;
    }

    function lightDescriptorFromSource(source, fallbackId) {
        if (!source || !$gameMap) return null;
        const tw = Math.max(1, $gameMap.tileWidth());
        const th = Math.max(1, $gameMap.tileHeight());
        let mapX = NaN;
        let mapY = NaN;
        if (typeof source.mapCenterX === "function" && typeof source.mapCenterY === "function") {
            mapX = Number(source.mapCenterX()) / tw - 0.5;
            mapY = Number(source.mapCenterY()) / th - 0.5;
        } else if (source.character) {
            mapX = Number.isFinite(source.character._realX) ? source.character._realX : Number(source.character.x);
            mapY = Number.isFinite(source.character._realY) ? source.character._realY : Number(source.character.y);
        } else {
            mapX = Number(source.mapX);
            mapY = Number(source.mapY);
        }
        if (!Number.isFinite(mapX) || !Number.isFinite(mapY)) return null;
        const currentOpacity = typeof source.currentOpacity === "function" ? source.currentOpacity() : source.opacity;
        return {
            id:String(source.dynamicId || source.origin || fallbackId),
            mapX, mapY,
            color:parseColor(source.color, 0xffffff),
            opacity:clamp(Number(currentOpacity == null ? 185 : currentOpacity), 0, 255),
            scale:clamp(Number(source.scale == null ? 1 : source.scale), 0.25, 6)
        };
    }

    function collectLightDescriptors(runtime) {
        if (!runtime.reflectLights || !$gameMap) return [];
        const out = [];
        const seen = new Set();
        const push = (descriptor) => {
            if (!descriptor) return;
            const key = `${descriptor.id}:${descriptor.mapX.toFixed(3)}:${descriptor.mapY.toFixed(3)}`;
            if (seen.has(key)) return;
            seen.add(key);
            out.push(descriptor);
        };

        // API publica do LudoLightEngine 2.x: inclui luzes de eventos e dinamicas.
        if (typeof $gameMap.ludoLightSources === "function") {
            let sources = [];
            try { sources = $gameMap.ludoLightSources() || []; } catch (_) {}
            for (let i = 0; i < sources.length; ++i) push(lightDescriptorFromSource(sources[i], `source-${i}`));
            if (typeof $gameMap.ludoPlayerLightSource === "function") {
                try { push(lightDescriptorFromSource($gameMap.ludoPlayerLightSource(), "player")); } catch (_) {}
            }
            if (typeof $gameMap.ludoItemEquipLightSource === "function") {
                try { push(lightDescriptorFromSource($gameMap.ludoItemEquipLightSource(), "item")); } catch (_) {}
            }
        } else {
            // Compatibilidade com builds antigos do LudoLightEngine.
            const defs = $gameMap._ludoDynamicLights && typeof $gameMap._ludoDynamicLights === "object"
                ? Object.values($gameMap._ludoDynamicLights) : [];
            for (const def of defs) {
                if (!def) continue;
                let mapX = null, mapY = null;
                if (def.kind === "position") { mapX = Number(def.x); mapY = Number(def.y); }
                else {
                    const c = characterFromLightId(def.characterId);
                    if (c) { mapX = Number.isFinite(c._realX) ? c._realX : c.x; mapY = Number.isFinite(c._realY) ? c._realY : c.y; }
                }
                if (!Number.isFinite(mapX) || !Number.isFinite(mapY)) continue;
                const overrides = def.overrides || {};
                push({
                    id:String(def.id || out.length), mapX, mapY,
                    color:parseColor(overrides.color, 0xffffff),
                    opacity:clamp(Number(overrides.opacity == null ? 185 : overrides.opacity), 0, 255),
                    scale:clamp(Number(overrides.scale == null ? 1 : overrides.scale), 0.25, 6)
                });
            }
            const playerLight = $gameSystem && $gameSystem._ludoPlayerLight;
            if (playerLight && playerLight.enabled !== false && $gamePlayer) {
                push({
                    id:"player", mapX:Number($gamePlayer._realX), mapY:Number($gamePlayer._realY),
                    color:parseColor(playerLight.color, 0xffffff),
                    opacity:clamp(Number(playerLight.opacity || 185), 0, 255),
                    scale:clamp(Number(playerLight.scale || 1), 0.25, 6)
                });
            }
        }
        return out.slice(0, runtime.profile.lightLimit);
    }

    function ensureLightSprites(spriteset, count) {
        const pool = spriteset._ludoReflectionLightSprites || (spriteset._ludoReflectionLightSprites = []);
        while (pool.length < count) {
            const sprite = new Sprite(lightGradientBitmap());
            sprite.anchor.set(0.5, 0);
            sprite.blendMode = PIXI.BLEND_MODES.ADD;
            sprite.z = 2.8;
            sprite.visible = false;
            sprite.mask = spriteset._ludoReflectionMask;
            spriteset._tilemap.addChild(sprite);
            pool.push(sprite);
        }
        return pool;
    }

    function updateLightReflections(spriteset, runtime) {
        const lights = collectLightDescriptors(runtime);
        const pool = ensureLightSprites(spriteset, lights.length);
        const tw = $gameMap.tileWidth(), th = $gameMap.tileHeight();
        for (let i = 0; i < pool.length; ++i) {
            const sprite = pool[i];
            const light = lights[i];
            if (!light) { sprite.visible = false; continue; }
            const surface = scanSurface(runtime, light.mapX, light.mapY, 1, 3);
            if (!surface) { sprite.visible = false; continue; }
            sprite.x = $gameMap.adjustX(light.mapX) * tw + tw * 0.5;
            sprite.y = $gameMap.adjustY(light.mapY) * th + th * 0.7;
            const rough = 1 + surface.style.waveX * 0.06;
            sprite.scale.set(light.scale * rough, light.scale * (0.75 + surface.style.waveY * 0.2));
            sprite.tint = light.color;
            sprite.opacity = Math.round(light.opacity * (surface.style.opacity / 205));
            const blur = clamp(Number(surface.style.blurStrength) || 0, 0, 24);
            const blurMode = String(surface.style.blurMode || "none");
            const needsBlur = blur > 0 && blurMode !== "none" && shaderSupported();
            if (needsBlur && !sprite._ludoReflectionBlurFilter) sprite._ludoReflectionBlurFilter = createShaderFilter();
            if (needsBlur && sprite._ludoReflectionBlurFilter) {
                const filter = sprite._ludoReflectionBlurFilter;
                filter.uniforms.uWaveX = 0; filter.uniforms.uWaveY = 0;
                filter.uniforms.uBlurX = (blurMode === "horizontal" || blurMode === "both") ? (blur * 0.5) / 192 : 0;
                filter.uniforms.uBlurY = (blurMode === "vertical" || blurMode === "both") ? (blur * 0.5) / 256 : 0;
                filter.padding = Math.ceil(blur + 4);
                sprite.filters = [filter];
            } else sprite.filters = null;
            sprite.visible = onScreen(sprite.x, sprite.y, 96 * sprite.scale.x, 192 * sprite.scale.y, runtime.cullingMargin);
        }
    }

    function createCharacterPairs(spriteset, runtime, mask) {
        const result = [];
        for (const source of spriteset._characterSprites || []) {
            if (!source || !source._character || source._character === $gamePlayer) continue;
            const event = isEvent(source._character);
            const follower = isFollower(source._character);
            if (!event && !follower) continue;
            const entry = makeReflectionEntry("character", runtime);
            attachEntry(spriteset, entry, mask);
            result.push({ source, character:source._character, event, follower, entry });
        }
        return result;
    }

    function createObjectEntries(spriteset, runtime, mask) {
        const entries = [];
        if (!runtime.reflectObjects) return entries;
        for (const item of runtime.objects) {
            if (!item || !Array.isArray(item.parts) || item.parts.length === 0) continue;
            const entry = makeReflectionEntry("object", runtime);
            prepareObjectEntry(entry, item, runtime);
            attachEntry(spriteset, entry, mask);
            entries.push(entry);
        }
        return entries;
    }

    Spriteset_Map.prototype.destroyLudoReflectionLayer = function() {
        const all = [];
        if (this._ludoPlayerReflectionEntry) all.push(this._ludoPlayerReflectionEntry);
        for (const pair of this._ludoReflectionCharacterPairs || []) if (pair.entry) all.push(pair.entry);
        for (const entry of this._ludoReflectionObjectEntries || []) all.push(entry);
        for (const entry of all) {
            if (entry.container && entry.container.parent) entry.container.parent.removeChild(entry.container);
            if (entry.container && typeof entry.container.destroy === "function") entry.container.destroy({children:true});
        }
        for (const sprite of this._ludoReflectionLightSprites || []) {
            if (sprite.parent) sprite.parent.removeChild(sprite);
            if (typeof sprite.destroy === "function") sprite.destroy();
        }
        const environment = this._ludoReflectionEnvironment;
        if (environment && environment.container) {
            if (environment.container.parent) environment.container.parent.removeChild(environment.container);
            if (typeof environment.container.destroy === "function") environment.container.destroy({children:true});
        }
        this._ludoReflectionEnvironment = null;
        if (this._ludoReflectionMask && this._ludoReflectionMask.parent) this._ludoReflectionMask.parent.removeChild(this._ludoReflectionMask);
        this._ludoReflectionMask = null;
        this._ludoPlayerReflectionEntry = null;
        this._ludoReflectionCharacterPairs = [];
        this._ludoReflectionObjectEntries = [];
        this._ludoReflectionLightSprites = [];
    };

    Spriteset_Map.prototype.createLudoReflectionLayer = function() {
        this.destroyLudoReflectionLayer();
        const runtime = resolveRuntime();
        if (!this._tilemap || !runtime.enabled) return;
        const mask = makeRegionMask();
        this._tilemap.addChild(mask);
        this._ludoReflectionMask = mask;
        rebuildRegionMask(mask, runtime, true);
        this._ludoReflectionEnvironment = createEnvironmentReflection(this, runtime, mask);
        updateEnvironmentReflection(this._ludoReflectionEnvironment, runtime);

        const playerEntry = makeReflectionEntry("character", runtime);
        attachEntry(this, playerEntry, mask);
        this._ludoPlayerReflectionEntry = playerEntry;
        this._ludoPlayerReflectionSource = playerSpriteOf(this);
        this._ludoReflectionCharacterPairs = createCharacterPairs(this, runtime, mask);
        this._ludoReflectionObjectEntries = createObjectEntries(this, runtime, mask);
        this._ludoReflectionLightSprites = [];
        this.updateLudoReflectionLayer(true);
    };

    Spriteset_Map.prototype.updateLudoReflectionLayer = function(force = false) {
        const runtime = resolveRuntime();
        if (!runtime.enabled || !this._ludoReflectionMask) return;
        globalClock += 1;
        rebuildRegionMask(this._ludoReflectionMask, runtime, force);
        updateEnvironmentReflection(this._ludoReflectionEnvironment, runtime);
        if (!this._ludoPlayerReflectionSource || this._ludoPlayerReflectionSource._character !== $gamePlayer) {
            this._ludoPlayerReflectionSource = playerSpriteOf(this);
        }

        let budget = runtime.maxReflections;
        const playerActive = syncCharacterEntry(this._ludoPlayerReflectionEntry, this._ludoPlayerReflectionSource, true, runtime, budget > 0);
        if (playerActive) budget--;
        for (const pair of this._ludoReflectionCharacterPairs || []) {
            const allowed = pair.event ? runtime.reflectEvents && !pageBlocksReflection(pair.character)
                : pair.follower ? runtime.reflectFollowers : false;
            const active = syncCharacterEntry(pair.entry, pair.source, allowed, runtime, budget > 0);
            if (active && budget > 0) budget--;
        }
        if (runtime.reflectObjects) {
            for (const entry of this._ludoReflectionObjectEntries || []) {
                const active = syncObjectEntry(entry, runtime, budget > 0);
                if (active && budget > 0) budget--;
            }
        } else {
            for (const entry of this._ludoReflectionObjectEntries || []) advanceAlpha(entry, false, runtime);
        }
        updateLightReflections(this, runtime);
    };

    const _createCharacters = Spriteset_Map.prototype.createCharacters;
    Spriteset_Map.prototype.createCharacters = function() {
        _createCharacters.call(this);
        this.createLudoReflectionLayer();
    };

    const _update = Spriteset_Map.prototype.update;
    Spriteset_Map.prototype.update = function() {
        _update.call(this);
        this.updateLudoReflectionLayer(false);
    };

    const _destroy = Spriteset_Map.prototype.destroy;
    Spriteset_Map.prototype.destroy = function(options) {
        this.destroyLudoReflectionLayer();
        _destroy.call(this, options);
    };

    function refreshCurrentScene() {
        runtimeDirty = true;
        const scene = SceneManager && SceneManager._scene;
        const spriteset = scene && scene._spriteset;
        if (spriteset && typeof spriteset.createLudoReflectionLayer === "function") spriteset.createLudoReflectionLayer();
    }

    PluginManager.registerCommand(PLUGIN_NAME, "Refresh", refreshCurrentScene);

    const API = Object.freeze({
        version:VERSION,
        presets:PRESETS,
        current() {
            const r = resolveRuntime();
            return {
                enabled:r.enabled, quality:r.quality, renderMode:effectiveRenderMode(r),
                surfaces:r.surfaces.map(s => ({regionId:s.regionId, preset:s.preset, maskMode:s.maskMode})),
                tileAlphaThreshold:r.tileAlphaThreshold, tileMaskLayer:r.tileMaskLayer,
                reflectEvents:r.reflectEvents, reflectFollowers:r.reflectFollowers,
                reflectObjects:r.reflectObjects, reflectLights:r.reflectLights,
                weatherIntegration:r.weatherIntegration, maxReflections:r.maxReflections,
                cullingMargin:r.cullingMargin, offsetY:r.offsetY,
                environment:Object.assign({}, r.environment)
            };
        },
        setCharacterOffset(character, value) {
            if (character && typeof character.setReflectionOffsetY === "function") character.setReflectionOffsetY(value);
        },
        refresh:refreshCurrentScene
    });
    window.LudoReflectionSystem = API;

    if (window.Ludo && window.Ludo.__isLudoCore && typeof window.Ludo.registerModule === "function") {
        try {
            window.Ludo.registerModule({
                id:"reflection", plugin:PLUGIN_NAME, version:VERSION, category:"world", api:API,
                requires:{}, optional:["weather", "lighting"], externalRequires:[],
                provides:["reflection", "reflection.surfaces"], legacyGlobals:["LudoReflectionSystem"]
            });
        } catch (error) {
            console.warn(`[${PLUGIN_NAME}] Nao foi possivel registrar o modulo no LudoCore.`, error);
        }
    }

    console.info(`[${PLUGIN_NAME}] v${VERSION} carregado.`);
})();
