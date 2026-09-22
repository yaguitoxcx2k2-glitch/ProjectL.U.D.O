// ============================================================================
//  TilesetOps.h — Criacao de tilesets e importacao de AutoTile.
//  Porte de: makeTileset, blockSize, packBlocks, makeCombinedTileset,
//  appendToCombinedTileset, insercao manual e o modulo de importacao de
//  autotiles usado pela LUDO.
// ============================================================================
#pragma once

#include "Editor.h"

namespace core {

bool isBlankTile(const Editor& ed, const TileRef& tile);

// ----------------------------------------------------------------- tilesets
/// Cria um tileset simples a partir de uma imagem.
Tileset makeTileset(const QImage& img, const QString& name, int tileWidth, int tileHeight,
                    int spacing, int margin, const QString& sourcePath = QString());

/// Divide um atlas que exceda o limite de textura em partes alinhadas a tiles.
/// As partes preservam metadados e continuam vinculadas à mesma imagem-fonte;
/// sourceTileX/sourceTileY permitem Live Reload do recorte correto. Se o atlas
/// já couber, devolve um vetor com uma cópia única.
QVector<Tileset> splitTilesetForTextureLimit(const Tileset& source, int maxTextureSize = 4096);

/// Identidade lógica de um conjunto de páginas. Projetos antigos sem metadado
/// usam o próprio ID físico e, portanto, continuam sendo uma página única.
QString tilesetPageGroupKey(const Tileset& ts);

/// Índices físicos das páginas do mesmo Tileset lógico, ordenados por página.
QVector<int> tilesetPageIndices(const Editor& ed, int anyPageIndex);

/// Reordena pageIndex=0..N-1 para um grupo depois de inserir/excluir páginas.
void renumberTilesetPages(Editor& ed, const QString& pageGroupId);

struct NamedImage { QImage img; QString name; QString sourcePath; };

// -------------------------------------------------------- charset (personagem)
/// Descobre a grade de uma folha de personagem olhando as FAIXAS totalmente
/// transparentes entre os quadros: cada bloco de conteudo e um quadro.
/// Muitas folhas (formatos de atlas comuns, Time Fantasy, RTPs) vem com essa folga.
/// Devolve QSize(colunas, linhas) ou um QSize invalido quando nao da para
/// afirmar nada (folha sem folgas, ou grade que nao divide a imagem inteira).
QSize detectCharsetGrid(const QImage& sheet);

/// Torna transparente todo pixel cuja cor esteja a no maximo `tolerance` de
/// distancia (por canal) de `key`. Usado para sheets antigas que vem com fundo
/// solido (magenta, ciano...) em vez de canal alfa.
/// Devolve quantos pixels foram afetados.
int applyChromaKey(QImage& img, const QColor& key, int tolerance);

/// Quantos pixels seriam afetados, sem alterar a imagem (para o preview).
int countChromaKeyPixels(const QImage& img, const QColor& key, int tolerance);

/// Tileset combinado: cada imagem entra como BLOCO INTEIRO (nunca refatiada),
/// empacotada em fileira unica (horizontal) ou coluna unica (vertical).
Tileset makeCombinedTileset(const QVector<NamedImage>& images, const QString& name,
                            int tileWidth, int tileHeight, const QString& direction);

/// Acrescenta imagens a um tileset combinado SEM mover os blocos ja colocados.
/// Devolve quantos tiles foram acrescentados.
int appendToCombinedTileset(Tileset& ts, const QVector<NamedImage>& images);

// ---------------------------- insercao em posicao exata -------------------
struct InsertAtResult {
    int addedCols = 0;      ///< colunas acrescentadas ao atlas
    int addedRows = 0;      ///< linhas acrescentadas
    int overwritten = 0;    ///< tiles nao vazios que foram cobertos
};

/// Copia um retangulo de tiles do tileset para uma imagem "limpa", sem
/// espacamento nem margem — util para alimentar a importacao de autotiles com
/// um recorte do proprio chipset.
QImage extractTileBlock(const Tileset& ts, int tx, int ty, int cols, int rows);

/// Quantos tiles NAO vazios seriam cobertos ao colocar `img` em (atCol,atRow).
int countOverwrittenTiles(const Tileset& ts, const QImage& img, int atCol, int atRow);

/// Primeira posicao (coluna,linha) onde `img` cabe sem cobrir nenhum tile —
/// procura de cima para baixo; se nao houver espaco, devolve a linha logo
/// abaixo do conteudo atual.
QPoint findFreeSlot(const Tileset& ts, const QImage& img);

/// Insere `img` numa posicao exata em tiles, fazendo o atlas crescer se
/// necessario. Nada do que ja existe muda de lugar (as coordenadas (tx,ty) dos
/// tiles ja pintados no mapa continuam validas).
bool insertImageAt(Tileset& ts, const QImage& img, int atCol, int atRow,
                   InsertAtResult* out, QString* error);

/// Insere uma fonte nomeada em posicao escolhida pelo usuario e registra o
/// bloco em combinedSources. Preserva todas as coordenadas ja usadas no mapa.
bool insertCombinedSourceAt(Tileset& ts, const NamedImage& image, int atCol, int atRow,
                            InsertAtResult* out, QString* error);

// ------------------------- vínculo/atualização de fontes ------------------
/// Caminhos absolutos atualmente vinculados ao tileset (sem duplicatas).
QStringList tilesetSourceFiles(const Editor& ed, int tilesetIdx);

/// Recarrega a imagem do tileset sem alterar coordenadas dos tiles. Quando
/// changedAbsolutePath não está vazio, atualiza apenas os blocos ligados a
/// esse arquivo. Retorna true somente quando pixels foram substituídos.
bool reloadTilesetSources(Editor& ed, int tilesetIdx,
                          const QString& changedAbsolutePath = QString(),
                          QString* error = nullptr, QStringList* updatedSources = nullptr);

/// Vincula/substitui a fonte de um tileset existente preservando a geometria.
/// Para tileset simples use sourceIndex=-1; para combinado informe o índice do
/// bloco em combinedSources. A nova imagem precisa ocupar a mesma grade lógica.
bool bindTilesetSource(Editor& ed, int tilesetIdx, int sourceIndex,
                       const QString& absolutePath, QString* error = nullptr);

// ------------------------------------------------ autotile animado --------
/// Retorna a definicao que cobre a coordenada. Quando includeFrames=true,
/// coordenadas de qualquer frame fisico tambem sao reconhecidas.
const AnimatedAutotile* animatedAutotileAt(const Tileset& ts, int tx, int ty,
                                           bool includeFrames = true,
                                           int* physicalFrame = nullptr,
                                           QPoint* local = nullptr);

/// Converte uma coordenada de qualquer frame para a coordenada logica do
/// frame 0. E usada pela paleta/Wang para nunca gravar frame 2/3 no mapa.
QPoint canonicalAnimatedTile(const Tileset& ts, int tx, int ty);

/// Frame visual no instante indicado. `instanceSeed` so e considerado quando
/// synchronized=false e garante fase estavel por celula/evento.
int animatedAutotileFrame(const AnimatedAutotile& anim, qint64 elapsedMs,
                          quint32 instanceSeed = 0);

/// Source rect visual para um tile logico. Para tiles estaticos equivale a
/// Tileset::tileRect().
QRect animatedTileRect(const Tileset& ts, int tx, int ty, qint64 elapsedMs,
                       quint32 instanceSeed = 0);

/// Assinatura barata dos frames ativos. O QRhi usa isto para reconstruir o
/// cache de chunks somente quando algum frame realmente muda.
quint64 animatedTilesetFrameSignature(const Editor& ed, qint64 elapsedMs);

/// Remove metadados de animacao cuja area fisica seja tocada por `tilesArea`.
/// Evita animacoes fantasmas quando o Gerenciador substitui uma parte do atlas.
int removeAnimatedAutotilesOverlapping(Tileset& ts, const QRect& tilesArea);

// ------------------------------------------------- importacao de AutoTile
namespace autotile {

/// Uma selecao feita no preview de entrada: posicao em pixels + modo.
struct Selection { int x = 0; int y = 0; int mode = 1; };

/// Como os blocos convertidos sao dispostos no atlas de saida.
enum class Layout {
    Vertical,    ///< um abaixo do outro (padrao)
    Horizontal,  ///< um ao lado do outro
    AsInput      ///< espelha a posicao dos blocos na imagem de entrada (JS original)
};

/// Modo sugerido pelo tamanho (em tiles) de uma selecao. 0 = nenhum modo bate.
int suggestModeForSelection(int cols, int rows);

/// Nome curto do modo, para menus e combos.
QString modeName(int mode);

/// Quantos blocos do modo cabem na selecao. Devolve (0,0) se nao divide certo.
QSize blocksInSelection(int cols, int rows, int mode);

/// Estado do conversor (equivalente ao objeto `atc` do JS).
struct Converter {
    QImage             img;
    int                tileSize = 32;
    int                mode = 1;
    QVector<Selection> selections;
    QString            baseName = QStringLiteral("autotile");
    /// Empilhamento vertical por padrao: com varios autotiles, o modo original
    /// gerava um atlas larguissimo (12 tiles por autotile lado a lado), ruim de
    /// usar na paleta, que rola na vertical.
    Layout             layout = Layout::Vertical;

