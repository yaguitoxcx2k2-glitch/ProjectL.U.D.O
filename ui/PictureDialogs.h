// ============================================================================
//  PictureDialogs.h — UI do sistema de Pictures.
//
//  Separado de Dialogs.h para reduzir acoplamento e tempo de recompilação:
//  mudanças em Pictures não obrigam os demais diálogos do editor a recompilar.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/EventModel.h"
#include "core/Picture.h"
#include "game/PictureFx.h"
#include "game/Pictures.h"

#include <QColor>
#include <QDialog>
#include <QSizeF>
#include <QTransform>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
QT_END_NAMESPACE

namespace ui {

/// Editor No-Code dos comandos de nomes, grupos e vínculos do Picture World.
bool editPictureWorldCommand(core::Editor& editor, core::EventCommand& command,
                             QWidget* parent = nullptr);

class PreviewClock;
class VisualEffectsPanel;
class TextEffectsEditorWidget;

// -------------------------------------------------- Imagens de tela
/// Biblioteca de imagens do projeto (as "pictures"). Importar, renomear,
/// remover — as imagens ficam EMBUTIDAS no .json, como o charset.
class PictureLibraryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PictureLibraryDialog(core::Editor& ed, QWidget* parent = nullptr,
                                  bool selectionMode = false,
                                  const QString& initialPictureId = QString());

    /// Abre a mesma Biblioteca de Pictures em modo de seleção e devolve o ID
    /// da Picture escolhida. String vazia = cancelado/nenhuma alteração.
    static QString choosePicture(core::Editor& ed, QWidget* parent = nullptr,
                                 const QString& currentPictureId = QString());
    QString selectedPictureId() const;

private:
    void recarregar();
    void importar();
    void remover();
    void renomear();
    void atualizarPreview();

    core::Editor& ed;
    QListWidget* m_lista = nullptr;
    QLabel*      m_preview = nullptr;
    QLabel*      m_info = nullptr;
    QPushButton* m_useSelected = nullptr;
    bool m_selectionMode = false;
    QString m_initialPictureId;
};

/// Tela de pré-visualização do comando "mostrar imagem": desenha o mapa como
/// o jogo desenharia e deixa ARRASTAR a imagem para posicioná-la, com alça de
/// escala (canto) e de rotação (topo). Acertar 384,207 no olho é impossível;
/// arrastando leva dois segundos.
class PictureStageView : public QWidget
{
    Q_OBJECT
public:
    PictureStageView(core::Editor& ed, core::PictureDef& def, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(560, 420); }
    /// Liga/desliga a animação da física (flutuar, balançar, girar, pulsar).
    void setAnimated(bool on);
    /// Toca as transições em laço: entra, segura, sai, repete. É a única
    /// forma de ver uma transição sem abrir o jogo.
    void playTransitions(bool on);
    /// Reproduz apenas a transição de entrada ou apenas a de saída uma vez.
    void previewTransition(bool entering);
    /// Exibe outras pictures do projeto como uma mesa de luz, sem alterar o
    /// comando atual. Elas também viram alvos do alinhamento magnético.
    void setReferencePictures(const QVector<core::PictureDef>& pictures);
    void setSnapEnabled(bool on);
    /// O preview de "Mover" é somente ensaio: esconde alças e não aceita
    /// arrasto, pois quem define o destino são os campos do comando.
    void setInteractive(bool on);
    /// Desenha o charset do projeto no centro da tela como referência de
    /// escala/profundidade para Pictures animadas.
    void setShowPlayerReference(bool on);

signals:
    void defChanged();          ///< o usuário arrastou algo

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    enum class Grab { None, Move, Scale, Rotate };

    /// Fator de tela: a área do jogo (800x600) cabendo no widget.
    double fator() const;
    QPointF paraTela(const QPointF& jogo) const;
    QPointF paraJogo(const QPointF& tela) const;
    QTransform transformDaImagem() const;
    QTransform transformDaImagem(const core::PictureDef& def, const QSizeF& base) const;
    QTransform transformDaImagem(const game::LivePicture& picture,
                                 const game::PictureFrame& frame) const;
    QTransform transformParaPalco(const QTransform& logicalScreen) const;
    QPointF    ancoraEmTela() const;
    const core::PictureAsset* asset() const;

    core::Editor& ed;
    core::PictureDef& m_def;
    Grab   m_grab = Grab::None;
    /// Mesma composição de efeitos que o jogo usa — o palco mostra a imagem
    /// exatamente como ela vai aparecer, com borda, brilho e onda.
    game::VisualFxCache m_fx;
    /// Tamanho da imagem BASE do quadro atual. Numa picture de texto não
    /// existe asset na biblioteca — o tamanho vem do texto desenhado, e as
    /// alças precisam saber disso (senão elas moldam a imagem errada).
    mutable QSizeF m_tamBase;
    QPointF m_grabIni;             ///< ponto do clique, em coordenadas do jogo
    double m_iniX = 0, m_iniY = 0, m_iniSX = 100, m_iniSY = 100, m_iniAng = 0;
    PreviewClock* m_clock = nullptr;
    bool m_playTrans = false;
    int m_singleTransition = 0;   ///< 0=nenhuma, 1=IN, 2=OUT
    bool m_snapEnabled = true;
    bool m_interactive = true;
    bool m_showPlayerReference = false;
    bool m_guideX = false, m_guideY = false;
    double m_guideXValue = 0.0, m_guideYValue = 0.0;
    QVector<core::PictureDef> m_references;
    game::PictureManager m_ensaio;   ///< picture de mentira, só para o ensaio
};

