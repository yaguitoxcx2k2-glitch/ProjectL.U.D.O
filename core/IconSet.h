// ============================================================================
//  IconSet.h — Folha de ícones do projeto.
//
//  Uma imagem única dividida em células iguais, como um tileset: o ícone 0 é a
//  primeira célula, o 1 é a seguinte, e assim por diante, da esquerda para a
//  direita e de cima para baixo. É o mesmo arranjo do editores de RPG, e é o que faz
//  `\I[5]` no meio de uma frase ter significado.
// ============================================================================
#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace core {

struct IconBlock {
    QString name, sourcePath;
    int x=0,y=0,cols=0,rows=0,startIndex=0;
    int count() const{return cols*rows;}
};

struct IconSet {
    QImage  image;
    QString sourcePath;      ///< compatibilidade com projetos antigos
    int     cellW = 32;
    int     cellH = 32;
    QVector<IconBlock> blocks;

    bool isValid() const { return !image.isNull() && cellW > 0 && cellH > 0; }
    int  columns() const { return isValid() ? qMax(1, image.width() / cellW) : 0; }
    int  rows()    const { return isValid() ? qMax(1, image.height() / cellH) : 0; }
    int  count()   const;
    QRect iconRect(int n) const;
    /// Empilha uma nova folha sem alterar os números já existentes.
    int appendImage(const QImage& img,const QString& name,const QString& source);
};

} // namespace core
