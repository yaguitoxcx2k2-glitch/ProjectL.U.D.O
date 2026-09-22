// ============================================================================
//  Dialogs.h — Todos os modais da ferramenta web convertidos em QDialog:
//    #tilesetModal          -> NewTilesetDialog
//    #combinedTilesetModal  -> CombinedTilesetDialog
//    #atcModal              -> AutoTileConverterDialog
//    "Redimensionar mapa"   -> ResizeMapDialog
//    "Atalhos"/"Sobre"      -> ShortcutsDialog
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/EventModel.h"
#include "core/TilesetOps.h"
#include "ui/maps/ResizeMapDialog.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QTimer;
class QLabel;
class QLineEdit;
class QListWidget;
class QElapsedTimer;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;
class QSpinBox;
class QPushButton;
class QGroupBox;
class QTextBrowser;
class QVBoxLayout;
QT_END_NAMESPACE

namespace core {
struct ProjectReferenceLocation;
}

namespace ui {

class CommandPreviewDialog;
class NarrativePreviewWidget;
class CommandInspector;

class MapDestinationPreview;
class TextEffectsEditorWidget;

/// Escolhe o único mapa/célula de início do jogo por meio de um preview
/// clicável. Não altera o projeto até a janela de configurações ser aceita.
class StartPositionDialog : public QDialog
{
    Q_OBJECT
public:
    StartPositionDialog(const core::Editor& ed, const QString& mapId,
                        const QPoint& position, QWidget* parent = nullptr);
    QString selectedMapId() const;
    QPoint selectedPosition() const { return m_position; }

private:
    const core::Editor& ed;
    QComboBox* m_map = nullptr;
    QLabel* m_location = nullptr;
    MapDestinationPreview* m_preview = nullptr;
    QPoint m_position;
};

// --------------------------------------------------------- Novo tileset
class TilesetPreview;

class NewTilesetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewTilesetDialog(core::Editor& ed, QWidget* parent = nullptr);
    core::Tileset result() const { return m_result; }

private:
    void pickImage();
    void updatePreview();

    /// Aplica a cor de transparencia escolhida sobre a imagem original.
    QImage processedImage() const;
    void   setChromaColor(const QColor& c);
    void   updateChromaUi();

    core::Editor&  ed;
    QImage         m_image;          ///< imagem original, intacta
    QString        m_path;
    core::Tileset  m_result;
    QLineEdit*     m_name = nullptr;
    QSpinBox      *m_tw = nullptr, *m_th = nullptr, *m_spacing = nullptr, *m_margin = nullptr;
    QLabel*        m_info = nullptr;
    TilesetPreview* m_preview = nullptr;
    QPushButton*   m_okBtn = nullptr;

    // cor de transparencia (chroma key)
    QCheckBox*   m_chromaOn = nullptr;
    QPushButton* m_chromaSwatch = nullptr;
    QPushButton* m_chromaPick = nullptr;
    QPushButton* m_chromaCorner = nullptr;
    QSpinBox*    m_chromaTol = nullptr;
    QLabel*      m_chromaInfo = nullptr;
    QColor       m_chromaColor;
};

// ------------------------------------------ Gerar autotile da paleta
/// Converte um recorte do proprio tileset em um ou mais recursos Autotile
/// independentes. O recorte e somente fonte: o Tileset normal nunca e
/// redimensionado, sobrescrito ou usado como destino da conversao.
class GenerateAutotileDialog : public QDialog
{
    Q_OBJECT
public:
    GenerateAutotileDialog(core::Editor& ed, int srcTilesetIdx,
                           int tx, int ty, int cols, int rows, QWidget* parent = nullptr);

private:
    void rebuildAtlas();          ///< regenera a saida quando o modo muda
    void doCreateAutotile();
    void doSavePng();

    core::Editor& ed;
    int m_srcIdx, m_tx, m_ty, m_cols, m_rows;
    core::autotile::Converter conv;
    QImage m_atlas;

    QComboBox*   m_mode = nullptr;
    QLabel      *m_srcPreview = nullptr, *m_outPreview = nullptr;
    QLabel      *m_info = nullptr;
};

// --------------------------------------------- Exportar para engines
class ScaledExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScaledExportDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    void updateSummary();
    void doExport();

    core::Editor& ed;
    QSpinBox*  m_tile = nullptr;
    QLineEdit* m_dir = nullptr;
    QCheckBox *m_optPng = nullptr, *m_optTmj = nullptr, *m_optTilesets = nullptr,
              *m_optStar = nullptr, *m_optRm2k = nullptr;
    QLabel*    m_summary = nullptr;
    QLabel*    m_warnings = nullptr;
};

