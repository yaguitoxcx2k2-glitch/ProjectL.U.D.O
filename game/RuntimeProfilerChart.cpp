#include "RuntimeProfilerChart.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <QPalette>
#include <QtMath>
namespace game {
RuntimeProfilerChart::RuntimeProfilerChart(QWidget* parent):QWidget(parent){setMinimumHeight(150);setAccessibleName(tr("Histórico de frame time"));}
void RuntimeProfilerChart::addFrame(double frameMs,double p95Ms){if(!qIsFinite(frameMs)||frameMs<0)return;m_frames.push_back(frameMs);if(m_frames.size()>m_capacity)m_frames.remove(0,m_frames.size()-m_capacity);m_p95=p95Ms;update();}
void RuntimeProfilerChart::clearHistory(){m_frames.clear();m_p95=0;update();}
QSize RuntimeProfilerChart::minimumSizeHint() const{return QSize(420,150);}
void RuntimeProfilerChart::paintEvent(QPaintEvent*){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().base());
    const QRectF r=rect().adjusted(38,10,-10,-24);if(r.width()<=1||r.height()<=1)return;
    const double maxMs=qMax(33.333,m_frames.isEmpty()?16.667:*std::max_element(m_frames.cbegin(),m_frames.cend()));
    auto yFor=[&](double ms){return r.bottom()-qBound(0.0,ms/maxMs,1.0)*r.height();};
    p.setPen(palette().mid().color());
    for(double ms:{16.667,33.333}){if(ms>maxMs)continue;const qreal y=yFor(ms);p.drawLine(QPointF(r.left(),y),QPointF(r.right(),y));p.drawText(QRectF(0,y-9,35,18),Qt::AlignRight|Qt::AlignVCenter,QString::number(ms, 'f', 1));}
    if(m_p95>0&&m_p95<=maxMs){QPen pen(palette().highlight().color());pen.setStyle(Qt::DashLine);p.setPen(pen);const qreal y=yFor(m_p95);p.drawLine(QPointF(r.left(),y),QPointF(r.right(),y));}
    if(m_frames.size()>1){QPainterPath path;for(int i=0;i<m_frames.size();++i){const qreal x=r.left()+r.width()*i/qMax(1,m_capacity-1);const QPointF pt(x,yFor(m_frames.at(i)));if(i==0)path.moveTo(pt);else path.lineTo(pt);}QPen pen(palette().text().color());pen.setWidthF(1.5);p.setPen(pen);p.drawPath(path);}
    p.setPen(palette().text().color());p.drawText(QRectF(r.left(),r.bottom()+3,r.width(),20),Qt::AlignLeft|Qt::AlignVCenter,tr("últimos %1 frames · escala 0–%2 ms").arg(m_frames.size()).arg(maxMs,0,'f',1));
}
}
