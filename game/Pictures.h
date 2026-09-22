// ============================================================================
//  Pictures.h — As pictures VIVAS durante a partida.
//
//  O `core::PictureDef` é o que o autor montou no editor; aqui está o que está
//  acontecendo agora na tela: quanto já andou o tween, há quanto tempo a
//  imagem existe (é o relógio da física), quais slots estão ocupados.
//
//  Sem interface e sem QPainter de propósito: dá para rodar uma cutscene
//  inteira de imagens num teste e conferir os números quadro a quadro, sem
//  abrir janela. Quem desenha é o `GameSession` — e, como ele desenha na
//  camada de interface, o caminho de GPU herda o resultado de graça.
// ============================================================================
#pragma once

#include "core/Picture.h"

#include <QMap>
#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <utility>

namespace game {

// Estado runtime próprio da "Imagem em Sequência". A configuração continua
// em PictureDef/FrameSequence para preservar o formato do projeto, mas o frame
// atual e o acumulador não dependem mais de LivePicture::age. Isso evita que
// mover, tingir, espelhar ou trocar configurações visuais reinicie a animação.
struct PictureFramePlayback {
    bool initialized = false;
    bool enabled = false;
    int startFrame = 0;
    int currentFrame = 0;
    int requestedCount = 1; ///< quantidade pedida no projeto antes da normalizacao segura
    int count = 1;          ///< quantidade efetiva (nunca maior que colunas x linhas)
    int columns = 1;
    int rows = 1;
    double fps = 12.0;
    double accumulated = 0.0;
    bool loop = true;
    bool playing = false;
    bool paused = false;
    int fixedFrame = -1;    ///< -1 = sem quadro forcado; >=0 = quadro fixo explicito

    void reset(const core::FrameSequence& sequence);
    void restoreLegacy(const core::FrameSequence& sequence, double ageSeconds);
    void update(double dt);
    void pause();
    void resume();
    void setFixedFrame(int frame);
    QString modeId() const;

    int capacity() const;
    bool usesFrames() const { return enabled || count > 1; }
    bool dynamic() const;
    bool validForImage(const QSize& imageSize) const;
    core::FrameSequenceValidation validationForImage(const QSize& imageSize) const;
    QRect rectAt(const QSize& imageSize) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& object, const core::FrameSequence& fallback);
};

/// Uma animação em andamento numa propriedade.
struct PictureTween {
    core::PictureProp prop = core::PictureProp::X;
    double from = 0.0, to = 0.0;
    double duration = 0.0;      ///< segundos
    double elapsed = 0.0;
    core::PictureEase ease = core::PictureEase::Linear;

    bool   finished() const { return elapsed >= duration; }
    /// Valor da propriedade neste instante.
    double value() const
    {
        if (duration <= 0.0) return to;
        const double t = core::applyEase(ease, elapsed / duration);
        return from + (to - from) * t;
    }
};

struct PictureKeyframe {
    double time = 0.0;
    double positionX = 0.0, positionY = 0.0;
    double opacity = 255.0, scaleX = 100.0, scaleY = 100.0, rotation = 0.0;
    QColor tint;
    QSet<QString> properties;
    QVariantMap toVariantMap() const;
    static PictureKeyframe fromVariantMap(const QVariantMap& value);
};

struct PictureTimeline {
    QString name;
    double duration = 0.0;
    bool loop = false;
    QVector<PictureKeyframe> keyframes;
    QVariantMap toVariantMap() const;
    static PictureTimeline fromVariantMap(const QVariantMap& value);
    bool valid() const { return !name.trimmed().isEmpty() && duration > 0.0 && !keyframes.isEmpty(); }
};

struct PictureTimelinePlayback {
    QString name;
    double elapsed = 0.0;
    bool playing = false;
};

/// Relação espacial persistente de uma Picture com outra entidade do mundo.
/// `target` aceita player/event:<id>; `parentId` referencia outro slot.
struct PictureAttachment {
    int parentId = 0;
    QString target;
    QString followAxis = QStringLiteral("both"); // both/x/y
    QPointF offset;
    bool inheritRotation = false;
    bool inheritScale = false;
    bool inheritOpacity = false;

    bool active() const { return parentId > 0 || !target.trimmed().isEmpty(); }
    QVariantMap toVariantMap() const;
    static PictureAttachment fromVariantMap(const QVariantMap& value);
};

