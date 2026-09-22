// ============================================================================
//  Model.h — Estruturas de dados do Tile Editor Studio (porte C++/Qt6).
//
//  Equivalente ao objeto `state` da ferramenta web:
//    state.map        -> MapInfo
//    state.tilesets   -> QVector<Tileset>
//    state.layers     -> arvore de LayerPtr (grupos/mascaras tem children)
//    state.wangSets   -> QVector<WangSet>
//    ...
//
//  Regras importantes preservadas do original:
//   * Uma celula pode conter uma PILHA de tiles (modo "Colocar por cima").
//     Pilha vazia == celula nula.
//   * Uma camada de tiles sempre cobre a area em PIXELS inteira do mapa,
//     independentemente do proprio tamanho de tile (multigrade).
//   * `firstgid` e recalculado sequencialmente (reindexTilesetGids).
// ============================================================================
#pragma once

#include <QColor>
#include <QJsonArray>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QSet>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QVector>

namespace core {

// ---------------------------------------------------------------- utilidades
/// Gera um id unico legivel (equivalente ao idGen() do JS).
QString idGen();

/// Limita v ao intervalo [a,b] (equivalente ao clamp() do JS).
inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
inline double clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

// ------------------------------------------------------------------ TileRef
/// Referencia a um tile dentro de um tileset, opcionalmente etiquetada com o
/// Wang Set/cor que a pintou (usado pelo pincel de terreno para recalcular
/// vizinhancas — igual aos campos wangSetId/wangColorId do JS).
struct TileRef {
    int     tilesetIdx = -1;
    int     tx = 0;
    int     ty = 0;
    QString wangSetId;              ///< vazio = tile nao pertence a um Wang Set
    int     wangColorId = -1;

    bool isValid() const { return tilesetIdx >= 0; }
    bool sameTile(const TileRef& o) const {
        return tilesetIdx == o.tilesetIdx && tx == o.tx && ty == o.ty;
    }
    bool operator==(const TileRef& o) const {
        return sameTile(o) && wangSetId == o.wangSetId && wangColorId == o.wangColorId;
    }
    bool operator!=(const TileRef& o) const { return !(*this == o); }
    /// Chave "tilesetIdx:tx:ty" (starKey / collisionKey / tileProbKey do JS).
    QString key() const { return QString::number(tilesetIdx) % QLatin1Char(':') %
                                 QString::number(tx) % QLatin1Char(':') % QString::number(ty); }
};

/// Pilha de tiles de uma celula. Vazia == celula sem tile.
using Cell = QVector<TileRef>;

/// Chave "tilesetIdx:tx:ty" usada pelos mapas de marcadores/probabilidade.
inline QString tileKey(int tilesetIdx, int tx, int ty) {
    return QString::number(tilesetIdx) % QLatin1Char(':') % QString::number(tx) %
           QLatin1Char(':') % QString::number(ty);
}

// ------------------------------------------------------------------ Tileset
/// Bloco de imagem-fonte dentro de um tileset combinado (nunca e refatiado:
/// cada imagem entra inteira para nao embaralhar paredes/telhados).
struct CombinedSource {
    QString name;
    QString sourcePath;           ///< imagem-fonte vinculada; relativa ao projeto quando possível
    int x = 0, y = 0;            ///< posicao do bloco dentro deste atlas, em tiles
    int cols = 0, rows = 0;
    int count = 0;
    // Quando um atlas grande e dividido automaticamente, varias partes podem
    // continuar apontando para a MESMA imagem-fonte. Estes offsets dizem qual
    // recorte da fonte pertence a este bloco e mantem o Live Reload funcional.
    int sourceTileX = 0, sourceTileY = 0;
};

/// Um autotile animado ocupa uma regiao logica (frame 0) e uma ou mais
/// regioes fisicas equivalentes no mesmo atlas. Mapas/Wang guardam sempre as
/// coordenadas do frame 0; o renderer escolhe apenas a origem visual do frame.
/// Isso preserva colisao, estrela, terreno/Wang e compatibilidade com mapas
/// antigos sem criar um segundo tipo de TileRef.
struct AnimatedAutotile {
    QString id = idGen();
    QString name;
    int baseX = 0, baseY = 0;
    int cols = 0, rows = 0;
    QVector<QPoint> frameOrigins;    ///< frame 0 incluido; coordenadas em tiles
    double fps = 6.0;
    bool loop = true;
    bool pingPong = false;
    bool synchronized = true;       ///< false = fase deterministica por celula

