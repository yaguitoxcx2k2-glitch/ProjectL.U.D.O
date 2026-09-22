// ============================================================================
//  Quantize.h — Redução de cores para 256 (PNG indexado de 8 bits).
//
//  Motivo: o formato clássico de 256 cores só aceita imagens de 256 cores indexadas —
//  PNG truecolor de 24 bits é recusado pelo editor. Além disso, o engine trata
//  o **índice 0 da paleta** como a cor transparente.
//
//  A quantização usa **median cut** com paleta ótima por imagem e mapeamento
//  para a cor mais próxima (sem dithering por padrão: dithering espalha ruído
//  e destrói pixel art).
// ============================================================================
#pragma once

#include <QColor>
#include <QImage>

namespace core { namespace quantize {

struct Options {
    /// Total de entradas da paleta, incluindo o índice 0.
    int    maxColors = 256;
    /// Cor gravada no índice 0 — a cor "transparente" para o editores de RPG.
    QColor index0 = QColor(Qt::black);
    /// Espalha o erro de quantização entre pixels vizinhos (Floyd–Steinberg).
    /// Desligado por padrão: em pixel art o resultado fica pior.
    bool   dither = false;
    /// Pixels com alfa abaixo disto viram o índice 0.
    int    alphaThreshold = 128;
};

struct Result {
    int  originalColors = 0;   ///< cores distintas na imagem original
    int  paletteColors  = 0;   ///< entradas usadas na paleta final (com o índice 0)
    bool lossless       = false; ///< true = coube tudo, nenhuma cor foi aproximada
    bool hasTransparency = false;
};

/// Quantidade de cores RGB distintas (ignora os pixels transparentes).
int countDistinctColors(const QImage& img, int alphaThreshold = 128);

/// Converte para QImage::Format_Indexed8 com no máximo `opt.maxColors` cores,
/// reservando o índice 0 para a transparência.
QImage toIndexed(const QImage& src, const Options& opt, Result* result = nullptr);

}} // namespace core::quantize
