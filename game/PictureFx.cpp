// ============================================================================
//  PictureFx.cpp — Composição dos efeitos das imagens de tela.
// ============================================================================
#include "PictureFx.h"

#include "TextPicture.h"
#include "core/Renderer.h"

#include <QPainter>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtMath>
#include <cmath>

using namespace core;

namespace game {

// ============================================================================
//  Desfoque
// ============================================================================
void borrarCaixa(QImage& img, int raio)
{
    if (raio < 1 || img.isNull()) return;
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = img.width(), h = img.height();
    if (w < 2 || h < 2) return;

    // Três passadas de caixa aproximam um gaussiano bem o bastante para um
    // brilho — e custam O(n) por passada, sem multiplicação por pixel.
    QVector<quint32> linha(qMax(w, h));
    for (int passada = 0; passada < 3; ++passada) {
        // horizontal
        for (int y = 0; y < h; ++y) {
            QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int c = 0; c < 4; ++c) {
                int soma = 0;
                const int n = 2 * raio + 1;
                for (int x = -raio; x <= raio; ++x) {
                    const int xi = qBound(0, x, w - 1);
                    soma += (p[xi] >> (c * 8)) & 0xff;
                }
                for (int x = 0; x < w; ++x) {
                    linha[x] = quint32(soma / n);
                    const int sai = qBound(0, x - raio, w - 1);
                    const int entra = qBound(0, x + raio + 1, w - 1);
                    soma += int((p[entra] >> (c * 8)) & 0xff) - int((p[sai] >> (c * 8)) & 0xff);
                }
                for (int x = 0; x < w; ++x)
                    p[x] = (p[x] & ~(0xffu << (c * 8))) | (linha[x] << (c * 8));
            }
        }
        // vertical
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) {
                int soma = 0;
                const int n = 2 * raio + 1;
                auto px = [&](int y) {
                    return (reinterpret_cast<QRgb*>(img.scanLine(qBound(0, y, h - 1)))[x] >> (c * 8)) & 0xff;
                };
                for (int y = -raio; y <= raio; ++y) soma += int(px(y));
                for (int y = 0; y < h; ++y) {
                    linha[y] = quint32(soma / n);
                    soma += int(px(y + raio + 1)) - int(px(y - raio));
                }
                for (int y = 0; y < h; ++y) {
                    QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
                    p[x] = (p[x] & ~(0xffu << (c * 8))) | (linha[y] << (c * 8));
                }
            }
        }
    }
}

// ============================================================================
//  Moldura 9-slice (porte de draw9Slice)
// ============================================================================
namespace {
/// Repete um pedaço da origem dentro do destino, em vez de esticar.
void repetir(QPainter& p, const QImage& src, const QRect& s, const QRectF& d)
{
    if (s.width() <= 0 || s.height() <= 0 || d.width() <= 0 || d.height() <= 0) return;
    p.save();
    p.setClipRect(d, Qt::IntersectClip);
    for (double y = d.top(); y < d.bottom(); y += s.height())
        for (double x = d.left(); x < d.right(); x += s.width())
            p.drawImage(QRectF(x, y, s.width(), s.height()), src, QRectF(s));
    p.restore();
}
} // namespace