    QRect baseRect() const { return QRect(baseX, baseY, cols, rows); }
    int frameCount() const { return frameOrigins.size(); }
    bool valid() const { return cols > 0 && rows > 0 && !frameOrigins.isEmpty(); }
};

/// Recurso semantico de Autotile pertencente ao PROJETO. O atlas fisico pode
/// viver em um Tileset normal legado ou em um atlas interno/oculto criado pelo
/// importador, mas essa e uma decisao de armazenamento: autoria, biblioteca,
/// Terrain/Wang e persistencia usam esta identidade global como fonte unica.

// ------------------------------------------------ efeitos por recurso de tile
/// Efeito visual associado ao RECURSO do tile/autotile. Diferente de TileEffect
/// (que pinta uma area do mapa), este binding acompanha o tileset e e aplicado
/// automaticamente a toda ocorrencia do recurso.
struct TileResourceEffectBinding {
    QString type;
    QString preset;
    bool enabled = true;
    QHash<QString, QString> properties;
};

inline QString resourceEffectPreset(const QVector<TileResourceEffectBinding>& effects, const QString& type)
{
    for (const TileResourceEffectBinding& effect : effects)
        if (effect.enabled && effect.type == type) return effect.preset;
    return QString();
}

inline void setResourceEffectPreset(QVector<TileResourceEffectBinding>& effects, const QString& type, const QString& preset)
{
    for (int i = effects.size() - 1; i >= 0; --i) {
        if (effects[i].type != type) continue;
        if (preset.trimmed().isEmpty()) effects.removeAt(i);
        else { effects[i].preset = preset.trimmed(); effects[i].enabled = true; }
        return;
    }
    if (!preset.trimmed().isEmpty()) {
        TileResourceEffectBinding effect; effect.type = type; effect.preset = preset.trimmed();
        effects.push_back(effect);
    }
}

inline QString resourceEffectProperty(const QVector<TileResourceEffectBinding>& effects, const QString& type,
                                      const QString& key, const QString& fallback = QString())
{
    for (const TileResourceEffectBinding& effect : effects) {
        if (!effect.enabled || effect.type != type) continue;
        return effect.properties.value(key, fallback);
    }
    return fallback;
}

inline int resourceEffectOpacityPercent(const QVector<TileResourceEffectBinding>& effects, const QString& type)
{
    bool ok = false;
    const int value = resourceEffectProperty(effects, type, QStringLiteral("opacityPercent")).toInt(&ok);
    return ok ? clampi(value, 0, 100) : -1; // -1 = usar opacidade original do preset
}

inline void setResourceEffectOpacityPercent(QVector<TileResourceEffectBinding>& effects, const QString& type, int percent)
{
    for (TileResourceEffectBinding& effect : effects) {
        if (effect.type != type) continue;
        if (percent < 0) effect.properties.remove(QStringLiteral("opacityPercent"));
        else effect.properties.insert(QStringLiteral("opacityPercent"), QString::number(clampi(percent, 0, 100)));
        return;
    }
}

inline QString resourceEffectBlurMode(const QVector<TileResourceEffectBinding>& effects, const QString& type)
{
    const QString value = resourceEffectProperty(effects, type, QStringLiteral("blurMode"), QStringLiteral("none")).trimmed().toLower();
    if (value == QLatin1String("horizontal") || value == QLatin1String("vertical") || value == QLatin1String("both"))
        return value;
    return QStringLiteral("none");
}

inline int resourceEffectBlurStrength(const QVector<TileResourceEffectBinding>& effects, const QString& type)
{
    bool ok = false;
    const int value = resourceEffectProperty(effects, type, QStringLiteral("blurStrength")).toInt(&ok);
    return ok ? clampi(value, 0, 24) : 0;
}

inline void setResourceEffectBlur(QVector<TileResourceEffectBinding>& effects, const QString& type,
                                  const QString& mode, int strength)
{
    const QString normalizedMode = mode.trimmed().toLower();
    const QString finalMode = (normalizedMode == QLatin1String("horizontal") ||
                               normalizedMode == QLatin1String("vertical") ||
                               normalizedMode == QLatin1String("both"))
        ? normalizedMode : QStringLiteral("none");
    const int finalStrength = clampi(strength, 0, 24);
    for (TileResourceEffectBinding& effect : effects) {
        if (effect.type != type) continue;
        if (finalMode == QLatin1String("none") || finalStrength <= 0) {
            effect.properties.remove(QStringLiteral("blurMode"));
            effect.properties.remove(QStringLiteral("blurStrength"));
        } else {
            effect.properties.insert(QStringLiteral("blurMode"), finalMode);
            effect.properties.insert(QStringLiteral("blurStrength"), QString::number(finalStrength));
        }
        return;
    }
}

struct TilesetAutotile {
    QString id = idGen();
    QString name;
    QString category;              ///< organizacao da biblioteca global; vazio = sem categoria
    QString tilesetId;              ///< owner fisico por ID estavel (nunca por indice visual)
    // Regiao logica do recurso dentro do atlas. Para animacao aponta sempre
    // para o frame 0; para Terrain legado e preenchida a partir dos tiles Wang.
    int     baseX = 0, baseY = 0;
    int     cols = 0, rows = 0;
    QString wangSetId;              ///< Terrain/Wang associado; vazio = sem Terrain
    int     wangColorId = -1;       ///< cor/terreno dentro do WangSet
    QString animatedAutotileId;     ///< opcional: animacao fisica no mesmo atlas
    int     previewTx = -1;          ///< tile representativo na paleta
    int     previewTy = -1;          ///< por padrao: primeiro tile da ultima fileira
    bool    extendAtMapBoundary = true; ///< borda do mapa continua o terreno (2.0)
    QVector<TileResourceEffectBinding> resourceEffects; ///< efeitos por recurso (ex.: reflection/puddle)