struct PictureTargetState {
    bool valid = false;
    double x = 0.0;
    double y = 0.0;
    double angle = 0.0;
    double scaleX = 100.0;
    double scaleY = 100.0;
    double opacity = 255.0;
};

/// Uma picture ocupando um slot. Herda o estado visual e acrescenta o que só
/// existe enquanto o jogo roda.
struct LivePicture {
    /// Em que ponto da vida a imagem está. Entrando e saindo são fases com
    /// hora para acabar; "sumindo" apaga o slot no fim.
    enum Phase { In, Normal, Out };

    core::PictureDef def;
    QString logicalName;
    QString pictureGroup;
    PictureAttachment attachment;
    QMap<core::PictureProp, QVariantMap> dynamicBindings;

    // Transform calculado pelo Picture World a cada quadro. Não altera `def`,
    // portanto detach restaura imediatamente a posição própria da Picture.
    PictureTargetState attachedTransform;
    double age = 0.0;                 ///< segundos de vida (alimenta física/efeitos)
    PictureFramePlayback framePlayback; ///< relógio independente da imagem em sequência
    QVector<PictureTween> tweens;
    PictureTimelinePlayback timeline;
    QString onTouchCommonEventId;
    QString onClickCommonEventId;

    Phase  phase = Normal;
    core::PictureTransition phaseType = core::PictureTransition::None;
    double phaseTime = 0.0;
    double phaseDuration = 0.0;

    // Transição independente do Negative. Fica fora de PictureTween porque
    // intensidade de FX não é uma propriedade geométrica da Picture.
    double negativeFrom = 0.0;
    double negativeTo = 0.0;
    double negativeElapsed = 0.0;
    double negativeDuration = 0.0;
    double effectiveNegativeStrength() const;

    /// Tem alguma coisa acontecendo? (tween OU transição)
    bool animating() const { return !tweens.isEmpty() || timeline.playing || phase != Normal || negativeDuration > 0.0; }
    bool inTransition() const { return phase != Normal; }

    // ---- valores efetivos (base + física) --------------------------------
    // A física NÃO altera `def`: ela é somada na hora de desenhar. Assim um
    // tween de X continua indo para onde foi mandado enquanto a imagem balança.
    double effectiveX() const;
    double effectiveY() const;
    double effectiveScaleX() const;
    double effectiveScaleY() const;
    double effectiveAngle() const;
    double effectiveOpacity() const;

    /// Frame efetivo. Previews legados que constroem LivePicture diretamente
    /// continuam funcionando: quando o playback não foi inicializado, a regra
    /// antiga baseada em age é convertida temporariamente para o novo estado.
    int effectiveFrameIndex() const;
    QRect frameRect(const QSize& imageSize) const;
    bool frameLayoutValid(const QSize& imageSize) const;
    bool frameConfigurationValid(const QSize& imageSize) const;
    core::FrameSequenceValidation frameValidation(const QSize& imageSize) const;
    bool frameDynamic() const;
};

/// Todos os slots de picture da partida.
class PictureManager
{
public:
    using DynamicValueResolver = std::function<QVariant(const QVariantMap&)>;
    using TargetResolver = std::function<PictureTargetState(const QString&)>;

    void setDynamicValueResolver(DynamicValueResolver resolver) { m_dynamicValueResolver = std::move(resolver); }
    void setTargetResolver(TargetResolver resolver) { m_targetResolver = std::move(resolver); }

    /// Mostra (ou substitui) a picture do slot `def.number`.
    /// Mostrar de novo no mesmo slot zera tweens e relógio — é o que o RPG
    /// Maker faz, e é o que evita uma imagem nova herdar a animação da velha.
    void show(const core::PictureDef& def);
    int showByName(const QString& logicalName, const core::PictureDef& def);
    int findByName(const QString& logicalName) const;
    void setLogicalName(int number, const QString& logicalName);
    void setGroup(int number, const QString& group);
    void eraseGroup(const QString& group);
    void moveGroup(const QString& group, const QMap<core::PictureProp, double>& targets,
                   double duration, core::PictureEase ease);
    QVector<int> groupMembers(const QString& group) const;
    void bindDynamicValue(int number, core::PictureProp prop, const QVariantMap& sourceSpec);
    void clearDynamicValue(int number, core::PictureProp prop);
    void attach(int number, const PictureAttachment& attachment);
    void detach(int number);
    bool defineTimeline(const PictureTimeline& timeline);
    bool playTimeline(int number, const QString& timelineName);
    void stopTimeline(int number);
    const PictureTimeline* timeline(const QString& name) const;
    void setOnTouch(int number, const QString& commonEventId);
    void setOnClick(int number, const QString& commonEventId);
    QString interactionCommonEvent(int number, bool click) const;

