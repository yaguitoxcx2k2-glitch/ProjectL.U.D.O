#include "IconSet.h"
#include <QPainter>
namespace core {
int IconSet::count() const
{
    if(!isValid())return 0;
    if(blocks.isEmpty())return columns()*rows();
    int n=0;for(const IconBlock& b:blocks)n+=b.count();return n;
}
QRect IconSet::iconRect(int n) const
{
    if(!isValid()||n<0||n>=count())return {};
    if(blocks.isEmpty()){const int c=columns();return QRect((n%c)*cellW,(n/c)*cellH,cellW,cellH);}
    for(const IconBlock& b:blocks)if(n>=b.startIndex&&n<b.startIndex+b.count()){
        const int local=n-b.startIndex;return QRect(b.x+(local%b.cols)*cellW,b.y+(local/b.cols)*cellH,cellW,cellH);
    }
    return {};
}
int IconSet::appendImage(const QImage& src,const QString& name,const QString& source)
{
    if(src.isNull()||cellW<=0||cellH<=0)return 0;
    const int newCols=src.width()/cellW,newRows=src.height()/cellH;
    if(newCols<=0||newRows<=0)return 0;
    const QImage part=src.copy(0,0,newCols*cellW,newRows*cellH).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(!isValid()){image=part;sourcePath=source;blocks={{name,source,0,0,newCols,newRows,0}};return newCols*newRows;}
    if(blocks.isEmpty())blocks.push_back({QStringLiteral("Folha 1"),sourcePath,0,0,columns(),rows(),0});
    const int start=count(),oldH=image.height(),newW=qMax(image.width(),part.width());
    QImage atlas(newW,oldH+part.height(),QImage::Format_ARGB32_Premultiplied);atlas.fill(Qt::transparent);
    {QPainter p(&atlas);p.drawImage(0,0,image);p.drawImage(0,oldH,part);}image=atlas;
    blocks.push_back({name,source,0,oldH,newCols,newRows,start});return newCols*newRows;
}
}