void desenhar9Slice(QPainter& p, const QImage& src, const QRectF& dst, int slice,
                    double escala, bool tileH, bool tileV)
{
    if (src.isNull() || dst.width() <= 0 || dst.height() <= 0) return;
    const int iw = src.width(), ih = src.height();
    const int s = qBound(1, slice, qMax(1, qMin(iw, ih) / 2));
    const double ss = qMax(1.0, double(qRound(s * (escala > 0 ? escala : 1.0))));
    const double cw = qMin(ss, std::floor(dst.width() / 2));
    const double ch = qMin(ss, std::floor(dst.height() / 2));
    const double mw = qMax(0.0, dst.width() - cw * 2);
    const double mh = qMax(0.0, dst.height() - ch * 2);
    const int scw = qMax(1, iw - s * 2);
    const int sch = qMax(1, ih - s * 2);

    const double x0 = dst.left(), y0 = dst.top();
    const double x1 = dst.right() - cw, y1 = dst.bottom() - ch;
    const double xc = dst.left() + cw, yc = dst.top() + ch;

    // cantos — nunca esticam (é o que faz a moldura parecer certa em qualquer
    // tamanho, e o motivo de 9-slice existir)
    p.drawImage(QRectF(x0, y0, cw, ch), src, QRectF(0, 0, s, s));
    p.drawImage(QRectF(x1, y0, cw, ch), src, QRectF(iw - s, 0, s, s));
    p.drawImage(QRectF(x0, y1, cw, ch), src, QRectF(0, ih - s, s, s));
    p.drawImage(QRectF(x1, y1, cw, ch), src, QRectF(iw - s, ih - s, s, s));

    if (mw > 0) {
        if (tileH) {
            repetir(p, src, QRect(s, 0, scw, s), QRectF(xc, y0, mw, ch));
            repetir(p, src, QRect(s, ih - s, scw, s), QRectF(xc, y1, mw, ch));
        } else {
            p.drawImage(QRectF(xc, y0, mw, ch), src, QRectF(s, 0, scw, s));
            p.drawImage(QRectF(xc, y1, mw, ch), src, QRectF(s, ih - s, scw, s));
        }
    }
    if (mh > 0) {
        if (tileV) {
            repetir(p, src, QRect(0, s, s, sch), QRectF(x0, yc, cw, mh));
            repetir(p, src, QRect(iw - s, s, s, sch), QRectF(x1, yc, cw, mh));
        } else {
            p.drawImage(QRectF(x0, yc, cw, mh), src, QRectF(0, s, s, sch));
            p.drawImage(QRectF(x1, yc, cw, mh), src, QRectF(iw - s, s, s, sch));
        }
    }
    if (mw > 0 && mh > 0)
        p.drawImage(QRectF(xc, yc, mw, mh), src, QRectF(s, s, scw, sch));
}