    /// Move/anima várias propriedades de uma vez. `duration` em segundos;
    /// 0 = imediato. Só as propriedades presentes no mapa são tocadas.
    void moveTo(int number, const QMap<core::PictureProp, double>& alvos,
                double duration, core::PictureEase ease);
    /// Anima UMA propriedade (equivale ao TweenPicture do plugin).
    void tween(int number, core::PictureProp prop, double target,
               double duration, core::PictureEase ease);
    /// Liga/desliga a física de um slot já visível.
    /// API estruturada do Pictures 2.0. Float/Breathing/Sway/Spin passam pelo
    /// mesmo estado e pelo mesmo avaliador temporal.
    void setMotionEffects(int number, const core::PicturePhysicsState& state);
    /// Compatibilidade com comandos/projetos anteriores.
    void setPhysics(int number, double floatSpeed, double floatRange,
                    double swaySpeed, double swayRange,
                    double spinSpeed, double pulseSpeed, double pulseRange);
    /// Troca a âncora sem mexer no resto.
    void setAnchor(int number, core::PictureAnchor a, double cx = 0.0, double cy = 0.0);
    /// Troca os efeitos de um slot já visível.
    void setEffects(int number, const core::PictureEffects& fx);
    /// Espelha uma imagem já visível.
    void setFlip(int number, bool flipH, bool flipV);
    /// Controla o pequeno estado runtime da Imagem em Sequência sem recriar a Picture.
    void pauseSequence(int number);
    void resumeSequence(int number);
    void setSequenceFixedFrame(int number, int frame);
    /// Atualiza as configurações de exibição sem recriar a picture.
    void setDisplaySettings(int number, core::PictureSpace space, core::PictureLayer layer,
                            bool duringBattle, bool eraseOnMapChange, bool affectedByTone);
    /// Desliga todos os efeitos (equivale ao ClearAllEffects do plugin).
    void clearEffects(int number);
    /// Começa a transição de saída. Quando ela termina, o slot é APAGADO
    /// sozinho — é o que o autor espera de "sumir com a imagem".
    /// `frames` <= 0 usa o que estiver nos efeitos da picture.
    void startTransitionOut(int number, core::PictureTransition tipo, int frames = -1);

    void erase(int number);
    void eraseOnMapChange();
    void clearAll();
    /// Finaliza tweens/transições imediatamente ao pular uma cutscene: mantém
    /// o estado final em vez de deixar animações órfãs rodando depois do skip.
    void finishAnimations();

    void update(double dt);
    /// Reavalia valores e alvos depois que o mundo/eventos avançam no mesmo
    /// tick, evitando um quadro de atraso entre ator e Picture anexada.
    void refreshBindings();

    bool contains(int number) const { return m_pics.contains(number); }
    int  count() const { return int(m_pics.size()); }
    /// Alguma animação rodando? (é o que o comando "esperar terminar" observa)
    bool busy(int number) const;
    bool anyBusy() const;

    LivePicture*       at(int number);
    const LivePicture* at(int number) const;

    /// Slots em ordem de desenho: número menor primeiro (fica atrás).
    QVector<const LivePicture*> ordered() const;

    /// Snapshot versionado pelo GameSave. Inclui tweens e transições em curso,
    /// não apenas a imagem final, para carregar sem saltos visuais.
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& object);

private:
    LivePicture* ensure(int number);
    /// Aplica o valor final de um tween na definição e o descarta.
    static void  applyProp(core::PictureDef& d, core::PictureProp p, double v);
    static double readProp(const core::PictureDef& d, core::PictureProp p);
    void applyTimeline(LivePicture& picture, double dt);
    void removeIndexesFor(const LivePicture& picture);
    bool resolveAttachment(int number, QSet<int>& visiting);

    QMap<int, LivePicture> m_pics;    ///< QMap: já vem ordenado pelo número
    QHash<QString, int> m_logicalNameMap;
    QHash<QString, PictureTimeline> m_timelines;
    DynamicValueResolver m_dynamicValueResolver;
    TargetResolver m_targetResolver;
};

} // namespace game