    QRect baseRect() const { return QRect(baseX, baseY, cols, rows); }
    bool hasRegion() const { return cols > 0 && rows > 0; }
    bool hasTerrain() const { return !wangSetId.isEmpty() && wangColorId >= 0; }
    bool animated() const { return !animatedAutotileId.isEmpty(); }
};

struct Tileset {
    QString id = idGen();
    QString name;
    QString category;
    int     firstgid = 1;
    QImage  image;
    QString sourcePath;             ///< caminho original (informativo)
    // Origem logica dentro da imagem-fonte. Zero para tilesets normais;
    // diferente de zero para partes criadas pelo split automatico de atlas.
    int     sourceTileX = 0, sourceTileY = 0;
    int     imagewidth = 0, imageheight = 0;
    int     tilewidth = 32, tileheight = 32;
    int     spacing = 0, margin = 0;
    int     columns = 0, rows = 0, tilecount = 0;
    bool    isVX512 = false;        ///< atlas 512x512 tipico do atlas 512×512
    bool    internalAutotileAtlas = false; ///< backing store oculto; nao aparece como Tileset normal
    /// Controla somente a paleta principal. O recurso continua existindo no
    /// Gerenciador, no projeto e no runtime mesmo quando oculto. Isso evita
    /// poluir a lista com tilesets auxiliares gerados por Bake.
    bool    paletteVisible = true;

    // --- páginas de Tileset ------------------------------------------------
    // Um Tileset lógico pode ocupar vários atlas físicos de até 4096×4096.
    // pageGroupId identifica o conjunto; pageIndex é 0-based e serve apenas
    // para ordenação/UX. O nome lógico é compartilhado entre todas as páginas.
    // Projetos antigos deixam pageGroupId vazio e são tratados como página única.
    QString pageGroupId;
    int     pageIndex = 0;

    // Tileset gerado por Bake de Object Layer. E um tileset normal para todas
    // as ferramentas (Patterns, Empilhar, Random etc.); os campos abaixo sao
    // apenas metadados de origem para UX/manutencao futura.
    bool    generatedFromBake = false;
    QString bakeSourceLayerId;
    // Tilesets gerados pela ferramenta Inclinação. Continuam sendo tilesets
    // normais/editáveis; os metadados apenas preservam a origem para UX.
    bool    generatedFromSlope = false;
    QString slopeSourceLayerId;
    QString slopeAxis;              ///< horizontal | vertical
    double  slopeStep = 0.0;

    // --- cor de transparencia (chroma key) aplicada na criacao --------------
    bool    chromaApplied = false;
    QColor  chromaColor;
    int     chromaTolerance = 0;

    // --- tileset combinado -------------------------------------------------
    bool                   combined = false;
    QVector<CombinedSource> combinedSources;
    int                    combinedTileCount = 0;
    QString                combinedDirection = QStringLiteral("horizontal");
    int                    packCursorX = 0, packCursorY = 0;

    // --- backing fisico de animacao ---------------------------------------
    // Identidades semanticas de Autotile ficam em Editor::autotiles.
    QVector<AnimatedAutotile> animatedAutotiles;

    // --- prioridade visual por tile ---------------------------------------
    // 0 = tile normal. 1..5 = participa da ordenacao Y com atores. O valor
    // fica no proprio Tileset (e nao em estado paralelo do Editor) para que
    // duplicacao, snapshot runtime, export e persistencia carreguem a mesma
    // semantica. Armazenamento esparso evita inflar projetos com milhares de
    // zeros. A chave local e "tx:ty" e continua estavel se o tileset for
    // movido/reordenado na lista global.
    QHash<QString, int> tilePriorities;

    // --- colisao por tile --------------------------------------------------
    // Fonte de verdade 2.0. 0 = livre; bits 1/2/4/8 = cima/direita/baixo/
    // esquerda. O Editor principal pode aplicar 0/15, enquanto o Gerenciador
    // edita os lados individualmente sem manter uma segunda copia global.
    QHash<QString, int> tileCollisionMasks;

    // --- probabilidade/peso de pintura ------------------------------------
    // 1.0 = padrao e portanto nao e persistido. Mantido no Tileset pela mesma
    // razao que prioridade/colisao: duplicacao, snapshot e export carregam o
    // recurso completo sem depender de indices globais do Editor.
    QHash<QString, double> tileProbabilities;
    QHash<QString, QVector<TileResourceEffectBinding>> tileResourceEffects; ///< chave tx:ty

