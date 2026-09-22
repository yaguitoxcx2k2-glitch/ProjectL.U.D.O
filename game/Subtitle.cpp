#include "Subtitle.h"

#include "core/Model.h"
#include "game/TextEffectRuntime.h"

#include <QFontMetricsF>
#include <cmath>

using namespace core;

namespace game {

namespace {

/// Lê "\XX[conteudo]" a partir de `i` (que aponta para a barra). Devolve o
/// conteúdo e avança `i`; devolve string nula se não casar.
bool matchCode(const QString& s, int i, const char* code, QString* arg, int* next)
{
    const int n = int(qstrlen(code));
    if (i + 1 + n > s.size()) return false;
    if (s[i] != QLatin1Char('\\')) return false;
    for (int k = 0; k < n; ++k)
        if (s[i + 1 + k].toUpper() != QLatin1Char(code[k])) return false;
    int j = i + 1 + n;
    if (arg) {
        arg->clear();
        if (j < s.size() && s[j] == QLatin1Char('[')) {
            const int close = s.indexOf(QLatin1Char(']'), j);
            if (close < 0) return false;
            *arg = s.mid(j + 1, close - j - 1);
            j = close + 1;
        }
    }
    if (next) *next = j;
    return true;
}

} // namespace

SubtitleCodes extractSubtitleCodes(QString& text)
{
    SubtitleCodes out;
    QString limpo;
    limpo.reserve(text.size());
    for (int i = 0; i < text.size();) {
        QString arg;
        int next = i;
        if (matchCode(text, i, "TI", &arg, &next)) {
            out.transitionIn = subtitleTransitionFromId(arg);
            out.hasIn = true;
            i = next;
            continue;
        }
        if (matchCode(text, i, "TO", &arg, &next)) {
            out.transitionOut = subtitleTransitionFromId(arg);
            out.hasOut = true;
            i = next;
            continue;
        }
        if (matchCode(text, i, "TY", nullptr, &next)) {
            out.typewriterSound = true;
            i = next;
            continue;
        }
        limpo += text[i];
        ++i;
    }
    text = limpo;
    return out;
}

void transitionState(SubtitleTransition t, double progress, bool entering,
                     QPointF* offset, QPointF* scale)
{
    // `progress` 0 = fora da tela / começando, 1 = no lugar.
    const double p = qBound(0.0, progress, 1.0);
    const double resto = 1.0 - p;
    QPointF off(0, 0), esc(1, 1);
    const double dist = 40.0;
    switch (t) {
    case SubtitleTransition::None:                                   break;
    case SubtitleTransition::SlideUp:    off.setY( dist * resto);    break;
    case SubtitleTransition::SlideDown:  off.setY(-dist * resto);    break;
    case SubtitleTransition::SlideLeft:  off.setX( dist * resto);    break;
    case SubtitleTransition::SlideRight: off.setX(-dist * resto);    break;
    case SubtitleTransition::ZoomIn:     esc = QPointF(0.6 + 0.4 * p, 0.6 + 0.4 * p); break;
    case SubtitleTransition::ZoomOut:    esc = QPointF(1.4 - 0.4 * p, 1.4 - 0.4 * p); break;
    case SubtitleTransition::Bounce: {
        // Quica uma vez: passa do ponto e volta.
        const double b = std::sin(p * 3.14159265 * 1.5) * (1.0 - p);
        off.setY(-b * 18.0);
        break;
    }
    case SubtitleTransition::FlipX:      esc.setX(p);                break;
    case SubtitleTransition::FlipY:      esc.setY(p);                break;
    }
    if (!entering) {
        // Na saída o movimento é o espelho da entrada.
        off = -off;
    }
    if (offset) *offset = off;
    if (scale)  *scale = esc;
}

void SubtitleManager::layout(Subtitle& s, const QString& texto)
{
    QFont f = m_font;
    f.setPixelSize(qMax(6, s.fontSize));
    f.setBold(s.style.fontBold);
    f.setItalic(s.style.fontItalic);

    const double limite = qMin(double(s.style.maxWidth),
                               m_screenW - 2.0 * s.style.marginHorizontal
                                   - 2.0 * s.style.paddingH);
    // Uma legenda é UMA página: se não couber, ela cresce em linhas (é assim
    // no plugin — quem quer paginar usa várias legendas em sequência).
    s.pages = layoutMessage(texto, f, qMax(40.0, limite), 99, m_varResolver);

    double w = 0.0, h = 0.0;
    for (const TextPage& p : s.pages) {
        for (const TextLine& l : p.lines) {
            double lw = 0.0;
            for (const TypedChar& c : l.chars)
                if (c.isDrawable()) lw += charAdvance(f, c, nullptr);
            w = qMax(w, lw);
            h += lineHeight(l, f);
        }
    }
    s.textSize = QSizeF(w, qMax(1.0, h));
}

QString SubtitleManager::show(const SubtitleRequest& req)
{ return showNow(req); }

QString SubtitleManager::enqueue(const SubtitleRequest& req)
{
    SubtitleRequest queued=req;const QString id=idGen();queued.text=queued.text;const int key=int(queued.track);m_queues[key].enqueue(queued);startQueuedTracks();return id;
}

QString SubtitleManager::showNow(const SubtitleRequest& req)
{
    Subtitle s;
    s.id = idGen();
    s.track=req.track;
    s.style=m_style.resolvedForTrack(req.track);
    const core::SubtitleTrackSettings trackSettings=m_style.trackSettings(req.track);
    s.speakerName = req.speakerName;

    // 1) códigos que valem para a legenda inteira saem do texto
    QString texto = req.text;
    const SubtitleCodes cod = extractSubtitleCodes(texto);

    // 2) estilo: o que o comando não disser vem do projeto
    s.fontSize  = req.fontSize > 0 ? req.fontSize : s.style.fontSize;
    s.fontColor = req.fontColor.isValid() ? req.fontColor : s.style.fontColor;
    s.nameColor = req.nameColor.isValid() ? req.nameColor : s.style.nameColor;
    s.gradient = req.gradient;
    s.effects = req.effects;
    s.position  = req.hasPosition  ? req.position  : s.style.position;
    s.textAlign = req.hasTextAlign ? req.textAlign : s.style.textAlign;
    s.boxAlign  = req.hasBoxAlign  ? req.boxAlign  : s.style.boxAlign;
    s.transitionIn  = cod.hasIn  ? cod.transitionIn
                                 : (req.hasTransitionIn ? req.transitionIn : s.style.transitionIn);
    s.transitionOut = cod.hasOut ? cod.transitionOut
                                 : (req.hasTransitionOut ? req.transitionOut : s.style.transitionOut);
    s.anchor = req.anchor;
    s.anchorEventId = req.anchorEventId;
    s.anchorOffsetY = req.anchorOffsetY;
    s.offsetX = req.offsetX + s.style.offsetX;
    s.offsetY = req.offsetY + s.style.offsetY;
    s.fadeInSec  = qMax(0.0, s.style.fadeInFrames  / 60.0);
    s.fadeOutSec = qMax(0.0, s.style.fadeOutFrames / 60.0);

    s.waitForInput = req.hasWaitForInput ? req.waitForInput : s.style.waitForInput;

    s.typewriter = req.hasTypewriter ? req.typewriter : s.style.typewriter;
    s.typewriterSpeed = req.typewriterSpeed > 0 ? req.typewriterSpeed : s.style.typewriterSpeed;
    const double presetCps = textEffectTypewriterCharsPerSecond(s.effects.entrance);
    if (presetCps > 0.0) { s.typewriter = true; s.typewriterSpeed = presetCps; }
    s.voiceFile = req.voiceFile;
    s.voiceVolume = req.voiceVolume;

    layout(s, texto);
    int dur=req.durationFrames;
    if(dur<0)dur=s.style.defaultDuration;
    if(dur==0)dur=qMax(trackSettings.minDurationFrames,int(std::ceil(qMax(1,s.totalChars())/qMax(1.0,trackSettings.charsPerSecond)*60.0)));
    s.remainingSec=s.waitForInput?1e9:qMax(0.05,dur/60.0);
    s.revealed = s.typewriter ? 0 : s.totalChars();
    s.revealTimesSec = QVector<double>(s.totalChars(), s.typewriter ? -1.0 : 0.0);
    s.transitionProgress = (s.transitionIn == SubtitleTransition::None) ? 1.0 : 0.0;

    // 3) uma linha que espera o jogador nunca empilha com outra: a anterior é
    //    encerrada na hora (mesma regra do plugin, que existe para o diálogo
    //    não virar pilha de textos sobrepostos).
    if (s.waitForInput) clearTrack(s.track);
    auto trackCount=[this,&s]{int count=0;for(const Subtitle& active:m_subs)if(active.track==s.track)++count;return count;};
    while(trackCount()>=trackSettings.maxSimultaneous){for(int i=0;i<m_subs.size();++i)if(m_subs[i].track==s.track){m_subs.remove(i);break;}}

    // Voz é encaminhada à fila unificada da sessão; não interrompe outra fala.
    if (!s.voiceFile.isEmpty()) {
        if (m_voice) m_voice(s.voiceFile, s.voiceVolume);
    }
    if (!req.soundEffect.isEmpty() && m_se) m_se(req.soundEffect, req.soundVolume);

    m_subs.push_back(s);
    return s.id;
}

void SubtitleManager::clearAll()
{
    if (!m_subs.isEmpty() && m_stopVoice) m_stopVoice();
    m_subs.clear();
    m_queues.clear();
}

void SubtitleManager::clearQueue(core::SubtitleTrack track){m_queues.remove(int(track));}
void SubtitleManager::clearTrack(core::SubtitleTrack track,bool withFade)
{
    clearQueue(track);for(int i=m_subs.size()-1;i>=0;--i)if(m_subs[i].track==track){if(withFade){m_subs[i].phase=Subtitle::FadeOut;m_subs[i].exitAgeSec=0.0;}else m_subs.remove(i);}
}

bool SubtitleManager::busy() const{if(!m_subs.isEmpty())return true;for(auto it=m_queues.cbegin();it!=m_queues.cend();++it)if(!it.value().isEmpty())return true;return false;}
bool SubtitleManager::busy(core::SubtitleTrack track) const{for(const Subtitle&s:m_subs)if(s.track==track)return true;return !m_queues.value(int(track)).isEmpty();}
bool SubtitleManager::freezesMap() const{for(const Subtitle&s:m_subs)if(s.style.freezeMap)return true;return false;}

void SubtitleManager::startQueuedTracks()
{
    const QList<int> keys=m_queues.keys();for(int key:keys){bool active=false;for(const Subtitle&s:m_subs)if(int(s.track)==key){active=true;break;}if(!active&&!m_queues[key].isEmpty())showNow(m_queues[key].dequeue());if(m_queues[key].isEmpty())m_queues.remove(key);}
}

void SubtitleManager::clearWithFade()
{
    for (Subtitle& s : m_subs) {
        if (s.phase == Subtitle::Done) continue;
        s.phase = Subtitle::FadeOut;
        s.exitAgeSec = 0.0;
        s.waitingForInput = false;
        s.waitForInput = false;
    }
}

void SubtitleManager::update(double dt)
{
    if (dt <= 0){startQueuedTracks();return;}
    for (int i = m_subs.size() - 1; i >= 0; --i) {
        Subtitle& s = m_subs[i];
        s.ageSec += dt;

        // ---- digitação ---------------------------------------------------
        if (s.typewriter && !s.finishedTyping()) {
            s.charAcc += dt * qMax(1.0, s.typewriterSpeed);
            while (s.charAcc >= 1.0 && !s.finishedTyping()) {
                s.charAcc -= 1.0;
                if (s.revealed < s.revealTimesSec.size() && s.revealTimesSec.at(s.revealed) < 0.0)
                    s.revealTimesSec[s.revealed] = s.ageSec;
                ++s.revealed;
            }
        }
        if (s.waitForInput && s.finishedTyping()) s.waitingForInput = true;

        // ---- fases -------------------------------------------------------
        // Encadeadas de propósito (em vez de um switch): com fade zerado, a
        // legenda tem de entrar e sair NO MESMO quadro. Com switch, cada troca
        // de fase custava um quadro extra e "duração 0" nunca era 0 de verdade.
        if (s.phase == Subtitle::FadeIn) {
            s.opacity += (s.fadeInSec <= 0.0) ? 1.0 : dt / s.fadeInSec;
            if (s.transitionIn != SubtitleTransition::None)
                s.transitionProgress = qBound(0.0, s.opacity, 1.0);
            if (s.opacity >= 1.0) { s.opacity = 1.0; s.transitionProgress = 1.0; s.phase = Subtitle::Show; }
        }
        if (s.phase == Subtitle::Show && !s.waitingForInput) {
            s.remainingSec -= dt;
            if (s.remainingSec <= s.fadeOutSec) {
                s.phase = Subtitle::FadeOut;
                s.exitAgeSec = 0.0;
            }
        }
        if (s.phase == Subtitle::FadeOut) {
            s.exitAgeSec += dt;
            const TextPage emptyPage;
            const TextPage& page = s.pages.isEmpty() ? emptyPage : s.pages.front();
            const double effectSpan = textEffectPhaseSpanSec(s.effects.exit,
                                                             page.drawableCount(),
                                                             textPageWordCount(page));
            const double outSpan = qMax(s.fadeOutSec, effectSpan);

            // Fade visual clássico e preset Exit pertencem à mesma fase de
            // saída. Quando o preset tem stagger maior que o fade legado, o
            // fade acompanha o span total para que os últimos glyphs não
            // desapareçam antes da animação que o usuário configurou. Sem
            // preset, outSpan == fadeOutSec e o comportamento antigo permanece.
            if (outSpan > 0.0)
                s.opacity = qBound(0.0, 1.0 - s.exitAgeSec / outSpan, 1.0);
            else
                s.opacity = 0.0;

            if (s.transitionOut != SubtitleTransition::None) {
                const double transitionSpan = qMax(0.001, outSpan);
                s.transitionProgress = qBound(0.0, 1.0 - s.exitAgeSec / transitionSpan, 1.0);
            }
            if (s.exitAgeSec + 1e-9 >= outSpan) {
                s.opacity = 0.0;
                s.phase = Subtitle::Done;
            }
        }
        if (s.phase == Subtitle::Done) {
            m_subs.remove(i);
        }
    }
    startQueuedTracks();
}

bool SubtitleManager::confirm()
{
    // Da mais nova para a mais antiga: a última linha é a que o jogador está
    // lendo.
    for (int i = m_subs.size() - 1; i >= 0; --i) {
        Subtitle& s = m_subs[i];
        if (s.phase == Subtitle::FadeOut || s.phase == Subtitle::Done) continue;
        if (s.typewriter && !s.finishedTyping()) {
            for (int glyph=s.revealed; glyph<s.revealTimesSec.size(); ++glyph)
                if (s.revealTimesSec.at(glyph) < 0.0) s.revealTimesSec[glyph] = s.ageSec;
            s.revealed = s.totalChars();     // primeiro toque: completa o texto
            if (s.waitForInput) s.waitingForInput = true;
            return true;
        }
        if (s.waitingForInput) {
            s.waitingForInput = false;
            s.waitForInput = false;
            s.remainingSec = 0.0;
            s.phase = Subtitle::FadeOut;
            s.exitAgeSec = 0.0;
            return true;
        }
    }
    return false;
}

} // namespace game