/// Formulário do texto rico de uma picture (ShowRichTextPicture).
class PictureTextPanel : public QWidget
{
    Q_OBJECT
public:
    PictureTextPanel(core::Editor& ed, core::PictureRichText& rt, QWidget* parent = nullptr);
    void puxar();

signals:
    void changed();

private:
    core::Editor& ed;
    core::PictureRichText& m_rt;
    QPlainTextEdit* m_texto = nullptr;
    QComboBox *m_fonte = nullptr, *m_align = nullptr, *m_bg = nullptr, *m_bgBorda = nullptr;
    QSpinBox  *m_tam = nullptr, *m_contorno = nullptr, *m_padding = nullptr,
              *m_larg = nullptr, *m_alt = nullptr, *m_raio = nullptr,
              *m_somX = nullptr, *m_somY = nullptr, *m_bgSlice = nullptr;
    QDoubleSpinBox *m_bgAlfa = nullptr, *m_bgEscala = nullptr;
    QCheckBox *m_negrito = nullptr, *m_italico = nullptr, *m_auto = nullptr,
              *m_quebra = nullptr, *m_sombra = nullptr, *m_usarGrad = nullptr;
    QPushButton *m_cor = nullptr, *m_cor2 = nullptr, *m_corContorno = nullptr,
                *m_corSombra = nullptr, *m_corBg = nullptr, *m_corBg2 = nullptr;
    QColor m_c1, m_c2, m_cCont, m_cSombra, m_cBg, m_cBg2;
    TextEffectsEditorWidget* m_textEffects = nullptr;
};

/// Editor dos comandos `picture.*`.
class PictureCommandDialog : public QDialog
{
    Q_OBJECT
public:
    /// `tipo` decide o formulário: picture.show, picture.move, picture.zoomIn,
    /// picture.zoomOut, picture.tween, picture.physics, picture.erase, picture.wait.
    PictureCommandDialog(core::Editor& ed, const QString& tipo, core::EventCommand& cmd,
                         QWidget* parent = nullptr);

private:
    void montarShow();
    void montarMove();
    void montarZoom(bool zoomIn);
    void montarSimples();
    void aplicarShow();

    core::Editor& ed;
    QString m_tipo;
    core::EventCommand& m_cmd;
    core::PictureDef m_def;
    PictureStageView* m_stage = nullptr;
    class PictureTextPanel* m_textPanel = nullptr;
    QComboBox* m_asset = nullptr;
    QLineEdit *m_logicalName = nullptr, *m_pictureGroup = nullptr,
              *m_dynamicX = nullptr, *m_dynamicY = nullptr, *m_dynamicOpacity = nullptr;
    QSpinBox*  m_number = nullptr;
    QDoubleSpinBox *m_x = nullptr, *m_y = nullptr, *m_sx = nullptr, *m_sy = nullptr,
                         *m_op = nullptr, *m_ang = nullptr;
    QComboBox *m_anchor = nullptr, *m_blend = nullptr, *m_space = nullptr, *m_layer = nullptr,
              *m_ease = nullptr, *m_positionPreset = nullptr, *m_nineEdgeMode = nullptr,
              *m_nineCenterMode = nullptr;
    QCheckBox *m_smooth = nullptr, *m_wait = nullptr, *m_negativeEnabled = nullptr,
              *m_flipH = nullptr, *m_flipV = nullptr,
              *m_duringBattle = nullptr, *m_eraseOnMapChange = nullptr, *m_affectedByTone = nullptr,
              *m_spritesheet = nullptr, *m_frameLoop = nullptr, *m_framePlaying = nullptr,
              *m_nineSlice = nullptr;
    QSpinBox *m_frameCount = nullptr, *m_frameColumns = nullptr, *m_frameRows = nullptr,
             *m_frameIndex = nullptr, *m_nineWidth = nullptr, *m_nineHeight = nullptr,
             *m_nineLeft = nullptr, *m_nineTop = nullptr, *m_nineRight = nullptr, *m_nineBottom = nullptr;
    QDoubleSpinBox *m_frameFps = nullptr, *m_negativeStrength = nullptr;
    QLabel* m_frameValidation = nullptr;
    /// Página de efeitos (borda, brilho, piscar, onda, brilho deslizante,
    /// máscara e transições). Fica num widget próprio para o mesmo formulário
    /// servir ao comando "Mostrar imagem" e ao comando "Efeitos da imagem".
    VisualEffectsPanel* m_fxPanel = nullptr;
    QSpinBox  *m_dur = nullptr, *m_negativeDuration = nullptr;
    QDoubleSpinBox *m_floatSpeed = nullptr, *m_floatRange = nullptr,
                         *m_swaySpeed = nullptr, *m_swayRange = nullptr,
                         *m_spinSpeed = nullptr, *m_pulseSpeed = nullptr, *m_pulseRange = nullptr;
};


} // namespace ui