    static QString priorityKey(int tx, int ty) {
        return QString::number(tx) % QLatin1Char(':') % QString::number(ty);
    }
    int tilePriority(int tx, int ty) const {
        if (!contains(tx, ty)) return 0;
        return clampi(tilePriorities.value(priorityKey(tx, ty), 0), 0, 5);
    }
    void setTilePriority(int tx, int ty, int priority) {
        if (!contains(tx, ty)) return;
        const QString key = priorityKey(tx, ty);
        const int normalized = clampi(priority, 0, 5);
        if (normalized == 0) tilePriorities.remove(key);
        else tilePriorities.insert(key, normalized);
    }
    int tileCollisionMask(int tx, int ty) const {
        if (!contains(tx, ty)) return 0;
        return tileCollisionMasks.value(priorityKey(tx, ty), 0) & 0x0f;
    }
    void setTileCollisionMask(int tx, int ty, int mask) {
        if (!contains(tx, ty)) return;
        const QString key = priorityKey(tx, ty);
        const int normalized = mask & 0x0f;
        if (normalized == 0) tileCollisionMasks.remove(key);
        else tileCollisionMasks.insert(key, normalized);
    }
    double tileProbability(int tx, int ty) const {
        if (!contains(tx, ty)) return 1.0;
        return tileProbabilities.value(priorityKey(tx, ty), 1.0);
    }
    void setTileProbability(int tx, int ty, double probability) {
        if (!contains(tx, ty)) return;
        const QString key = priorityKey(tx, ty);
        if (qFuzzyCompare(probability, 1.0)) tileProbabilities.remove(key);
        else tileProbabilities.insert(key, probability);
    }


    QString tileEffectPreset(int tx, int ty, const QString& type) const {
        if (!contains(tx, ty)) return QString();
        return resourceEffectPreset(tileResourceEffects.value(priorityKey(tx, ty)), type);
    }
    void setTileEffectPreset(int tx, int ty, const QString& type, const QString& preset) {
        if (!contains(tx, ty)) return;
        const QString key = priorityKey(tx, ty);
        QVector<TileResourceEffectBinding> effects = tileResourceEffects.value(key);
        setResourceEffectPreset(effects, type, preset);
        if (effects.isEmpty()) tileResourceEffects.remove(key);
        else tileResourceEffects.insert(key, effects);
    }


    /// Retangulo (em pixels) do tile (tx,ty) dentro da imagem, considerando
    /// margem e espacamento — mesma conta do drawLayerToCtx original.
    QRect tileRect(int tx, int ty) const {
        return QRect(margin + tx * (tilewidth + spacing),
                     margin + ty * (tileheight + spacing), tilewidth, tileheight);
    }
    bool contains(int tx, int ty) const {
        return tx >= 0 && ty >= 0 && tx < columns && ty < rows;
    }
    /// Recalcula colunas/linhas/contagem a partir da imagem e da grade.
    void recomputeGrid();
};

// ------------------------------------------------------------------- Objeto
/// Objeto de uma camada de objetos (retangulo livre, podendo carregar tiles).
struct MapObject {
    QString         id = idGen();
    QString         name;
    QString         type;
    double          x = 0, y = 0, w = 0, h = 0;
    double          rotation = 0;
    /// Filtro visual usado quando rotation nao e multiplo de 90 graus.
    /// "rotsprite" preserva pixel art; "nearest" e "smooth" ficam
    /// disponiveis para casos especiais/arte HD. Projetos antigos usam
    /// RotSprite automaticamente.
    QString         rotationFilter = QStringLiteral("rotsprite");
    /// Filtro usado quando largura/altura diferem do tamanho nativo do stamp.
    /// "xbr" aplica ampliacao edge-aware para pixel art; "nearest" preserva
    /// pixels duros e "smooth" usa interpolacao bilinear.
    QString         scaleFilter = QStringLiteral("nearest");
    bool            visible = true;
    QVector<TileRef> tiles;         ///< tiles desenhados dentro do objeto
    int             stampW = 1, stampH = 1;  ///< dimensoes do stamp de origem
    QHash<QString, QString> properties;

    QRectF rect() const { return QRectF(x, y, w, h); }
};


// -------------------------------------------------------- Filtros raster
/// Filtro não destrutivo aplicado ao conteúdo de uma Image/Paint Layer ou à
/// máscara raster de Image/Tile Layer. Os campos são compartilhados para
/// manter o formato compacto e permitir reordenação sem subclasses.
struct RasterLayerFilter {
    QString id = idGen();
    QString type = QStringLiteral("gaussianBlur"); // gaussianBlur | directionalBlur | noise | contactShadow
    bool enabled = true;

    // Desfoques
    double radius = 4.0;          ///< raio/comprimento em pixels
    double strength = 1.0;       ///< 0..1, mistura original -> efeito
    double angle = 0.0;          ///< graus; 0 = direita, 90 = baixo
    int quality = 1;             ///< 0 baixa, 1 média, 2 alta