// ============================================================================
//  Dissolver
// ============================================================================
QImage renderPictureNineSlice(const QImage& src, const PictureNineSlice& spec)
{
    if (!spec.enabled || src.isNull()) return src;
    if (!spec.validFor(src.size())) return src;

    const int sw = src.width(), sh = src.height();
    const int tw = spec.width > 0 ? spec.width : sw;
    const int th = spec.height > 0 ? spec.height : sh;
    if (tw == sw && th == sh) return src;

    const int l = qBound(0, spec.left, sw - 1);
    const int t = qBound(0, spec.top, sh - 1);
    const int r = qBound(0, spec.right, sw - l - 1);
    const int b = qBound(0, spec.bottom, sh - t - 1);

    QImage out(QSize(tw, th), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect sTL(0, 0, l, t), sT(l, 0, sw-l-r, t), sTR(sw-r, 0, r, t);
    const QRect sL(0, t, l, sh-t-b), sC(l, t, sw-l-r, sh-t-b), sR(sw-r, t, r, sh-t-b);
    const QRect sBL(0, sh-b, l, b), sB(l, sh-b, sw-l-r, b), sBR(sw-r, sh-b, r, b);

    const QRect dTL(0, 0, l, t), dT(l, 0, tw-l-r, t), dTR(tw-r, 0, r, t);
    const QRect dL(0, t, l, th-t-b), dC(l, t, tw-l-r, th-t-b), dR(tw-r, t, r, th-t-b);
    const QRect dBL(0, th-b, l, b), dB(l, th-b, tw-l-r, b), dBR(tw-r, th-b, r, b);

    p.drawImage(dTL, src, sTL); p.drawImage(dTR, src, sTR);
    p.drawImage(dBL, src, sBL); p.drawImage(dBR, src, sBR);

    auto drawPatchLocal = [&](const QRect& sourceRect, const QRect& targetRect, bool tile) {
        if (sourceRect.isEmpty() || targetRect.isEmpty()) return;
        if (!tile) {
            p.drawImage(targetRect, src, sourceRect);
            return;
        }
        const QImage patch = src.copy(sourceRect);
        if (patch.isNull()) return;
        p.save();
        p.setClipRect(targetRect);
        for (int yy = targetRect.top(); yy <= targetRect.bottom(); yy += qMax(1, patch.height()))
            for (int xx = targetRect.left(); xx <= targetRect.right(); xx += qMax(1, patch.width()))
                p.drawImage(QPoint(xx, yy), patch);
        p.restore();
    };

    const bool tileEdges = spec.edgeMode == PictureNineSliceMode::Tile;
    const bool tileCenter = spec.centerMode == PictureNineSliceMode::Tile;
    drawPatchLocal(sT, dT, tileEdges);
    drawPatchLocal(sB, dB, tileEdges);
    drawPatchLocal(sL, dL, tileEdges);
    drawPatchLocal(sR, dR, tileEdges);
    drawPatchLocal(sC, dC, tileCenter);
    p.end();
    return out;
}

QImage mascaraDissolver(double avanco)
{
    // Ruído FIXO (semente constante): a mesma cutscene dissolve igual toda vez
    // que roda — sorteio novo a cada partida deixaria o efeito irrepetível e
    // impossível de testar.
    static QVector<quint8> ruido;
    if (ruido.isEmpty()) {
        ruido.resize(64 * 64);
        QRandomGenerator g(20240817u);
        for (int i = 0; i < ruido.size(); ++i) ruido[i] = quint8(g.bounded(256));
    }
    // 256 e não 255: com 255 o pixel de ruído 255 nunca some, e a imagem
    // terminava a dissolução com uns respingos para sempre na tela.
    const int limite = int(qBound(0.0, avanco, 1.0) * 256.0);
    QImage m(64, 64, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 64; ++y) {
        QRgb* linha = reinterpret_cast<QRgb*>(m.scanLine(y));
        for (int x = 0; x < 64; ++x)
            linha[x] = (ruido[y * 64 + x] < limite) ? qRgba(0, 0, 0, 0) : qRgba(0, 0, 0, 255);
    }
    return m;
}

// ============================================================================
//  Transições (porte de startTransitionIn / startTransitionOut)
// ============================================================================
TransitionState transitionAt(PictureTransition tipo, double t, bool entrando)
{
    t = qBound(0.0, t, 1.0);
    TransitionState st;
    if (tipo == PictureTransition::None) return st;

    // Na saída o avanço vai de "inteira" para "sumida"; na entrada é o
    // contrário. O plugin escreve os dois casos separados, e aqui também: a
    // curva de entrada de algumas (quicar, elástico) não é o espelho da saída.
    const double f = entrando ? t : (1.0 - t);   // 1 = totalmente visível

    switch (tipo) {
    case PictureTransition::None:
        break;
    case PictureTransition::Fade:
        st.opacityMul = f;
        break;
    case PictureTransition::SlideDown:
        st.opacityMul = f;
        st.offset = QPointF(0, entrando ? -80.0 * (1.0 - t) : 80.0 * t);
        break;
    case PictureTransition::SlideUp:
        st.opacityMul = f;
        st.offset = QPointF(0, entrando ? 80.0 * (1.0 - t) : -80.0 * t);
        break;
    case PictureTransition::SlideLeft:
        st.opacityMul = f;
        st.offset = QPointF(entrando ? 80.0 * (1.0 - t) : -80.0 * t, 0);
        break;
    case PictureTransition::SlideRight:
        st.opacityMul = f;
        st.offset = QPointF(entrando ? -80.0 * (1.0 - t) : 80.0 * t, 0);
        break;
    case PictureTransition::Zoom:
        // Compatibilidade: zoom clássico cresce de 0 -> 1 na entrada e
        // encolhe de 1 -> 0 na saída.
        st.opacityMul = f;
        st.scaleMulX = st.scaleMulY = f;
        break;
    case PictureTransition::ZoomIn:
        // Entrada aproxima (50% -> 100%); saída continua aproximando
        // (100% -> 150%). O nome descreve o movimento visual, não o destino.
        st.opacityMul = f;
        st.scaleMulX = st.scaleMulY = entrando ? (0.5 + 0.5 * t) : (1.0 + 0.5 * t);
        break;
    case PictureTransition::ZoomOut:
        // Entrada vem de perto (150% -> 100%); saída afasta (100% -> 50%).
        st.opacityMul = f;
        st.scaleMulX = st.scaleMulY = entrando ? (1.5 - 0.5 * t) : (1.0 - 0.5 * t);
        break;
    case PictureTransition::Rotate:
        st.opacityMul = f;
        st.angleAdd = entrando ? -360.0 * (1.0 - t) : 360.0 * t;
        break;
    case PictureTransition::Flip:
        st.opacityMul = f;
        st.scaleMulX = f;               // só o eixo X: a carta virando
        break;
    case PictureTransition::Elastic: {
        st.opacityMul = f;
        if (entrando) {
            const double p = 0.3;
            const double e = (t <= 0.0 || t >= 1.0)
                                 ? t
                                 : std::pow(2.0, -10.0 * t) * std::sin((t - p / 4.0) * (2.0 * M_PI) / p) + 1.0;
            st.scaleMulX = st.scaleMulY = e;
        } else {
            st.scaleMulX = st.scaleMulY = f;
        }
        break;
    }
    case PictureTransition::Shake:
        st.opacityMul = f;
        st.offset = QPointF(std::sin(t * 30.0) * 10.0 * (1.0 - t), 0);
        break;
    case PictureTransition::Dissolve:
        st.opacityMul = 1.0;            // a dissolução é por pixel, não por alfa
        st.dissolve = entrando ? (1.0 - t) : t;
        st.scaleMulX = st.scaleMulY = entrando ? (0.5 + 0.5 * t) : (1.0 - t * 0.5);
        break;
    case PictureTransition::Bounce: {
        // Só faz sentido entrando; saindo, vira um zoom.
        if (!entrando) { st.opacityMul = f; st.scaleMulX = st.scaleMulY = f; break; }
        st.opacityMul = qMin(1.0, t * 3.0);
        const double b = applyEase(PictureEase::Bounce, t);
        st.scaleMulX = st.scaleMulY = b;
        break;
    }
    }
    return st;
}

// ============================================================================
//  Composição
// ============================================================================
namespace {

/// Silhueta colorida da imagem: mantém o alfa e troca a cor. É a base do brilho.
QImage silhueta(const QImage& src, const QColor& cor)
{
    QImage s = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&s);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(s.rect(), cor);
    p.end();
    return s;
}


QPainter::CompositionMode compositionForTint(PictureTintMode mode)
{
    switch (mode) {
    case PictureTintMode::Multiply:  return QPainter::CompositionMode_Multiply;
    case PictureTintMode::Screen:    return QPainter::CompositionMode_Screen;
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    case PictureTintMode::Overlay:   return QPainter::CompositionMode_Overlay;
    case PictureTintMode::SoftLight: return QPainter::CompositionMode_SoftLight;
    case PictureTintMode::HardLight: return QPainter::CompositionMode_HardLight;
#endif
    case PictureTintMode::Normal:    break;
    }
    return QPainter::CompositionMode_SourceAtop;
}

QImage tintedImage(const QImage& src, const PictureTint& tint)
{
    if (!tint.enabled || src.isNull() || tint.strength <= 0.0) return src;
    QImage out = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QColor c = tint.color;
    c.setAlphaF(qBound(0.0, tint.strength, 1.0));
    QPainter p(&out);
    p.setCompositionMode(compositionForTint(tint.mode));
    p.fillRect(out.rect(), c);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, src);
    p.end();
    return out;
}