// ---------------------------------------------------- Tileset combinado
class CombinedTilesetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CombinedTilesetDialog(core::Editor& ed, QWidget* parent = nullptr);
    core::Tileset result() const { return m_result; }

private:
    void addImages();
    void updateInfo();

    /// Imagens com a cor de transparencia ja aplicada (uma por uma).
    QVector<core::NamedImage> processedImages() const;
    void setChromaColor(const QColor& c);
    void updateChromaUi();

    core::Editor&                 ed;
    QVector<core::NamedImage>     m_images;
    core::Tileset                 m_result;
    QListWidget*                  m_list = nullptr;
    QLineEdit*                    m_name = nullptr;
    QSpinBox                     *m_tw = nullptr, *m_th = nullptr;
    QComboBox*                    m_direction = nullptr;
    QLabel*                       m_info = nullptr;
    TilesetPreview*               m_preview = nullptr;

    // cor de transparencia
    QCheckBox*   m_chromaOn = nullptr;
    QPushButton* m_chromaSwatch = nullptr;
    QPushButton* m_chromaPick = nullptr;
    QCheckBox*   m_chromaPerImage = nullptr;
    QSpinBox*    m_chromaTol = nullptr;
    QLabel*      m_chromaInfo = nullptr;
    QColor       m_chromaColor;
};

// ------------------------------------------------- Importar AutoTile
/// Preview clicavel da imagem de entrada do conversor.
class AtcInputView : public QWidget
{
    Q_OBJECT
public:
    explicit AtcInputView(core::autotile::Converter& conv, QWidget* parent = nullptr);
    QSize sizeHint() const override;
signals:
    void changed();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    core::autotile::Converter& conv;
};

class AutoTileConverterDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AutoTileConverterDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    void pickImage();
    void refreshAll();
    void refreshOutputPreview();
    void updateSourceFrame(bool clearSelectionIfGeometryChanges = true);
    QVector<QImage> convertedAnimationFrames(QString* error = nullptr) const;
    QImage packedAnimationOutput(QVector<QPoint>* relativeFrameOrigins = nullptr,
                                 QString* error = nullptr) const;
    QString registerAnimation(core::Tileset& ts, int baseCol, int baseRow,
                              const QVector<QPoint>& relativeOrigins, const QSize& frameTiles);
    void savePng();
    void importAutotiles();

    core::Editor&              ed;
    core::autotile::Converter  conv;
    QImage                     m_sourceImage;
    AtcInputView*              m_input = nullptr;
    QLabel*                    m_output = nullptr;
    QLabel*                    m_info = nullptr;
    QSpinBox*                  m_tileSize = nullptr;
    QComboBox*                 m_tileSizePreset = nullptr;
    QLineEdit*                 m_name = nullptr;
    QComboBox*                 m_category = nullptr;
    QCheckBox*                 m_animated = nullptr;
    QSpinBox*                  m_frameCount = nullptr;
    QComboBox*                 m_frameAxis = nullptr;
    QDoubleSpinBox*            m_animationFps = nullptr;
    QCheckBox*                 m_animationLoop = nullptr;
    QCheckBox*                 m_animationPingPong = nullptr;
    QComboBox*                 m_animationSync = nullptr;
    QTimer*                    m_animationPreviewTimer = nullptr;
    qint64                     m_animationPreviewMs = 0;
};

// ------------------------------------------------------------- Atalhos
class ShortcutsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};