    // Ruído
    double amount = 0.12;        ///< 0..1
    double scale = 2.0;          ///< tamanho do grão em pixels
    int seed = 1337;
    bool monochrome = true;

    // Sombra de contato / AO 2D
    double distance = 3.0;       ///< deslocamento em pixels
    double spread = 1.0;         ///< expansão suave antes do blur
    double opacity = 0.35;       ///< 0..1
    QColor color = QColor(0, 0, 0, 255);

    bool operator==(const RasterLayerFilter& o) const {
        return id == o.id && type == o.type && enabled == o.enabled &&
               qFuzzyCompare(radius + 1.0, o.radius + 1.0) &&
               qFuzzyCompare(strength + 1.0, o.strength + 1.0) &&
               qFuzzyCompare(angle + 181.0, o.angle + 181.0) && quality == o.quality &&
               qFuzzyCompare(amount + 1.0, o.amount + 1.0) &&
               qFuzzyCompare(scale + 1.0, o.scale + 1.0) && seed == o.seed &&
               monochrome == o.monochrome &&
               qFuzzyCompare(distance + 1.0, o.distance + 1.0) &&
               qFuzzyCompare(spread + 1.0, o.spread + 1.0) &&
               qFuzzyCompare(opacity + 1.0, o.opacity + 1.0) && color == o.color;
    }
    bool operator!=(const RasterLayerFilter& o) const { return !(*this == o); }
};

// -------------------------------------------------------------------- Layer
enum class LayerType { Tile, Object, Image, Group };

struct Layer;
using LayerPtr = QSharedPointer<Layer>;

struct Layer {
    QString   id = idGen();
    QString   name;
    LayerType type = LayerType::Tile;

    bool    visible = true;
    bool    locked = false;
    double  opacity = 1.0;
    QString blendMode = QStringLiteral("source-over");
    int     offsetx = 0, offsety = 0;
    /// "below" (padrao) ou "above": camada inteira desenhada acima do jogador.
    QString zMode = QStringLiteral("below");
    int depthLevel = 0; // Altura física, independente da prioridade Z.
    QColor  uiColor;                 ///< cor opcional da linha na lista
    bool    collapsed = false;

    /// Mascara: e container (tem filhos) mas continua pintavel; os filhos sao
    /// recortados pelo conteudo da propria base.
    bool isMask = false;
    bool maskShowBase = true;

    // --- camada de tiles ---------------------------------------------------
    int  tileWidth = 32, tileHeight = 32;
    int  cols = 0, rows = 0;
    QVector<QVector<Cell>> data2D;   ///< [row][col]

    // --- camada de objetos -------------------------------------------------
    QVector<MapObject> objects;

    // --- camada de imagem --------------------------------------------------
    QImage  image;
    QString imagePath;
    int     imagewidth = 0, imageheight = 0;
    /// Transformacao editorial/runtime da Image Layer. Offset X/Y continua
    /// representando a posicao do canto superior esquerdo antes da rotacao.
    /// A rotacao acontece ao redor do centro visual da imagem escalada.
    double  imageScaleX = 1.0;
    double  imageScaleY = 1.0;
    double  imageRotation = 0.0;
    bool    imageFlipX = false;
    bool    imageFlipY = false;
    /// Repetição editorial/runtime da Image Layer. A imagem-fonte continua
    /// única; o Renderer apenas a ladrilha no eixo solicitado. Paint Layers
    /// permanecem 1:1 e não usam repetição.
    bool    imageRepeatX = false;
    bool    imageRepeatY = false;
    /// Mantém esta imagem separada no runtime para produzir profundidade
    /// relativa à câmera. 1 acompanha o mapa; 0 fica presa à tela.
    bool    parallaxLayer = false;
    double  parallaxFactorX = 0.5;
    double  parallaxFactorY = 0.5;
    double  parallaxSpeedX = 0.0; ///< pixels por segundo
    double  parallaxSpeedY = 0.0;
    /// Camada Visual MZ. O parallax deixa de ser exclusivo de Image Layer:
    /// Tile, Object, Paint e Group podem ser compostos pelo exportador em uma
    /// textura visual, mantendo o conteúdo original editável no projeto.
    bool    parallaxRepeatX = false;
    bool    parallaxRepeatY = false;
    QString parallaxMotionPreset = QStringLiteral("custom");
    double  parallaxOscillationX = 0.0; ///< balanço horizontal em pixels
    double  parallaxOscillationY = 0.0; ///< balanço vertical em pixels
    double  parallaxOscillationSpeed = 1.0; ///< ciclos por segundo
    bool    parallaxSmoothMotion = true; ///< subpixel + filtro linear no runtime MZ

    /// Animação por spritesheet de uma Image Layer usada como Camada Visual.
    /// Os quadros possuem tamanho uniforme e são lidos da esquerda para a
    /// direita, de cima para baixo. Recurso exclusivo do runtime MZ.
    bool    parallaxAnimationEnabled = false;
    int     parallaxAnimationColumns = 1;
    int     parallaxAnimationRows = 1;
    int     parallaxAnimationFrames = 1;
    double  parallaxAnimationFps = 8.0;
    bool    parallaxAnimationPingPong = false;