    /// Canto superior-esquerdo (em pixels) do bloco de saida de `index`.
    QPoint originFor(int index) const;

    /// Alinha um valor ao centro do tile mais proximo (atcSnap).
    int  snap(int v) const;
    /// Retangulo (x,y,w,h) de uma selecao (atcSelRect).
    QRect selRect(int x, int y, int mode) const;
    /// Tamanho ja empacotado a esquerda/acima do limite (atcPackedSize).
    QPoint packedSize(int limitX, int limitY, bool relative) const;
    /// Alterna a selecao sob o ponto clicado (clique de novo remove).
    void toggleAt(int px, int py);
    /// Gera a imagem de saida no formato de atlas usado pela LUDO.
    QImage buildOutput() const;
    /// Tamanho da imagem de saida sem gerar os pixels.
    QSize outputSize() const;
    /// Descricao para a linha de informacao da UI.
    QString info() const;
};

/// Monta um conversor cuja FONTE e um recorte do tileset (em vez de um arquivo
/// externo), ja fatiado em blocos do tamanho do modo. E o caminho usado pelo
/// item "Gerar autotile" do menu de contexto da paleta.
Converter fromTilesetBlock(const Tileset& ts, int tx, int ty, int cols, int rows, int mode);

} // namespace autotile

} // namespace core