// ------------------------------------------------- Comando "mostrar mensagem"
/// Preview da caixa de mensagem, desenhada do mesmo jeito que o jogo desenha
/// (mesma função de quebra de linha): dá para ver onde o texto quebra e em
/// quantas páginas vai virar SEM precisar rodar o jogo.
class MessagePreview : public QWidget
{
    Q_OBJECT
public:
    explicit MessagePreview(core::Editor& ed, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    void setText(const QString& raw);
    void setPositionId(const QString& id);
    void setSpeaker(const QString& speaker);
    void setPage(int i);
    void setOffset(int x, int y);
    void setBaseFontSize(int px);
    void setTextEffects(const core::TextEffectStack& effects, const core::TextGradientSpec& gradient);
    int  pageCount() const { return m_pageCount; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    core::Editor& ed;
    QString m_raw;
    QString m_speaker;
    QString m_pos = QStringLiteral("bottom");
    int     m_page = 0;
    int     m_pageCount = 1;
    int     m_offsetX = 0, m_offsetY = 0;
    int     m_baseFontSize = 0;
    core::TextEffectStack m_effects;
    core::TextGradientSpec m_gradient;
    QElapsedTimer* m_clock = nullptr;
};

/// Editor do comando `message`: texto, posição da caixa e botões de código.
class MessageCommandDialog : public QDialog
{
    Q_OBJECT
public:
    /// Edita `cmd` no lugar; só grava se o usuário confirmar.
    explicit MessageCommandDialog(core::Editor& ed, core::EventCommand& cmd,
                                  QWidget* parent = nullptr);
    /// Some com os botões OK/Cancelar: usado quando este formulário vira uma
    /// PÁGINA da janela única de texto, que tem os seus próprios botões.
    void esconderBotoes();
    QString texto() const;
    void    setTexto(const QString& t);

private:
    void refresh();

    core::Editor& ed;
    core::EventCommand& m_cmd;
    QPlainTextEdit* m_edit = nullptr;
    QLineEdit*      m_speaker = nullptr;
    QComboBox*      m_speakerProfile = nullptr;
    QLineEdit*      m_expression = nullptr;
    QLineEdit*      m_dialogueVoice = nullptr;
    QComboBox*      m_effectPreset = nullptr;
    QLineEdit*      m_localizationKey = nullptr;
    QLineEdit*      m_speakerLocalizationKey = nullptr;
    QComboBox*      m_pos = nullptr;
    MessagePreview* m_preview = nullptr;
    QLabel*         m_info = nullptr;
    QSpinBox*       m_pageSpin = nullptr;
    QSpinBox*       m_offsetX = nullptr;
    QSpinBox*       m_offsetY = nullptr;
    QSpinBox*       m_fontSize = nullptr;
    QComboBox*      m_overflow = nullptr;
    QDialogButtonBox* m_box = nullptr;
    TextEffectsEditorWidget* m_textEffects = nullptr;
};

// ------------------------------------------------------- Comando de legenda
/// Preview da legenda, desenhado com a MESMA função de quebra e os mesmos
/// efeitos do jogo — inclusive animados (onda, tremida, arco-íris…).
class SubtitlePreview : public QWidget
{
    Q_OBJECT
public:
    explicit SubtitlePreview(core::Editor& ed, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    void setRequest(const QString& text, const QString& speaker,
                    const QString& position, const QString& textAlign,
                    int offsetX = 0, int offsetY = 0);
    void setStyleOverride(const core::SubtitleStyle& style);
    void clearStyleOverride();
    void setTextEffects(const core::TextEffectStack& effects, const core::TextGradientSpec& gradient);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    core::Editor& ed;
    QString m_text, m_speaker, m_pos, m_align;
    int m_offsetX = 0, m_offsetY = 0;
    QElapsedTimer* m_clock = nullptr;
    core::SubtitleStyle m_styleOverride;
    bool m_hasStyleOverride = false;
    core::TextEffectStack m_effects;
    core::TextGradientSpec m_gradient;
};

/// Editor do comando `subtitle.show`.
class SubtitleCommandDialog : public QDialog
{
    Q_OBJECT
public:
    SubtitleCommandDialog(core::Editor& ed, core::EventCommand& cmd, QWidget* parent = nullptr);
    void esconderBotoes();
    QString texto() const;
    void    setTexto(const QString& t);

private:
    void refresh();

    core::Editor& ed;
    core::EventCommand& m_cmd;
    QPlainTextEdit* m_edit = nullptr;
    QLineEdit*  m_speaker = nullptr;
    QLineEdit*  m_localizationKey = nullptr;
    QLineEdit*  m_speakerLocalizationKey = nullptr;
    QComboBox*  m_pos = nullptr;
    QComboBox*  m_track = nullptr;
    QComboBox*  m_align = nullptr;
    QComboBox*  m_anchor = nullptr;
    QLineEdit*  m_anchorEvent = nullptr;
    QComboBox*  m_transIn = nullptr;
    QComboBox*  m_transOut = nullptr;
    QSpinBox*   m_duration = nullptr;
    QSpinBox*   m_offsetX = nullptr;
    QSpinBox*   m_offsetY = nullptr;
    QCheckBox*  m_waitInput = nullptr;
    QCheckBox*  m_waitEnd = nullptr;
    QCheckBox*  m_typewriter = nullptr;
    QLineEdit*  m_voice = nullptr;
    SubtitlePreview* m_preview = nullptr;
    QLabel*     m_info = nullptr;
    QDialogButtonBox* m_box = nullptr;
    TextEffectsEditorWidget* m_textEffects = nullptr;
};

/// Janela ÚNICA de texto: a mesma para caixa de mensagem e para legenda. Quem
/// escreve escolhe o modo; a janela grava o comando certo por baixo (`message`
/// ou `subtitle.show`), então projetos antigos continuam abrindo.
class TextCommandDialog : public QDialog
{
    Q_OBJECT
public:
    TextCommandDialog(core::Editor& ed, core::EventCommand& cmd, QWidget* parent = nullptr);

private:
    void trocarModo();
    void refresh();
    void gravar();

    core::Editor& ed;
    core::EventCommand& m_cmd;
    QComboBox*      m_modo = nullptr;
    QPlainTextEdit* m_edit = nullptr;
    QWidget*        m_paginaMensagem = nullptr;
    QWidget*        m_paginaLegenda = nullptr;
    // caixa de mensagem
    QComboBox*  m_pos = nullptr;
    MessagePreview* m_prevMsg = nullptr;
    QSpinBox*   m_pageSpin = nullptr;
    QLabel*     m_info = nullptr;
    // legenda
    QLineEdit*  m_speaker = nullptr;
    QComboBox  *m_subPos = nullptr, *m_align = nullptr, *m_anchor = nullptr,
               *m_transIn = nullptr, *m_transOut = nullptr;
    QLineEdit*  m_anchorEvent = nullptr;
    QSpinBox*   m_duration = nullptr;
    QCheckBox  *m_waitInput = nullptr, *m_waitEnd = nullptr, *m_typewriter = nullptr;
    QLineEdit*  m_voice = nullptr;
    SubtitlePreview* m_prevSub = nullptr;
};

/// Ajustes globais das legendas (Configurações do Jogo ▸ Jogabilidade).
class SubtitleStyleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SubtitleStyleDialog(core::Editor& ed, QWidget* parent = nullptr);
    SubtitleStyleDialog(core::Editor& ed, core::SubtitleStyle& target, QWidget* parent = nullptr);

private:
    void build();
    core::Editor& ed;
    core::SubtitleStyle* m_target = nullptr;
    bool m_markProjectDirty = true;
    // As cores PRECISAM ser membros: os botões e o "OK" mexem nelas depois que
    // o construtor já terminou. Enquanto eram variáveis locais, os lambdas
    // apontavam para pilha morta — e as cores viravam lixo (#00800000).
    QColor m_fontColor, m_nameColor, m_outlineColor, m_bgColor;
};

// ------------------------------------------------ Banco de dados do jogo
/// Interruptores e variáveis: nome e valor inicial. É a memória que o jogo
/// vai usar — aqui o autor só define como ela COMEÇA.
class GameDataDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GameDataDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    void recarregar();
    core::Editor& ed;
    QTableWidget* m_switches = nullptr;
    QTableWidget* m_vars = nullptr;
    QTableWidget* m_strings = nullptr;
};

/// Editor de um comando de lógica (interruptor, variável, condição, rótulo,
/// pulo, espera, chamar evento comum).
class LogicCommandDialog : public QDialog
{
    Q_OBJECT
public:
    /// `tipo` define qual formulário aparece; `cmd` é editado no lugar.
    LogicCommandDialog(core::Editor& ed, const QString& tipo, core::EventCommand& cmd,
                       QWidget* parent = nullptr, const core::CommonEvent* commonContext = nullptr,
                       bool allowConditionTree = true);

private:
    core::Editor& ed;
    core::EventCommand& m_cmd;
    const core::CommonEvent* m_commonContext = nullptr;
};

/// Condições de uma página de evento.
class PageConditionsDialog : public QDialog
{
    Q_OBJECT
public:
    PageConditionsDialog(core::Editor& ed, QVariantMap& conditions, QWidget* parent = nullptr);

private:
    core::Editor& ed;
    QVariantMap& m_cond;
};

/// Lista de eventos comuns do projeto (criar, renomear, editar comandos).
class CommonEventsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CommonEventsDialog(core::Editor& ed, QWidget* parent = nullptr,
                                const QString& initialCommonId = QString());

private:
    void recarregar();
    core::Editor& ed;
    QListWidget* m_lista = nullptr;
    QLineEdit* m_busca = nullptr;
    QComboBox* m_categoriaFiltro = nullptr;
};