    /// Pós-efeito dinâmico da Camada Visual no MZ. O preset é intencionalmente
    /// simples para quem não conhece shaders; intensidade e velocidade são os
    /// únicos ajustes necessários na maioria dos casos.
    QString parallaxEffectPreset = QStringLiteral("none");
    double  parallaxEffectStrength = 1.0;
    double  parallaxEffectSpeed = 1.0;
    /// Camada de Reflexo (somente MZ): o alpha define onde a superfície
    /// refletiva existe. É uma máscara de autoria, não parte do cenário.
    bool    reflectionLayer = false;
    QString reflectionPreset = QStringLiteral("still");
    int     reflectionOpacity = 70;
    int     reflectionBlur = 2;
    int     reflectionWave = 4;
    /// "nearest" preserva pixels duros; "bilinear" usa SmoothPixmapTransform.
    QString imageFilter = QStringLiteral("nearest");
    /// true = imagem usada apenas como referência editorial e ignorada em
    /// exportações. false = imagem rasterizada que faz parte real do mapa.
    bool    imageReferenceOnly = true;
    /// true = camada raster criada pelo Paint Brush livre. Continua sendo
    /// uma Image Layer no renderer/exportador, mas recebe ferramentas de
    /// pintura destrutiva no editor em coordenadas de pixel.
    bool    imagePaintLayer = false;
    /// Máscara raster não-destrutiva no estilo Photoshop/Krita. Em Image
    /// Layer usa o espaço local da imagem; em Tile Layer usa o espaço raster
    /// local da grade (cols*tileWidth × rows*tileHeight).
    bool    imageMaskEnabled = false;
    QImage  imageMask;
    /// Bloqueia pixels transparentes durante pintura raster. Em Tile Layer,
    /// quando a máscara está sendo pintada, limita o brush à silhueta dos tiles.
    bool    alphaLock = false;

    /// Pilhas não destrutivas. imageFilters atuam no conteúdo de Image/Paint
    /// Layer. maskFilters atuam sobre a máscara raster, inclusive em Tile Layer.
    QVector<RasterLayerFilter> imageFilters;
    QVector<RasterLayerFilter> maskFilters;

    // --- filhos (grupo ou mascara) ----------------------------------------
    QVector<LayerPtr> children;

    bool isContainer() const { return type == LayerType::Group || isMask; }
    bool isPaintable() const { return type == LayerType::Tile || type == LayerType::Object ||
                                      (type == LayerType::Image && imagePaintLayer); }

    // -- acesso a celulas (equivalente a getCellStack/setCellStack) ---------
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < cols && y < rows; }
    Cell cellAt(int x, int y) const {
        if (!inBounds(x, y)) return Cell();
        return data2D[y][x];
    }
    void setCell(int x, int y, const Cell& stack) {
        if (!inBounds(x, y)) return;
        data2D[y][x] = stack;
    }
    /// Tile do topo da pilha (ou invalido).
    TileRef topAt(int x, int y) const {
        const Cell& c = data2D.value(y).value(x);
        return c.isEmpty() ? TileRef() : c.last();
    }
    /// Redimensiona a grade preservando o conteudo que couber.
    void resizeGrid(int newCols, int newRows);
    /// Cria a grade vazia com o tamanho atual de cols/rows.
    void allocGrid();
    bool isEmptyLayer() const;
};

/// Fabricas equivalentes a makeTileLayer / makeObjectLayer / makeImageLayer / makeGroupLayer.
LayerPtr makeTileLayer(const QString& name, int tw, int th, int cols, int rows);
LayerPtr makeObjectLayer(const QString& name);
LayerPtr makeImageLayer(const QImage& img, const QString& name, const QString& path = QString(),
                        bool referenceOnly = true);
LayerPtr makePaintLayer(const QSize& pixelSize, const QString& name = QStringLiteral("Pintura"));
LayerPtr makeGroupLayer(const QString& name);
/// Copia profunda de uma sub-arvore (novos ids).
LayerPtr cloneLayer(const LayerPtr& src, bool newIds = true);

// ----------------------------------------------------------------- WangSet
struct WangColor {
    int     id = 1;
    QString name;
    QColor  color = QColor("#4a90d7");
    bool    hasIcon = false;
    int     iconTilesetIdx = -1, iconTx = 0, iconTy = 0;
};

/// Rotulos das 8 direcoes de um tile Wang. -1 = sem cor.
struct WangTileData {
    int tl = -1, t = -1, tr = -1, l = -1, r = -1, bl = -1, b = -1, br = -1;
    /// Tile "isolado" (mascara 0): usado quando a celula nao tem nenhum vizinho
    /// do mesmo terreno. Equivale ao campo `_isolatedColorId` da versao web —
    /// precisa existir porque um tile isolado tem as 8 posicoes vazias e, sem
    /// esta marca, seria indistinguivel de um tile sem rotulo nenhum.
    int isolatedColorId = -1;

