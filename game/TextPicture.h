// ============================================================================
//  TextPicture.h — Desenha um texto rico numa QImage (ShowRichTextPicture).
//
//  É o que transforma "Capítulo I" numa picture: mede o texto, decide o
//  tamanho da imagem, pinta o fundo escolhido e desenha as letras com a mesma
//  função que a caixa de mensagem e as legendas usam (`drawTextPage`).
//
//  Como é uma imagem comum no fim das contas, ela herda tudo o que uma picture
//  sabe fazer — girar, escalar, ganhar moldura, entrar quicando, dissolver.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/Picture.h"

#include <QHash>
#include <QImage>

#include <functional>

namespace game {

/// Renderiza o texto. `tempo` alimenta as animações por letra; `revealed`
/// limita quantas letras aparecem (-1 = todas).
QImage renderTextPicture(const core::PictureRichText& rt, const core::Editor& ed,
                         const QFont& fonteBase, double tempo, int revealed = -1,
                         const std::function<QString(int)>& varValue = {},
                         double exitTime = -1.0);

/// O texto tem alguma animação por letra? (decide se recompõe a cada quadro)
bool richTextIsAnimated(const core::PictureRichText& rt);

/// Carrega as fontes `.ttf`/`.otf` da pasta `fonts/` ao lado do projeto, para
/// que `fontFamily` possa usá-las pelo nome. Roda uma vez por caminho.
/// Carrega uma fonte uma única vez por processo. Usado também pelo preload
/// de F5/F6 para que GameSession não repita I/O/registro no primeiro frame.
bool carregarFonteDoProjeto(const QString& arquivo);
void carregarFontesDoProjeto(const QString& caminhoDoProjeto);

} // namespace game