/// Editor de uma lista de comandos — usado tanto pela página do evento quanto
/// pelos eventos comuns (é a MESMA lista, então é o mesmo widget).
class CommandListWidget : public QWidget
{
    Q_OBJECT
public:
    CommandListWidget(core::Editor& ed, QVector<core::EventCommand>& cmds,
                      QWidget* parent = nullptr);
    void recarregar();
    /// Texto legível de um comando (usado na lista e nos testes de interface).
    static QString descricao(const core::Editor& ed, const core::EventCommand& c);

private:
    void adicionar(const QString& tipo, int choiceCommand = -1, int choiceBranch = -1);
    int  selecionado() const;
    void atualizarPreviewLateral();
    int  fimDoBloco(int inicio) const;
    void selecionarIndice(int indice);
    const core::CommonEvent* commonContext() const;
    core::ProjectReferenceLocation commandLocation(int index) const;

    core::Editor& ed;
    QVector<core::EventCommand>& m_cmds;
    QListWidget* m_lista = nullptr;
    QPushButton* m_previewLateralButton = nullptr;
    QPushButton* m_playNarrativeButton = nullptr;
    NarrativePreviewWidget* m_previewLateral = nullptr;
    CommandPreviewDialog* m_previewLateralDialog = nullptr;
    QLabel* m_structureBreadcrumb = nullptr;
    CommandInspector* m_commandInspector = nullptr;
    QTabWidget* m_commandSideTabs = nullptr;
    QWidget* m_frameworkPreview = nullptr;
    CommandPreviewDialog* m_frameworkPreviewDialog = nullptr;
    QSet<QString> m_collapsedGroups;
};