    bool hasLabels() const {
        return tl >= 0 || t >= 0 || tr >= 0 || l >= 0 || r >= 0 || bl >= 0 || b >= 0 || br >= 0;
    }
    bool isEmpty() const { return !hasLabels() && isolatedColorId < 0; }
    /// E o tile isolado da cor indicada? (8 posicoes vazias + marca)
    bool isIsolatedOf(int colorId) const {
        return !hasLabels() && isolatedColorId == colorId && colorId >= 0;
    }
    int  get(const QString& pos) const;
    void set(const QString& pos, int colorId);
    /// Todas as 8 posicoes, na ordem canonica usada pela UI e pelos atalhos 1..8.
    static QStringList positions();
};

struct WangSet {
    QString id = idGen();
    QString name;
    QString type = QStringLiteral("mixed");   ///< mixed | corner | edge
    bool    hasIcon = false;
    int     iconTilesetIdx = -1, iconTx = 0, iconTy = 0;
    QVector<WangColor>            colors;
    QHash<QString, WangTileData>  tiles;      ///< chave "tilesetIdx:tx,ty"

    static QString tileKeyOf(int tilesetIdx, int tx, int ty) {
        return QString::number(tilesetIdx) % QLatin1Char(':') % QString::number(tx) %
               QLatin1Char(',') % QString::number(ty);
    }
    static bool parseTileKey(const QString& key, int* tilesetIdx, int* tx, int* ty);
    const WangColor* colorById(int id) const;
    WangColor*       colorById(int id);
};

/// Geometria reutilizavel de Wang Tiles (preset), como no state.wangPresets.
struct WangPreset {
    QString id = idGen();
    QString name;
    QString type = QStringLiteral("mixed");
    int     w = 8, h = 6;
    bool    builtin = false;
    /// "x,y" -> conjunto de posicoes marcadas ("tl","t",...)
    QHash<QString, QSet<QString>> positions;
};

// -------------------------------------------------------------- Random pool
/// Entrada do pool aleatorio: um retangulo do tileset (pode ser multi-tile).
struct RandomEntry {
    int             tilesetIdx = -1;
    int             x = 0, y = 0, w = 1, h = 1;
    QVector<TileRef> tiles;      ///< tiles com dx/dy implicitos pela ordem
    QVector<QPoint> offsets;     ///< dx,dy de cada tile
};

// ---------------------------------------------------------------- Selecoes
/// Retangulo selecionado na paleta do tileset (state.tsSel).
struct TilesetSelection {
    int  tilesetIdx = -1;
    int  x = 0, y = 0, w = 1, h = 1;
    bool valid() const { return tilesetIdx >= 0 && w > 0 && h > 0; }
};

/// Stamp multi-tile copiado do MAPA (state.customStamp): sobrepoe tsSel.
struct CustomStamp {
    int              w = 0, h = 0;
    QVector<TileRef> tiles;      ///< paralelo a offsets
    QVector<QPoint>  offsets;
    bool valid() const { return w > 0 && h > 0 && !tiles.isEmpty(); }
    void clear() { w = h = 0; tiles.clear(); offsets.clear(); }
};

/// Stamp "resolvido" pronto para pintar (equivalente ao retorno de currentStampTiles).
struct Stamp {
    int              w = 1, h = 1;
    QVector<TileRef> tiles;
    QVector<QPoint>  offsets;
    bool valid() const { return w > 0 && h > 0 && !tiles.isEmpty(); }
    /// Tile na posicao relativa (rx,ry) do stamp, ou invalido.
    TileRef at(int rx, int ry) const {
        for (int i = 0; i < offsets.size(); ++i)
            if (offsets[i].x() == rx && offsets[i].y() == ry) return tiles[i];
        return TileRef();
    }
    /// Pilha completa na posicao relativa. Patterns capturados de celulas em
    /// modo Empilhar podem conter varios TileRef com o MESMO offset.
    Cell stackAt(int rx, int ry) const {
        Cell out;
        const int count = qMin(tiles.size(), offsets.size());
        for (int i = 0; i < count; ++i)
            if (offsets[i].x() == rx && offsets[i].y() == ry && tiles[i].isValid())
                out.push_back(tiles[i]);
        return out;
    }
};

/// Stamp salvo na biblioteca (Map Creation Suite).
struct SavedStamp {
    QString id = idGen();
    QString name;
    Stamp   stamp;
};

// -------------------------------------------------------------------- Mapa
struct MapInfo {
    bool depthEnabled = false;
    double depthScale = 0.90;
    int depthStartLevel = 0;
    QJsonArray depthTransitions; // endpoints em células: x0,y0 (chão), x1,y1 (ponte)
    int    width = 40, height = 30;      ///< em tiles da grade base
    int    tileWidth = 32, tileHeight = 32;
    QColor background = QColor("#2e2e2e");