QImage negativeImage(const QImage& src, const PictureNegative& negative)
{
    if (!negative.enabled || src.isNull() || negative.strength <= 0.0) return src;
    const double amount = qBound(0.0, negative.strength, 1.0);
    QImage out = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            const int r = qRound(qRed(px)   * (1.0 - amount) + (255 - qRed(px))   * amount);
            const int g = qRound(qGreen(px) * (1.0 - amount) + (255 - qGreen(px)) * amount);
            const int b = qRound(qBlue(px)  * (1.0 - amount) + (255 - qBlue(px))  * amount);
            line[x] = qRgba(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), a);
        }
    }
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QRect frameRectFor(const LivePicture& lp, const QSize& size)
{
    return lp.frameRect(size);
}

} // namespace

PictureFrame VisualFxCache::build(const LivePicture& lp, const Editor& ed)
{
    return buildImage(lp, ed, QImage(), QString());
}

PictureFrame VisualFxCache::buildImage(const LivePicture& lp, const Editor& ed,
                                        const QImage& source, const QString& sourceKey)
{
    PictureFrame f;
    const PictureDef& d = lp.def;

    // A imagem base vem da biblioteca OU é o texto desenhado na hora.
    QImage arte;
    QString chaveArte;
    bool dynamicFrameAffectsPixels = false;
    bool sourcePixelsDynamic = false;
    if (d.rich.enabled) {
        const bool animado = richTextIsAnimated(d.rich);
        sourcePixelsDynamic = animado;
        const QString k = d.rich.cacheKey();
        if (animado) {
            // Cada quadro é diferente: nem adianta guardar.
            const double textExitTime = lp.phase == LivePicture::Out ? qMax(0.0, lp.phaseTime) : -1.0;
            arte = renderTextPicture(d.rich, ed, m_fonte, lp.age, -1, m_var, textExitTime);
        } else {
            auto it = m_textos.constFind(k);
            if (it == m_textos.constEnd()) {
                arte = renderTextPicture(d.rich, ed, m_fonte, 0.0, -1, m_var);
                m_textos.insert(k, arte);
            } else {
                arte = *it;
            }
        }
        chaveArte = QStringLiteral("txt:") + k;
    } else {
        const PictureAsset* a = nullptr;
        QImage original;
        QString assetKey;
        if (!source.isNull()) {
            original = source;
            assetKey = sourceKey.isEmpty() ? QStringLiteral("external:%1").arg(source.cacheKey()) : sourceKey;
        } else {
            a = ed.pictureFor(d.assetId, d.assetName);
            if (!a || !a->isValid()) return f;
            original = a->image;
            assetKey = a->id;
        }
        arte = original;
        if (d.animated || d.frameCount > 1) {
            const QRect fr = frameRectFor(lp, arte.size());
            // Configuração inválida nunca torna a Picture invisível: mantém a
            // imagem inteira como fallback e o teste/diagnóstico acusa o layout.
            dynamicFrameAffectsPixels = !fr.isEmpty();
            if (dynamicFrameAffectsPixels) arte = arte.copy(fr);
        }
        const QRect frameRect = frameRectFor(lp, original.size());
        const int frameKey = (d.animated || d.frameCount > 1)
            ? (dynamicFrameAffectsPixels ? lp.effectiveFrameIndex() : -2) : -1;
        // Flip é transformação geométrica oficial (RuntimePictureTransform),
        // não mutação dos pixels. Assim âncora/pivot, rotação, escala,
        // transições e flip compartilham a mesma matriz em CPU e QRhi.
        chaveArte = QStringLiteral("%1|fr%2|%3x%4|valid%5")
            .arg(assetKey).arg(frameKey)
            .arg(d.frameColumns).arg(d.frameRows).arg(int(!frameRect.isEmpty()));
    }
    if (arte.isNull()) return f;

    // Pictures 2.0: layout e color transforms pertencem ao mesmo pipeline
    // independentemente de a origem ser bitmap ou Picture Text.
    arte = renderPictureNineSlice(arte, d.nineSlice);
    if (d.fx.tone.enabled)
        core::applyScreenTone(arte, d.fx.tone.red, d.fx.tone.green, d.fx.tone.blue, d.fx.tone.gray);
    // Tint é apenas compatibilidade de projeto legado; novos comandos usam Tonalidade.
    arte = tintedImage(arte, d.fx.tint);
    PictureNegative runtimeNegative = d.fx.negative;
    runtimeNegative.enabled = lp.effectiveNegativeStrength() > 0.0;
    runtimeNegative.strength = lp.effectiveNegativeStrength();
    arte = negativeImage(arte, runtimeNegative);
    const QVariantMap nineMap = d.nineSlice.toParams();
    chaveArte += QStringLiteral("|nine%1|tone%2,%3,%4,%5|tint%6%7%8|neg%9,%10")
        .arg(QString::fromUtf8(QJsonDocument::fromVariant(nineMap).toJson(QJsonDocument::Compact)))
        .arg(d.fx.tone.red).arg(d.fx.tone.green).arg(d.fx.tone.blue).arg(d.fx.tone.gray)
        .arg(int(d.fx.tint.enabled)).arg(d.fx.tint.color.name(QColor::HexArgb)).arg(d.fx.tint.strength)
        .arg(int(runtimeNegative.enabled)).arg(runtimeNegative.strength);

    f.valid = true;
    f.baseSize = QSizeF(arte.size());
    f.opacity = lp.effectiveOpacity();
    const PictureEffects& fx = d.fx;

    // ---- 1) piscar: mexe só na opacidade, nunca nos pixels ----------------
    if (fx.blink.enabled) {
        const double minA = qBound(0.0, fx.blink.minAlpha, 1.0);
        const double speed = qMax(0.1, fx.blink.speed);
        const double activeSeconds = 1.0 / speed;
        const double delaySeconds = qMax(0.0, fx.blink.delay);
        const double totalSeconds = activeSeconds + delaySeconds;
        const double localAge = totalSeconds > 0.0 ? std::fmod(lp.age, totalSeconds) : lp.age;
        if (localAge <= activeSeconds || delaySeconds <= 0.0) {
            const double t = delaySeconds > 0.0 ? (localAge / activeSeconds) : lp.age * speed;
            if (fx.blink.hard) {
                const double ciclo = std::fmod(t, 1.0);
                f.opacity *= (ciclo < 0.5) ? 1.0 : minA;
            } else {
                const double onda = (std::sin(t * 2.0 * M_PI) + 1.0) / 2.0;
                f.opacity *= minA + (1.0 - minA) * onda;
            }
        }
    }

    // ---- 2) transição -----------------------------------------------------
    TransitionState tr;
    if (lp.phase != LivePicture::Normal && lp.phaseDuration > 0.0) {
        const double t = qBound(0.0, lp.phaseTime / lp.phaseDuration, 1.0);
        tr = transitionAt(lp.phaseType, t, lp.phase == LivePicture::In);
        f.opacity *= tr.opacityMul;
        f.scaleMulX = tr.scaleMulX;
        f.scaleMulY = tr.scaleMulY;
        f.angleAdd = tr.angleAdd;
        f.posOffset = tr.offset;
    }

    // ---- 3) parte estática (borda, brilho, máscara) — vem do cache --------
    const QString chave = chaveArte + QLatin1Char('|') + fx.staticKey();

    // Contrato de versão do slot: a GPU só reenvia uma textura quando os
    // PIXELS mudam. Antes, alterações estáticas como um 9-slice podiam manter
    // a mesma versão no caminho rápido, enquanto certos efeitos estáticos
    // incrementavam a versão a cada frame. Centralizar aqui mantém CPU/QRhi
    // em paridade sem uploads desnecessários.
    auto marcarPixels = [this, &d](const QString& pixelKey, bool dinamico) {
        const QString anterior = m_ultimaChave.value(d.number);
        if (anterior != pixelKey) {
            m_ultimaChave[d.number] = pixelKey;
            m_versoes[d.number] = m_versoes.value(d.number, 0) + 1;
        } else if (dinamico) {
            m_versoes[d.number] = m_versoes.value(d.number, 0) + 1;
        }
    };

    QImage base;
    QPointF offset;
    if (!fx.any() && tr.dissolve <= 0.0) {
        f.image = arte;
        f.drawOffset = QPointF(0, 0);
        marcarPixels(chave, sourcePixelsDynamic);
        return f;                        // caminho rápido: nada a compor
    }

    // Picture Text animado muda os pixels a cada frame. Reutilizar uma
    // composição estática nesse caso congelava o primeiro frame sempre que
    // Tint/Negative/Borda/etc. também estavam ligados. Bitmap/spritesheet usa
    // chave por frame e continua aproveitando o cache normalmente.
    const bool podeCachearEstatico = !sourcePixelsDynamic && lp.negativeDuration <= 0.0;
    auto it = m_estaticos.constFind(chave);
    if (podeCachearEstatico && it != m_estaticos.constEnd() && !fx.staticKey().isEmpty()) {
        base = it->img;
        offset = it->offset;
    } else if (fx.staticKey().isEmpty()) {
        base = arte;
        offset = QPointF(0, 0);
    } else {
        // Quanto a imagem cresce: o brilho vaza para fora e a borda 9-slice
        // pode ficar por fora também.
        int margem = 0;
        if (fx.glow.enabled) margem = qMax(margem, int(std::ceil(fx.glow.strength * 2.0)));
        if (fx.border.enabled) {
            margem = qMax(margem, fx.border.style == PictureBorderStyle::NineSlice
                                      ? int(std::ceil(fx.border.slice * qMax(1.0, fx.border.scale)))
                                      : int(std::ceil(fx.border.width)));
            margem += qMax(qAbs(fx.border.paddingX), qAbs(fx.border.paddingY));
        }
        const QSize tam = arte.size() + QSize(margem * 2, margem * 2);
        base = QImage(tam, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::transparent);
        offset = QPointF(-margem, -margem);

        QPainter p(&base);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        const QRectF corpo(margem, margem, arte.width(), arte.height());

        const QRectF moldura = corpo.adjusted(-fx.border.paddingX, -fx.border.paddingY,
                                              fx.border.paddingX, fx.border.paddingY);

        // Glow é sempre o fundo. Quando "somente borda" está ativo, a fonte
        // do halo é a moldura, e não os pixels/texto da Picture.
        if (fx.glow.enabled && fx.glow.strength > 0.0) {
            QImage fonteGlow(tam, QImage::Format_ARGB32_Premultiplied);
            fonteGlow.fill(Qt::transparent);
            {
                QPainter gp(&fonteGlow);
                if (fx.glow.borderOnly && fx.border.enabled) {
                    if (fx.border.style == PictureBorderStyle::NineSlice) {
                        if (const PictureAsset* b = ed.pictureById(fx.border.imageAssetId))
                            desenhar9Slice(gp, b->image, moldura, fx.border.slice, fx.border.scale,
                                           fx.border.tileH, fx.border.tileV);
                    } else {
                        QPen pen(Qt::white, qMax(0.5, fx.border.width));
                        pen.setJoinStyle(Qt::MiterJoin);
                        gp.setPen(pen); gp.setBrush(Qt::NoBrush);
                        const double meio = fx.border.width / 2.0;
                        gp.drawRect(moldura.adjusted(meio, meio, -meio, -meio));
                    }
                } else {
                    gp.drawImage(corpo.topLeft(), arte);
                }
            }
            QImage g = silhueta(fonteGlow, fx.glow.color);
            borrarCaixa(g, qBound(1, int(fx.glow.strength), 30));
            p.drawImage(0, 0, g);
            p.drawImage(0, 0, g);
        }

        // A borda/moldura é composta ANTES do conteúdo. Isto é importante
        // para Picture Text: glyphs nunca ficam escondidos sob a borda.
        if (fx.border.enabled) {
            if (fx.border.style == PictureBorderStyle::NineSlice) {
                if (const PictureAsset* b = ed.pictureById(fx.border.imageAssetId)) {
                    QImage bordaArte = b->image;
                    if (fx.border.tint != QColor(255, 255, 255)) {
                        QPainter tp(&bordaArte);
                        tp.setCompositionMode(QPainter::CompositionMode_Multiply);
                        tp.fillRect(bordaArte.rect(), fx.border.tint);
                        tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                        tp.drawImage(0, 0, b->image);
                    }
                    p.setOpacity(qBound(0.0, fx.border.alpha, 1.0));
                    desenhar9Slice(p, bordaArte, moldura, fx.border.slice, fx.border.scale,
                                   fx.border.tileH, fx.border.tileV);
                    p.setOpacity(1.0);
                }
            } else {
                QColor c = fx.border.color;
                c.setAlphaF(qBound(0.0, fx.border.alpha, 1.0));
                QPen caneta(c, qMax(0.5, fx.border.width));
                caneta.setJoinStyle(Qt::MiterJoin);
                p.setPen(caneta); p.setBrush(Qt::NoBrush);
                const double meio = fx.border.width / 2.0;
                p.drawRect(moldura.adjusted(meio, meio, -meio, -meio));
                p.setPen(Qt::NoPen);
            }
        }

        // conteúdo por cima da moldura
        p.drawImage(corpo.topLeft(), arte);

        // máscara: recorta conteúdo e moldura usando o mesmo contrato antigo.
        if (fx.mask.enabled) {
            if (const PictureAsset* m = ed.pictureById(fx.mask.assetId)) {
                QImage mask(tam, QImage::Format_ARGB32_Premultiplied);
                mask.fill(Qt::transparent);
                QPainter mp(&mask);
                mp.setRenderHint(QPainter::SmoothPixmapTransform, true);
                const QPointF center=corpo.center()+QPointF(fx.mask.offsetX,fx.mask.offsetY);
                mp.translate(center); mp.rotate(fx.mask.angle);
                mp.scale(fx.mask.scaleX/100.0,fx.mask.scaleY/100.0);
                mp.drawImage(QRectF(-corpo.width()/2.0,-corpo.height()/2.0,
                                    corpo.width(),corpo.height()),m->image,
                             QRectF(QPointF(0,0),QSizeF(m->image.size())));
                mp.resetTransform();
                if (fx.mask.invert) {
                    mp.setCompositionMode(QPainter::CompositionMode_SourceOut);
                    mp.fillRect(mask.rect(), Qt::black);
                }
                mp.end();
                p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                p.drawImage(0, 0, mask);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
        }
        p.end();
        if (podeCachearEstatico)
            m_estaticos.insert(chave, Estatico{ base, offset });
    }

    // ---- 4) parte dinâmica (onda, brilho deslizante, dissolver) -----------
    const bool precisaDinamico = (fx.distort.enabled && fx.distort.amplitude > 0.0) ||
                                 (fx.shine.enabled && fx.shine.width > 0.0) ||
                                 (fx.glow.enabled && fx.glow.blink) ||
                                 tr.dissolve > 0.0;
    if (!precisaDinamico) {
        f.image = base;
        f.drawOffset = offset;
        marcarPixels(chave, sourcePixelsDynamic);
        return f;
    }

    QImage dinamica = base;
    QPointF dinOffset = offset;

    // onda horizontal: cada linha desliza um pouco
    if (fx.distort.enabled && fx.distort.amplitude > 0.0) {
        const double amp = fx.distort.amplitude;
        const int extra = int(std::ceil(amp)) + 1;
        QImage saida(dinamica.width() + extra * 2, dinamica.height(),
                     QImage::Format_ARGB32_Premultiplied);
        saida.fill(Qt::transparent);
        QPainter p(&saida);
        const double lam = qMax(1.0, fx.distort.wavelength);
        const double fase = lp.age * fx.distort.speed * 2.0 * M_PI;
        for (int y = 0; y < dinamica.height(); ++y) {
            const double dx = std::sin(y / lam * 2.0 * M_PI + fase) * amp;
            p.drawImage(QPointF(extra + dx, y), dinamica,
                        QRectF(0, y, dinamica.width(), 1));
        }
        p.end();
        dinamica = saida;
        dinOffset -= QPointF(extra, 0);
    }

    // brilho pulsante: reforça a silhueta em um ciclo separado do Blink
    if (fx.glow.enabled && fx.glow.blink && fx.glow.strength > 0.0) {
        const double pulse = (std::sin(lp.age * qMax(0.1, fx.glow.blinkSpeed) * 2.0 * M_PI) + 1.0) / 2.0;
        if (pulse > 0.02) {
            QImage g(dinamica.size(), QImage::Format_ARGB32_Premultiplied);
            g.fill(Qt::transparent);
            {
                QPainter gp(&g);
                QColor c = fx.glow.color;
                c.setAlphaF(qBound(0.0, pulse, 1.0));
                gp.drawImage(QPointF(0, 0), silhueta(dinamica, c));
            }
            borrarCaixa(g, qBound(1, int(fx.glow.strength), 30));
            QPainter gp(&dinamica);
            gp.setCompositionMode(QPainter::CompositionMode_Plus);
            gp.drawImage(0, 0, g);
        }
    }

    // brilho deslizante: faixa clara atravessando, presa ao alfa da imagem
    if (fx.shine.enabled && fx.shine.width > 0.0) {
        const double w = dinamica.width();
        const double faixa = qMax(2.0, fx.shine.width);
        const double travel = 1.0 / qMax(0.01, fx.shine.speed);
        const double cycle = travel + qMax(0.0, fx.shine.delay);
        double local = std::fmod(qMax(0.0, lp.age), qMax(0.001, cycle));
        if (local < 0.0) local += cycle;
        // Durante o delay não existe uma segunda faixa escondida: a anterior
        // já saiu totalmente. Isso elimina o reinício abrupto nas bordas.
        if (local < travel) {
            const double u = qBound(0.0, local / travel, 1.0);
            const double pos = -faixa + u * (w + faixa * 2.0);
            QPainter p(&dinamica);
            p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            QLinearGradient g(pos, 0, pos + faixa, dinamica.height());
            QColor c = fx.shine.color;
            c.setAlpha(0); g.setColorAt(0.0, c);
            c.setAlpha(200); g.setColorAt(0.5, c);
            c.setAlpha(0); g.setColorAt(1.0, c);
            p.fillRect(dinamica.rect(), g);
            p.end();
        }
    }

    // dissolver: recorta pelo ruído fixo
    if (tr.dissolve > 0.0) {
        QPainter p(&dinamica);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        const QImage m = mascaraDissolver(tr.dissolve);
        p.drawTiledPixmap(dinamica.rect(), QPixmap::fromImage(m));
        p.end();
    }

    f.image = dinamica;
    f.drawOffset = dinOffset;
    // Efeitos desta seção alteram pixels em função do relógio da Picture.
    marcarPixels(chave, true);
    return f;
}

} // namespace game