// UI de Pictures em PictureDialogs.h.
// ------------------------------------------------- Resolução e ícones
/// Resolução da tela do jogo. Fica no projeto: o jogo tem que rodar igual em
/// qualquer máquina, e o palco de pré-visualização das imagens usa a mesma.
class GameResolutionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GameResolutionDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    core::Editor& ed;
};

/// Grade da folha de ícones com o NÚMERO de cada um escrito por cima — é o
/// que responde “qual número eu escrevo em \I[n]?” sem contar no dedo.
class IconSheetView : public QWidget
{
    Q_OBJECT
public:
    explicit IconSheetView(core::Editor& ed, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    int   selected() const { return m_sel; }
    void  refresh();

signals:
    void picked(int indice);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    core::Editor& ed;
    int m_sel = -1;
    int m_zoom = 2;
};

/// Importar a folha de ícones e escolher o tamanho da célula.
class IconSetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit IconSetDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    void importar();
    void removerFolha();
    void atualizar();

    core::Editor& ed;
    IconSheetView* m_grade = nullptr;
    QSpinBox *m_cellW = nullptr, *m_cellH = nullptr;
    QLabel*   m_info = nullptr;
    QListWidget* m_blocks = nullptr;
};

class FontManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FontManagerDialog(core::Editor& ed, QWidget* parent = nullptr);
private:
    void importar();
    void atualizar();
    core::Editor& ed;
    QListWidget* m_list = nullptr;
    QComboBox* m_main = nullptr;
};

// ------------------------------------------------------ Controles do jogo
/// Mapa de teclas: uma linha por ação, com captura de tecla.
class DatabaseDialog : public QDialog
{
    Q_OBJECT
public: explicit DatabaseDialog(core::Editor& ed,QWidget* parent=nullptr);
};

/// Banco de Dados Personalizado No-Code. É separado do banco RPG nativo e
/// trabalha exclusivamente com IDs estáveis, campos tipados e modos
/// Somente leitura / Runtime.
class CustomDatabaseDialog : public QDialog
{
    Q_OBJECT
public: explicit CustomDatabaseDialog(core::Editor& ed,QWidget* parent=nullptr);
};

/// Instala pacotes declarativos `.ludoplugin`. Eles só podem combinar
/// comandos nativos e nunca executam código, DLL ou JavaScript.
class NoCodePluginManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NoCodePluginManagerDialog(core::Editor& ed, QWidget* parent = nullptr);
};

class LudoHelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LudoHelpDialog(QWidget* parent=nullptr);
};

class CutsceneSkipDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CutsceneSkipDialog(core::Editor& ed,QWidget* parent=nullptr);
};

class ControlsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ControlsDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    void refresh();
    core::Editor& ed;
    core::InputMap m_map;
    core::InputSystemSettings m_system;
    QTableWidget* m_table = nullptr;
};

} // namespace ui