    // Panorama editorial do mapa. É desenhado atrás das camadas LUDO e entra
    // no render/export final; não interfere no parallax de referência usado
    // pela integração com o RPG Maker MV/MZ.
    QString panoramaPath;
    QImage  panorama;
    bool    panoramaVisible = false;
    int     panoramaOpacity = 255;
    bool    panoramaFit = false;          ///< esticar para o tamanho do mapa
    bool    panoramaRepeat = false;       ///< repetir a imagem a partir de 0,0

    int pixelWidth()  const { return width * tileWidth; }
    int pixelHeight() const { return height * tileHeight; }
};

// ------------------------------------------------------------------ Pincel
/// Ajustes do pincel avancado (Map Creation Suite V7).
struct BrushSettings {
    int     size = 1;                       ///< raio em tiles (1 = 1 tile; diametro = size*2-1)
    QString shape = QStringLiteral("square");  ///< square | circle | diamond | alpha
    int     density = 100;                  ///< 0..100 (%), multiplicado pela intensidade da máscara
    int     spacing = 1;
    bool    flipH = false, flipV = false;
    int     seed = 173;

    // Brush por alpha mask -------------------------------------------------
    // Estado estritamente de authoring: a imagem não entra no runtime nem no
    // arquivo .ludo. O caminho é persistido pelo EditorSessionStore.
    QString alphaMaskPath;
    QImage  alphaMask;                      ///< grayscale 0..255 preparado uma vez no carregamento
    int     alphaMaskRotation = 0;           ///< graus, sentido horário
    bool    alphaMaskInvert = false;

    bool usesAlphaMask() const {
        return shape == QLatin1String("alpha") && !alphaMask.isNull();
    }
};

/// Pincel raster livre, separado do Brush de Tiles. A ponta pode ser
/// procedural, uma alpha mask colorizada ou uma imagem RGBA completa.
struct RasterBrushSettings {
    // O mesmo motor atende pintura livre e Pixel Art. `authoringMode` muda
    // rasterização/interpolação/coordenadas, sem criar uma segunda ferramenta.
    QString authoringMode = QStringLiteral("normal"); ///< normal | pixel-art
    int sizePx = 64;                    ///< diâmetro físico do modo normal
    int opacity = 100;                  ///< opacidade máxima do stroke, 0..100
    int flow = 100;                     ///< alpha por dab, 0..100
    int hardness = 80;                  ///< borda do círculo procedural, 0..100
    int spacingPercent = 20;            ///< distância entre dabs em % do tamanho
    QColor color = QColor(90, 70, 55);  ///< cor usada em procedural/alpha
    QString tipMode = QStringLiteral("round"); ///< round | alpha | color
    QString tipImagePath;
    QImage tipImage;
    int rotation = 0;                   ///< rotação-base da ponta
    bool rotateToStroke = false;
    int scatterPercent = 0;             ///< deslocamento perpendicular, % do tamanho
    int sizeJitter = 0;                 ///< 0..100
    int rotationJitter = 0;             ///< amplitude aleatória em graus
    QString blendMode = QStringLiteral("source-over");

    // Pixel Art -----------------------------------------------------------
    // pixelScale amplia cada pixel artístico como bloco inteiro, sempre com
    // nearest-neighbour. O canvas não muda de resolução: é só o passo do brush.
    int pixelSize = 1;                          ///< diâmetro em pixels artísticos, independente do modo normal
    int pixelScale = 1;                         ///< 1..8 pixels reais por pixel artístico
    QString pixelShape = QStringLiteral("square"); ///< square | circle (procedural)
    QString pixelDither = QStringLiteral("none");  ///< none | 25 | 50 | 75
    bool pixelMirrorH = false;
    bool pixelMirrorV = false;
    bool pixelReplaceEnabled = false;
    QColor pixelReplaceColor = QColor(0, 0, 0, 255); ///< comparação exata RGBA

    bool pixelArt() const { return authoringMode == QLatin1String("pixel-art"); }

    // Mesclagem orgânica da borda da imagem. Os nomes antigos permanecem
    // internamente para manter compatibilidade com sessões já salvas. A borda
    // é encontrada pelo alpha real da textura, não pelo retângulo do arquivo.
    bool softenImageEdges = false;
    int edgeSoftnessPercent = 18;      ///< área do contorno afetada, 1..50%
    int edgeSoftnessStrength = 70;     ///< força da mesclagem, 0..100%
    int edgeIrregularityPercent = 30;  ///< quebra orgânica do contorno, 0..100%
    bool preserveEdgeCenter = true;    ///< mantém miolo/detalhes intactos
};

/// Ferramentas disponiveis (data-tool do HTML).
enum class Tool { Stamp, Eraser, Fill, Rect, Circle, Line, Terrain, Select, Object, Paint, Slope };
QString toolId(Tool t);
Tool    toolFromId(const QString& id);
QString toolLabel(Tool t);

} // namespace core
