#include "Icons.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <QSet>
#include <cmath>
#include <QPainterPath>
#include <QPolygonF>

namespace ui { namespace icons {

namespace {

/// M_PI nao e definido pelo MSVC sem _USE_MATH_DEFINES; usamos uma constante nossa.
constexpr double kPi = 3.14159265358979323846;

const QColor kInk  = QColor("#D7DAE0");   // traço/neutro principal
const QColor kAcc  = QColor("#5DADE2");   // azul principal
const QColor kWarn = QColor("#F2C66D");
const QColor kBad  = QColor("#F28C8C");
const QColor kVeg  = QColor("#55C2C3");

/// Desenha o ícone `name` num quadrado 0..64 (o painter já está escalado).
void paintIcon(QPainter& p, const QString& name, const QColor& ink)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (name == QLatin1String("brush")) {
        p.setPen(QPen(ink, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(42, 14, 54, 26);                       // cabo
        QPainterPath head;
        head.moveTo(40, 16);
        head.lineTo(52, 28);
        head.lineTo(26, 48);
        head.lineTo(16, 52);
        head.lineTo(20, 42);
        head.closeSubpath();
        p.setBrush(kAcc);
        p.setPen(QPen(ink, 3));
        p.drawPath(head);
    } else if (name == QLatin1String("eraser")) {
        p.setBrush(QColor("#55C2C3"));
        p.setPen(QPen(ink, 4));
        QPolygonF body({ QPointF(14, 42), QPointF(36, 18), QPointF(52, 34), QPointF(30, 54) });
        p.drawPolygon(body);
        p.setPen(QPen(ink, 3));
        p.drawLine(24, 32, 40, 48);
        p.setPen(QPen(kBad, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(12, 56, 54, 56);
    } else if (name == QLatin1String("fill")) {
        p.setBrush(kAcc);
        p.setPen(QPen(ink, 4));
        QPolygonF can({ QPointF(14, 30), QPointF(34, 12), QPointF(52, 32), QPointF(32, 50) });
        p.drawPolygon(can);
        p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(22, 22, 22, 40);
        p.setBrush(kAcc);
        p.setPen(Qt::NoPen);
        QPainterPath drop;
        drop.moveTo(54, 36);
        drop.quadTo(64, 48, 54, 54);
        drop.quadTo(44, 48, 54, 36);
        p.drawPath(drop);
    } else if (name == QLatin1String("rect")) {
        p.setPen(QPen(ink, 5));
        p.setBrush(QColor(93, 173, 226, 70));
        p.drawRect(12, 16, 40, 32);
    } else if (name == QLatin1String("circle")) {
        p.setPen(QPen(ink, 5));
        p.setBrush(QColor(93, 173, 226, 70));
        p.drawEllipse(QRectF(10, 14, 44, 36));
    } else if (name == QLatin1String("line")) {
        p.setPen(QPen(ink, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(14, 50, 50, 16);
        p.setBrush(kAcc);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(14, 50), 7, 7);
        p.drawEllipse(QPointF(50, 16), 7, 7);
    } else if (name == QLatin1String("terrain")) {
        p.setPen(Qt::NoPen);
        p.setBrush(kVeg);
        QPainterPath leaf;
        leaf.moveTo(18, 48);
        leaf.quadTo(18, 18, 50, 16);
        leaf.quadTo(50, 46, 18, 48);
        p.drawPath(leaf);
        p.setPen(QPen(QColor("#9B8AFB"), 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(20, 50, 44, 26);
    } else if (name == QLatin1String("select")) {
        p.setBrush(ink);
        p.setPen(QPen(QColor("#F28C8C"), 2));
        QPolygonF arrow({ QPointF(20, 10), QPointF(20, 50), QPointF(30, 40),
                          QPointF(37, 55), QPointF(45, 51), QPointF(38, 37), QPointF(50, 34) });
        p.drawPolygon(arrow);
    } else if (name == QLatin1String("object")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor(155, 138, 251, 120));
        QPolygonF dia({ QPointF(32, 10), QPointF(54, 32), QPointF(32, 54), QPointF(10, 32) });
        p.drawPolygon(dia);
    } else if (name == QLatin1String("undo") || name == QLatin1String("redo")) {
        const bool re = name == QLatin1String("redo");
        p.save();
        if (re) { p.translate(64, 0); p.scale(-1, 1); }
        QPainterPath arc;
        arc.moveTo(50, 44);
        arc.arcTo(QRectF(14, 16, 36, 36), 0, 200);
        p.setPen(QPen(ink, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(arc);
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({ QPointF(10, 22), QPointF(28, 22), QPointF(16, 38) }));
        p.restore();
    } else if (name == QLatin1String("grid") || name == QLatin1String("multigrid")) {
        p.setPen(QPen(ink, 4));
        p.drawRect(10, 10, 44, 44);
        p.setPen(QPen(name == QLatin1String("multigrid") ? kAcc : ink, 3));
        p.drawLine(24, 10, 24, 54);
        p.drawLine(40, 10, 40, 54);
        p.drawLine(10, 24, 54, 24);
        p.drawLine(10, 40, 54, 40);
    } else if (name == QLatin1String("focus")) {
        p.setPen(QPen(kWarn, 5));
        p.setBrush(QColor(242, 198, 109, 60));
        p.drawEllipse(QRectF(18, 14, 28, 28));
        p.setPen(QPen(kWarn, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(26, 46, 38, 46);
        p.drawLine(28, 53, 36, 53);
    } else if (name == QLatin1String("ghost")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor(155, 138, 251, 110));
        QPainterPath g;
        g.moveTo(14, 52);
        g.lineTo(14, 30);
        g.arcTo(QRectF(14, 10, 36, 36), 180, -180);
        g.lineTo(50, 52);
        g.lineTo(41, 45);
        g.lineTo(32, 52);
        g.lineTo(23, 45);
        g.closeSubpath();
        p.drawPath(g);
        p.setBrush(QColor("#F28C8C"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(25, 30), 3.5, 4.5);
        p.drawEllipse(QPointF(39, 30), 3.5, 4.5);
    } else if (name == QLatin1String("ontop")) {
        p.setPen(QPen(ink, 3));
        p.setBrush(QColor("#9B8AFB"));
        p.drawRect(16, 34, 34, 16);
        p.setBrush(kAcc);
        p.drawRect(22, 18, 34, 16);
    } else if (name == QLatin1String("random")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor("#55C2C3"));
        p.drawRoundedRect(QRectF(12, 12, 40, 40), 8, 8);
        p.setBrush(QColor("#F28C8C"));
        p.setPen(Qt::NoPen);
        for (QPointF pt : { QPointF(23, 23), QPointF(41, 23), QPointF(32, 32),
                            QPointF(23, 41), QPointF(41, 41) })
            p.drawEllipse(pt, 4, 4);
    } else if (name == QLatin1String("snap")) {
        p.setPen(QPen(kBad, 7, Qt::SolidLine, Qt::FlatCap));
        QPainterPath u;
        u.moveTo(18, 44);
        u.lineTo(18, 28);
        u.arcTo(QRectF(18, 12, 28, 32), 180, -180);
        u.lineTo(46, 44);
        p.drawPath(u);
        p.setPen(QPen(QColor("#55C2C3"), 7, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(18, 44, 18, 54);
        p.drawLine(46, 44, 46, 54);
    } else if (name == QLatin1String("filled")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(kAcc);
        p.drawRect(12, 16, 40, 32);
    } else if (name == QLatin1String("star")) {
        p.setPen(QPen(QColor("#F2C66D"), 2));
        p.setBrush(kWarn);
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double ang = -kPi / 2 + i * kPi / 5;
            const double r = (i % 2 == 0) ? 24 : 10;
            star << QPointF(32 + r * std::cos(ang), 32 + r * std::sin(ang));
        }
        p.drawPolygon(star);
    } else if (name == QLatin1String("collision")) {
        p.setPen(QPen(kBad, 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(16, 16, 48, 48);
        p.drawLine(48, 16, 16, 48);
    } else if (name == QLatin1String("add")) {
        p.setPen(QPen(kVeg, 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(32, 14, 32, 50);
        p.drawLine(14, 32, 50, 32);
    } else if (name == QLatin1String("remove")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor("#9B8AFB"));
        p.drawRect(18, 22, 28, 32);
        p.setPen(QPen(kBad, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(14, 18, 50, 18);
        p.drawLine(26, 30, 26, 46);
        p.drawLine(38, 30, 38, 46);
    } else if (name == QLatin1String("duplicate")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor("#9B8AFB"));
        p.drawRect(12, 12, 30, 30);
        p.setBrush(kAcc);
        p.drawRect(24, 24, 30, 30);
    } else if (name == QLatin1String("up") || name == QLatin1String("down")) {
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        if (name == QLatin1String("up"))
            p.drawPolygon(QPolygonF({ QPointF(32, 12), QPointF(52, 40), QPointF(12, 40) }));
        else
            p.drawPolygon(QPolygonF({ QPointF(32, 52), QPointF(52, 24), QPointF(12, 24) }));
    } else if (name == QLatin1String("outdent")) {
        // Sair da pasta: linha de hierarquia + seta para a esquerda.
        p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(20, 14, 20, 50);
        p.drawLine(20, 22, 49, 22);
        p.drawLine(20, 42, 49, 42);
        p.setBrush(kAcc);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({ QPointF(8, 32), QPointF(26, 20), QPointF(26, 28),
                                  QPointF(48, 28), QPointF(48, 36), QPointF(26, 36),
                                  QPointF(26, 44) }));
    } else if (name == QLatin1String("group") || name == QLatin1String("folder")) {
        p.setPen(QPen(ink, 3));
        p.setBrush(QColor("#F2C66D"));
        QPainterPath f;
        f.moveTo(10, 48);
        f.lineTo(10, 18);
        f.lineTo(26, 18);
        f.lineTo(31, 24);
        f.lineTo(54, 24);
        f.lineTo(54, 48);
        f.closeSubpath();
        p.drawPath(f);
    } else if (name == QLatin1String("objectlayer")) {
        p.setPen(QPen(ink, 3));
        p.setBrush(QColor(93, 173, 226, 130));
        p.drawRect(12, 14, 22, 18);
        p.drawRect(30, 32, 22, 18);
    } else if (name == QLatin1String("image")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor("#5DADE2"));
        p.drawRect(10, 14, 44, 36);
        p.setBrush(kWarn);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(22, 26), 5, 5);
        p.setBrush(QColor("#55C2C3"));
        p.drawPolygon(QPolygonF({ QPointF(14, 48), QPointF(30, 30), QPointF(46, 48) }));
    } else if (name == QLatin1String("tileset")) {
        p.setPen(QPen(ink, 3));
        const QColor cols[4] = { QColor("#55C2C3"), QColor("#5DADE2"),
                                 QColor("#F2C66D"), QColor("#9B8AFB") };
        int i = 0;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                p.setBrush(cols[i++]);
                p.drawRect(12 + x * 21, 12 + y * 21, 20, 20);
            }
    } else if (name == QLatin1String("convert")) {
        p.setPen(QPen(kAcc, 6, Qt::SolidLine, Qt::RoundCap));
        QPainterPath a;
        a.moveTo(48, 26);
        a.arcTo(QRectF(14, 14, 36, 36), 60, 240);
        p.drawPath(a);
        p.setBrush(kAcc);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({ QPointF(52, 12), QPointF(54, 32), QPointF(36, 24) }));
    } else if (name == QLatin1String("resize")) {
        p.setPen(QPen(ink, 4, Qt::DashLine));
        p.drawRect(10, 10, 44, 44);
        p.setPen(QPen(kAcc, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(22, 42, 42, 22);
        p.setBrush(kAcc);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({ QPointF(44, 16), QPointF(48, 32), QPointF(32, 28) }));
        p.drawPolygon(QPolygonF({ QPointF(20, 48), QPointF(16, 32), QPointF(32, 36) }));
    } else if (name == QLatin1String("zoomin") || name == QLatin1String("zoomout") ||
               name == QLatin1String("fit") || name == QLatin1String("fit-view")) {
        p.setPen(QPen(ink, 5));
        if (name == QLatin1String("fit") || name == QLatin1String("fit-view")) {
            p.drawRect(10, 14, 44, 36);
            p.setPen(QPen(kAcc, 4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(20, 24, 44, 24);
            p.drawLine(20, 40, 44, 40);
        } else {
            p.drawEllipse(QRectF(12, 12, 30, 30));
            p.drawLine(40, 40, 54, 54);
            p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(20, 27, 34, 27);
            if (name == QLatin1String("zoomin")) p.drawLine(27, 20, 27, 34);
        }
    } else if (name == QLatin1String("lock") || name == QLatin1String("unlock")) {
        p.setPen(QPen(ink, 5));
        if (name == QLatin1String("lock")) {
            QPainterPath sh;
            sh.moveTo(22, 30);
            sh.lineTo(22, 22);
            sh.arcTo(QRectF(22, 10, 20, 24), 180, -180);
            sh.lineTo(42, 30);
            p.drawPath(sh);
        } else {
            QPainterPath sh;
            sh.moveTo(22, 30);
            sh.lineTo(22, 22);
            sh.arcTo(QRectF(22, 10, 20, 24), 180, -120);
            p.drawPath(sh);
        }
        p.setPen(QPen(ink, 3));
        p.setBrush(kWarn);
        p.drawRoundedRect(QRectF(16, 30, 32, 24), 4, 4);
    } else if (name == QLatin1String("save")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor("#9B8AFB"));
        p.drawRect(12, 12, 40, 40);
        p.setBrush(QColor("#55C2C3"));
        p.drawRect(20, 12, 24, 16);
        p.setBrush(kAcc);
        p.drawRect(18, 34, 28, 18);
    } else if (name == QLatin1String("open")) {
        p.setPen(QPen(ink, 3));
        p.setBrush(QColor("#F2C66D"));
        p.drawPolygon(QPolygonF({ QPointF(8, 50), QPointF(16, 26), QPointF(58, 26),
                                  QPointF(50, 50) }));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({ QPointF(10, 44), QPointF(10, 16), QPointF(26, 16),
                                   QPointF(31, 22), QPointF(50, 22), QPointF(50, 26) }));
    } else if (name == QLatin1String("new")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor("#55C2C3"));
        p.drawPolygon(QPolygonF({ QPointF(16, 8), QPointF(40, 8), QPointF(50, 20),
                                  QPointF(50, 56), QPointF(16, 56) }));
        p.setPen(QPen(kVeg, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(33, 28, 33, 46);
        p.drawLine(24, 37, 42, 37);
    } else if (name == QLatin1String("export")) {
        p.setPen(QPen(ink, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(32, 44, 32, 14);
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({ QPointF(32, 8), QPointF(44, 24), QPointF(20, 24) }));
        p.setPen(QPen(ink, 5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({ QPointF(14, 40), QPointF(14, 54), QPointF(50, 54),
                                   QPointF(50, 40) }));
    } else if (name == QLatin1String("palette")) {
        p.setPen(QPen(ink, 3));
        p.setBrush(QColor("#9B8AFB"));
        p.drawEllipse(QRectF(10, 12, 44, 40));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#9B8AFB"));
        p.drawEllipse(QPointF(40, 40), 7, 6);
        for (int i = 0; i < 4; ++i) {
            static const char* cs[] = { "#F28C8C", "#55C2C3", "#5DADE2", "#F2C66D" };
            p.setBrush(QColor(cs[i]));
            p.drawEllipse(QPointF(20 + i * 8, 22 + (i % 2) * 8), 4.5, 4.5);
        }
    } else if (name == QLatin1String("slope")) {
        p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(12, 48, 52, 48);
        p.drawLine(16, 42, 48, 34);
        p.drawLine(20, 34, 44, 20);
        p.setPen(QPen(kAcc, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(14, 54, 54, 54);
    } else if (name == QLatin1String("mask") || name == QLatin1String("mask-edit")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(QColor(255,255,255,35));
        p.drawRect(11, 11, 42, 42);
        p.setBrush(QColor(255,255,255,210));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(20, 20, 24, 24));
        if (name == QLatin1String("mask-edit")) {
            p.setPen(QPen(kAcc, 5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(38, 48, 53, 33);
            p.setBrush(kAcc); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(52,32),4,4);
        }
    } else if (name == QLatin1String("alpha-lock")) {
        p.setPen(QPen(ink, 4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(26, 10, 24, 24), 0, 180 * 16);
        p.drawRoundedRect(QRectF(24, 28, 28, 24), 4, 4);
        p.setPen(QPen(kAcc, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(12, 48, 20, 20);
        p.drawLine(16, 35, 24, 35);
    } else if (name == QLatin1String("layer-tile")) {
        p.setPen(QPen(ink, 3));
        p.drawRect(10,10,44,44);
        p.drawLine(32,10,32,54); p.drawLine(10,32,54,32);
        p.setBrush(QColor(93,173,226,80)); p.drawRect(11,11,20,20);
    } else if (name == QLatin1String("layer-paint")) {
        p.setPen(QPen(ink, 3)); p.drawRect(10,10,44,44);
        p.setPen(QPen(kAcc, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(18,44,42,20); p.drawLine(24,48,48,24);
    } else if (name == QLatin1String("effects")) {
        p.setPen(QPen(kWarn, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(32,10,32,54); p.drawLine(10,32,54,32);
        p.drawLine(17,17,47,47); p.drawLine(47,17,17,47);
    } else if (name == QLatin1String("layer-filters") || name == QLatin1String("mask-filters")) {
        p.setPen(QPen(ink, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(12,18,52,18); p.drawLine(12,32,52,32); p.drawLine(12,46,52,46);
        p.setBrush(kAcc); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(24,18),5,5); p.drawEllipse(QPointF(42,32),5,5); p.drawEllipse(QPointF(29,46),5,5);
        if (name == QLatin1String("mask-filters")) { p.setPen(QPen(kWarn,3)); p.setBrush(Qt::NoBrush); p.drawRect(7,8,50,48); }
    } else if (name == QLatin1String("filter-gaussian")) {
        p.setPen(Qt::NoPen); p.setBrush(QColor(255,255,255,45)); p.drawEllipse(QRectF(10,10,44,44));
        p.setBrush(QColor(255,255,255,90)); p.drawEllipse(QRectF(18,18,28,28));
        p.setBrush(kAcc); p.drawEllipse(QRectF(27,27,10,10));
    } else if (name == QLatin1String("filter-directional")) {
        p.setPen(QPen(ink,4,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(10,22,48,22); p.drawLine(16,32,54,32); p.drawLine(10,42,48,42);
        p.setBrush(kAcc); p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF({QPointF(48,16),QPointF(58,22),QPointF(48,28)}));
    } else if (name == QLatin1String("filter-noise")) {
        p.setPen(Qt::NoPen);
        for (int y=12;y<=52;y+=8) for(int x=12;x<=52;x+=8) {
            const int v=((x*17+y*31)/8)%3; p.setBrush(v==0?kAcc:(v==1?ink:QColor(255,255,255,90))); p.drawRect(x,y,4,4);
        }
    } else if (name == QLatin1String("filter-contact-shadow")) {
        p.setPen(QPen(ink,3)); p.setBrush(QColor(255,255,255,50)); p.drawRect(18,12,28,28);
        p.setPen(Qt::NoPen); p.setBrush(QColor(0,0,0,100)); p.drawEllipse(QRectF(14,38,36,12));
        p.setPen(QPen(kAcc,3)); p.drawLine(18,42,46,42);
    } else if (name == QLatin1String("eyedropper")) {
        p.setPen(QPen(ink, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(18,46,45,19); p.drawLine(38,14,50,26);
        p.setPen(QPen(kAcc, 4)); p.drawLine(15,49,25,49);
    } else if (name == QLatin1String("history")) {
        p.setPen(QPen(ink,4)); p.drawEllipse(QRectF(13,13,38,38));
        p.drawLine(32,20,32,33); p.drawLine(32,33,42,39);
    } else if (name == QLatin1String("map-move")) {
        p.setPen(QPen(ink,4,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(12,32,52,32); p.drawLine(32,12,32,52);
        p.drawLine(12,32,20,24); p.drawLine(12,32,20,40);
        p.drawLine(52,32,44,24); p.drawLine(52,32,44,40);
        p.drawLine(32,12,24,20); p.drawLine(32,12,40,20);
        p.drawLine(32,52,24,44); p.drawLine(32,52,40,44);
    } else if (name == QLatin1String("merge-down")) {
        p.setPen(QPen(ink,3)); p.setBrush(QColor(93,173,226,60));
        p.drawRect(14,12,36,14); p.drawRect(14,40,36,12);
        p.setPen(QPen(kAcc,5,Qt::SolidLine,Qt::RoundCap)); p.drawLine(32,26,32,39);
    } else {
        // fallback: quadrado neutro. A chave continua disponível no Editor de
        // Ícones e é marcada como pendente até o usuário fornecer um asset.
        p.setPen(QPen(ink, 4));
        p.drawRect(14, 14, 36, 36);
    }
}

struct IconSpec {
    QString name;
    QColor tint;
};

QHash<QString, QIcon>& cache()
{
    static QHash<QString, QIcon> c;
    return c;
}

QHash<quint64, IconSpec>& rememberedSpecs()
{
    // Mantemos também as chaves antigas depois de limpar o cache. Isso permite
    // reconhecer QIcons que já foram entregues a QAction/QToolButton e trocá-los
    // em tempo real quando as Preferências são confirmadas.
    static QHash<quint64, IconSpec> specs;
    return specs;
}

QSettings iconSettings()
{
    return QSettings(QStringLiteral("LudoEngine"), QStringLiteral("Ludo Engine"));
}

QString safeKey(QString key)
{
    for (int i = 0; i < key.size(); ++i) {
        const QChar ch = key.at(i);
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_')) key[i] = QLatin1Char('_');
    }
    return key;
}

QString customDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.ludo-engine");
    return QDir(base).filePath(QStringLiteral("icons"));
}

QString settingsKey(const QString& name)
{
    return QStringLiteral("editor/icons/custom/%1").arg(name);
}

QPixmap renderFile(const QString& filePath, int size)
{
    size = qMax(1, size);
    if (filePath.trimmed().isEmpty()) return {};

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return {};

    if (info.suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0 ||
        info.suffix().compare(QStringLiteral("svgz"), Qt::CaseInsensitive) == 0) {
        QSvgRenderer svg(filePath);
        if (!svg.isValid()) return {};
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QSizeF natural = svg.defaultSize().isValid() ? QSizeF(svg.defaultSize()) : QSizeF(size, size);
        QSizeF target = natural;
        target.scale(QSizeF(size, size), Qt::KeepAspectRatio);
        const QRectF rect((size - target.width()) / 2.0, (size - target.height()) / 2.0,
                          target.width(), target.height());
        svg.render(&painter, rect);
        return pm;
    }

    const QPixmap source(filePath);
    if (source.isNull()) return {};
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    const QPixmap scaled = source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&pm);
    painter.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    return pm;
}

QPixmap renderDefault(const QString& name, int size, const QColor& tint)
{
    QPixmap pm(qMax(1, size), qMax(1, size));
    pm.fill(Qt::transparent);

    // Os PNGs enviados pelo autor têm prioridade. SVG e QPainter permanecem
    // como fallback para itens ainda sem bitmap dedicado.
    if (!tint.isValid()) {
        const QPixmap source(QStringLiteral(":/icons/%1.png").arg(name));
        if (!source.isNull()) {
            const QPixmap scaled = source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter painter(&pm);
            painter.drawPixmap((size-scaled.width())/2, (size-scaled.height())/2, scaled);
            return pm;
        }
        const QString caminho = QStringLiteral(":/icons/%1.svg").arg(name);
        QSvgRenderer svg;
        if (QFile::exists(caminho)) svg.load(caminho);
        if (svg.isValid()) {
            QPainter painter(&pm);
            painter.setRenderHint(QPainter::Antialiasing, true);
            svg.render(&painter, QRectF(0, 0, size, size));
            return pm;
        }
    }

    QPainter painter(&pm);
    painter.scale(size / 64.0, size / 64.0);
    paintIcon(painter, name, tint.isValid() ? tint : kInk);
    return pm;
}

} // namespace

QVector<Descriptor> catalog()
{
    // Ordem pensada para o Editor de Ícones. Também inclui ações que hoje
    // dependem de fallback vetorial/genérico; elas aparecem como "Criar ícone".
    return {
        {QStringLiteral("new"),               QStringLiteral("Novo projeto")},
        {QStringLiteral("open"),              QStringLiteral("Abrir projeto")},
        {QStringLiteral("save"),              QStringLiteral("Salvar mapa")},
        {QStringLiteral("folder"),            QStringLiteral("Sincronizar RPG Maker")},
        {QStringLiteral("export"),            QStringLiteral("Exportar / Publicar")},
        {QStringLiteral("tileset-manager"),   QStringLiteral("Gerenciador de Tilesets")},
        {QStringLiteral("brush"),             QStringLiteral("Pincel genérico (legado)")},
        {QStringLiteral("tile-brush"),        QStringLiteral("Pincel de tiles")},
        {QStringLiteral("paint-brush"),       QStringLiteral("Pincel de pintura")},
        {QStringLiteral("tile-brush-settings"),QStringLiteral("Formato do pincel de tiles")},
        {QStringLiteral("paint-brush-settings"),QStringLiteral("Configurar pincel de pintura")},
        {QStringLiteral("eraser"),            QStringLiteral("Borracha")},
        {QStringLiteral("fill"),              QStringLiteral("Balde")},
        {QStringLiteral("rect"),              QStringLiteral("Retângulo")},
        {QStringLiteral("circle"),            QStringLiteral("Círculo / Elipse")},
        {QStringLiteral("line"),              QStringLiteral("Linha / Caminho")},
        {QStringLiteral("terrain"),           QStringLiteral("Conexões do Autotile")},
        {QStringLiteral("select"),            QStringLiteral("Seleção")},
        {QStringLiteral("object"),            QStringLiteral("Objeto")},
        {QStringLiteral("slope"),             QStringLiteral("Inclinação")},
        {QStringLiteral("eyedropper"),        QStringLiteral("Conta-gotas")},
        {QStringLiteral("effects"),           QStringLiteral("Efeitos visuais")},
        {QStringLiteral("layer-filters"),     QStringLiteral("Filtros da camada")},
        {QStringLiteral("mask-filters"),      QStringLiteral("Filtros da máscara")},
        {QStringLiteral("filter-gaussian"),   QStringLiteral("Desfoque suave")},
        {QStringLiteral("filter-directional"),QStringLiteral("Desfoque em direção")},
        {QStringLiteral("filter-noise"),      QStringLiteral("Granulação")},
        {QStringLiteral("filter-contact-shadow"),QStringLiteral("Sombra de contato")},
        {QStringLiteral("undo"),              QStringLiteral("Desfazer")},
        {QStringLiteral("redo"),              QStringLiteral("Refazer")},
        {QStringLiteral("history"),           QStringLiteral("Histórico")},
        {QStringLiteral("filled"),            QStringLiteral("Forma preenchida")},
        {QStringLiteral("ghost"),             QStringLiteral("Prévia ligada")},
        {QStringLiteral("ghost-off"),         QStringLiteral("Prévia desligada")},
        {QStringLiteral("ontop"),             QStringLiteral("Empilhar tiles")},
        {QStringLiteral("random"),            QStringLiteral("Tiles aleatórios")},
        {QStringLiteral("snap"),              QStringLiteral("Encaixar na grade")},
        {QStringLiteral("grid"),              QStringLiteral("Grade")},
        {QStringLiteral("multigrid"),         QStringLiteral("Multigrade")},
        {QStringLiteral("focus"),             QStringLiteral("Focar camada")},
        {QStringLiteral("palette"),           QStringLiteral("Paleta genérica (legado)")},
        {QStringLiteral("region"),            QStringLiteral("Regiões")},
        {QStringLiteral("color-palette"),     QStringLiteral("Escolher cor")},
        {QStringLiteral("collision"),         QStringLiteral("Colisão")},
        {QStringLiteral("star"),              QStringLiteral("Prioridade")},
        {QStringLiteral("layer-add"),         QStringLiteral("Adicionar camada")},
        {QStringLiteral("layer-remove"),      QStringLiteral("Excluir camada")},
        {QStringLiteral("duplicate"),         QStringLiteral("Duplicar camada")},
        {QStringLiteral("up"),                QStringLiteral("Mover camada para cima")},
        {QStringLiteral("down"),              QStringLiteral("Mover camada para baixo")},
        {QStringLiteral("outdent"),           QStringLiteral("Mover para fora da pasta")},
        {QStringLiteral("group"),             QStringLiteral("Grupo de camadas")},
        {QStringLiteral("layer-tile"),        QStringLiteral("Camada de tiles")},
        {QStringLiteral("objectlayer"),       QStringLiteral("Camada de objetos")},
        {QStringLiteral("image"),             QStringLiteral("Imagem genérica (legado)")},
        {QStringLiteral("layer-image"),       QStringLiteral("Camada de imagem")},
        {QStringLiteral("layer-reference"),   QStringLiteral("Imagem de referência")},
        {QStringLiteral("tileset-source-add"),QStringLiteral("Adicionar imagem ao Tileset")},
        {QStringLiteral("layer-paint"),       QStringLiteral("Camada de pintura")},
        {QStringLiteral("mask"),              QStringLiteral("Máscara da camada")},
        {QStringLiteral("mask-edit"),         QStringLiteral("Máscara da camada — editando")},
        {QStringLiteral("alpha-lock"),        QStringLiteral("Proteger áreas vazias")},
        {QStringLiteral("merge-down"),        QStringLiteral("Mesclar com camada inferior")},
        {QStringLiteral("tileset"),           QStringLiteral("Tileset")},
        {QStringLiteral("tileset-new"),       QStringLiteral("Novo Tileset")},
        {QStringLiteral("import"),            QStringLiteral("Importar")},
        {QStringLiteral("convert"),           QStringLiteral("Converter / Autotile")},
        {QStringLiteral("refresh"),           QStringLiteral("Atualizar")},
        {QStringLiteral("resize"),            QStringLiteral("Redimensionar mapa")},
        {QStringLiteral("map-move"),          QStringLiteral("Mover conteúdo do mapa")},
        {QStringLiteral("map-rename"),        QStringLiteral("Renomear mapa")},
        {QStringLiteral("zoomin"),            QStringLiteral("Zoom +")},
        {QStringLiteral("zoomout"),           QStringLiteral("Zoom -")},
        {QStringLiteral("one-to-one"),        QStringLiteral("Zoom 1:1")},
        {QStringLiteral("fit-view"),          QStringLiteral("Ajustar mapa à janela")},
        {QStringLiteral("lock"),              QStringLiteral("Bloquear")},
        {QStringLiteral("unlock"),            QStringLiteral("Desbloquear")},
        {QStringLiteral("preferences"),       QStringLiteral("Preferências")},
        {QStringLiteral("modules"),           QStringLiteral("Módulos do Editor")},
        {QStringLiteral("icon-preset-import"),QStringLiteral("Importar preset de ícones")},
        {QStringLiteral("icon-preset-export"),QStringLiteral("Exportar preset de ícones")},
        {QStringLiteral("team-login"),        QStringLiteral("Equipe — conectar")},
        {QStringLiteral("team-offline"),      QStringLiteral("Equipe — desconectado")},
        {QStringLiteral("team-online"),       QStringLiteral("Equipe — conectado")},
        {QStringLiteral("team-reconnecting"), QStringLiteral("Equipe — reconectando")},
        {QStringLiteral("team-slow"),         QStringLiteral("Equipe — conexão lenta")},
        {QStringLiteral("add"),               QStringLiteral("Adicionar")},
        {QStringLiteral("remove"),            QStringLiteral("Remover")}
    };
}

bool hasDedicatedArtwork(const QString& name)
{
    return QFile::exists(QStringLiteral(":/icons/%1.png").arg(name)) ||
           QFile::exists(QStringLiteral(":/icons/%1.svg").arg(name)) ||
           QFile::exists(QStringLiteral(":/icons/%1.svgz").arg(name));
}

QStringList missingArtworkKeys()
{
    QStringList out;
    for (const Descriptor& item : catalog())
        if (!hasDedicatedArtwork(item.key)) out.push_back(item.key);
    return out;
}

QString customIconPath(const QString& name)
{
    QSettings s = iconSettings();
    const QString path = s.value(settingsKey(name)).toString();
    return QFileInfo::exists(path) ? path : QString();
}

QPixmap filePixmap(const QString& filePath, int size)
{
    return renderFile(filePath, size);
}

QPixmap defaultPixmap(const QString& name, int size, const QColor& tint)
{
    return renderDefault(name, qMax(1, size), tint);
}

QPixmap pixmap(const QString& name, int size, const QColor& tint)
{
    size = qMax(1, size);
    if (!tint.isValid()) {
        const QString custom = customIconPath(name);
        if (!custom.isEmpty()) {
            const QPixmap pm = renderFile(custom, size);
            if (!pm.isNull()) return pm;
        }
    }
    return renderDefault(name, size, tint);
}

QIcon get(const QString& name, const QColor& tint)
{
    // O caminho customizado participa da chave. Isso também protege contra uma
    // alteração externa do QSettings antes de refreshApplicationIcons().
    const QString custom = tint.isValid() ? QString() : customIconPath(name);
    const QString key = name + QLatin1Char('|') + (tint.isValid() ? tint.name() : QString()) +
                        QLatin1Char('|') + custom;
    auto it = cache().constFind(key);
    if (it != cache().constEnd()) return it.value();

    QIcon icon;
    for (int s : { 16, 20, 24, 28, 32, 40, 48, 64 }) icon.addPixmap(pixmap(name, s, tint));
    cache().insert(key, icon);
    rememberedSpecs().insert(icon.cacheKey(), IconSpec{name, tint});
    return icon;
}

bool setCustomIcon(const QString& name, const QString& sourceFile, QString* error)
{
    const QFileInfo sourceInfo(sourceFile);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        if (error) *error = QStringLiteral("Arquivo de ícone não encontrado: %1").arg(sourceFile);
        return false;
    }

    const bool svgFile = sourceInfo.suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0 ||
                         sourceInfo.suffix().compare(QStringLiteral("svgz"), Qt::CaseInsensitive) == 0;
    bool valid = false;
    if (svgFile) valid = QSvgRenderer(sourceFile).isValid();
    else valid = !QPixmap(sourceFile).isNull();
    if (!valid) {
        if (error) *error = QStringLiteral("O arquivo não pôde ser lido como imagem: %1").arg(sourceFile);
        return false;
    }

    QDir dir(customDirectory());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        if (error) *error = QStringLiteral("Não foi possível criar a pasta de ícones: %1").arg(dir.absolutePath());
        return false;
    }

    QString ext = sourceInfo.suffix().toLower();
    if (ext.isEmpty()) ext = QStringLiteral("png");
    const QString destination = dir.filePath(safeKey(name) + QLatin1Char('.') + ext);

    const QString old = customIconPath(name);
    const QString sourceCanonical = sourceInfo.canonicalFilePath();
    const QString destCanonical = QFileInfo(destination).canonicalFilePath();
    if (sourceCanonical.isEmpty() || destCanonical.isEmpty() || sourceCanonical != destCanonical) {
        if (QFile::exists(destination) && !QFile::remove(destination)) {
            if (error) *error = QStringLiteral("Não foi possível substituir o ícone existente: %1").arg(destination);
            return false;
        }
        if (!QFile::copy(sourceFile, destination)) {
            if (error) *error = QStringLiteral("Não foi possível copiar o ícone para: %1").arg(destination);
            return false;
        }
    }

    if (!old.isEmpty() && old != destination) {
        const QString root = QDir(customDirectory()).absolutePath() + QDir::separator();
        const QString absoluteOld = QFileInfo(old).absoluteFilePath();
        if (absoluteOld.startsWith(root, Qt::CaseInsensitive)) QFile::remove(absoluteOld);
    }

    QSettings s = iconSettings();
    s.setValue(settingsKey(name), destination);
    cache().clear();
    return true;
}

void clearCustomIcon(const QString& name)
{
    QSettings s = iconSettings();
    const QString path = s.value(settingsKey(name)).toString();
    s.remove(settingsKey(name));
    if (!path.isEmpty()) {
        const QString root = QDir(customDirectory()).absolutePath() + QDir::separator();
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (absolute.startsWith(root, Qt::CaseInsensitive)) QFile::remove(absolute);
    }
    cache().clear();
}

void clearAllCustomIcons()
{
    for (const Descriptor& item : catalog()) clearCustomIcon(item.key);
}

bool exportPreset(const QString& filePath, QString* error)
{
    QString path = filePath.trimmed();
    if (path.isEmpty()) { if (error) *error = QStringLiteral("Destino vazio."); return false; }
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".ludoicons");

    QJsonArray entries;
    for (const Descriptor& descriptor : catalog()) {
        const QString iconPath = customIconPath(descriptor.key);
        if (iconPath.isEmpty()) continue;
        QFile file(iconPath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Não foi possível ler o ícone %1: %2").arg(descriptor.key, iconPath);
            return false;
        }
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("key"), descriptor.key);
        entry.insert(QStringLiteral("extension"), QFileInfo(iconPath).suffix().toLower());
        entry.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
        entries.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("LUDO Icon Preset"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("toolbarIconSize"), toolbarIconSize());
    root.insert(QStringLiteral("icons"), entries);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Não foi possível criar o preset: %1").arg(path);
        return false;
    }
    if (out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        if (error) *error = QStringLiteral("Falha ao gravar o preset: %1").arg(path);
        return false;
    }
    return true;
}

bool importPreset(const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Não foi possível abrir o preset: %1").arg(filePath);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Preset inválido: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("LUDO Icon Preset")) {
        if (error) *error = QStringLiteral("O arquivo não é um preset de ícones do LUDO.");
        return false;
    }

    struct PendingIcon { QString key; QString ext; QByteArray bytes; };
    QVector<PendingIcon> pending;
    const QJsonArray entries = root.value(QStringLiteral("icons")).toArray();
    pending.reserve(entries.size());
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString key = entry.value(QStringLiteral("key")).toString().trimmed();
        QString ext = entry.value(QStringLiteral("extension")).toString().toLower().trimmed();
        const QByteArray bytes = QByteArray::fromBase64(entry.value(QStringLiteral("data")).toString().toLatin1());
        if (key.isEmpty() || bytes.isEmpty()) continue;
        if (ext.isEmpty()) ext = QStringLiteral("png");
        bool valid = false;
        if (ext == QLatin1String("svg") || ext == QLatin1String("svgz")) {
            QSvgRenderer svg;
            svg.load(bytes);
            valid = svg.isValid();
        } else {
            valid = !QImage::fromData(bytes).isNull();
        }
        if (!valid) {
            if (error) *error = QStringLiteral("O preset contém um ícone inválido: %1").arg(key);
            return false;
        }
        pending.push_back({key, ext, bytes});
    }

    QDir dir(customDirectory());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        if (error) *error = QStringLiteral("Não foi possível criar a pasta de ícones: %1").arg(dir.absolutePath());
        return false;
    }

    // Um preset representa o conjunto completo. Removemos customizações atuais
    // somente depois que todo o arquivo foi validado.
    clearAllCustomIcons();
    QSettings settings = iconSettings();
    for (const PendingIcon& item : pending) {
        const QString destination = dir.filePath(safeKey(item.key) + QLatin1Char('.') + item.ext);
        QFile::remove(destination);
        QFile out(destination);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(item.bytes) != item.bytes.size()) {
            if (error) *error = QStringLiteral("Não foi possível instalar o ícone %1.").arg(item.key);
            return false;
        }
        settings.setValue(settingsKey(item.key), destination);
    }
    setToolbarIconSize(qBound(16, root.value(QStringLiteral("toolbarIconSize")).toInt(toolbarIconSize()), 64));
    cache().clear();
    return true;
}


int toolbarIconSize()
{
    QSettings s = iconSettings();
    return qBound(16, s.value(QStringLiteral("editor/icons/toolbarSize"), 28).toInt(), 64);
}

void setToolbarIconSize(int size)
{
    QSettings s = iconSettings();
    s.setValue(QStringLiteral("editor/icons/toolbarSize"), qBound(16, size, 64));
}

void refreshApplicationIcons(QWidget* root)
{
    // Copiamos o mapa antes de criar os novos QIcons. As ações existentes ainda
    // carregam cacheKeys antigos; com isso sabemos qual nome deve ser reaplicado.
    const QHash<quint64, IconSpec> oldSpecs = rememberedSpecs();
    cache().clear();

    QList<QWidget*> widgets;
    if (root) {
        widgets.push_back(root);
        widgets.append(root->findChildren<QWidget*>());
    } else {
        widgets = QApplication::allWidgets();
    }

    QSet<QAction*> actions;
    for (QWidget* widget : widgets) {
        if (!widget) continue;
        for (QAction* action : widget->actions()) actions.insert(action);
        const auto childActions = widget->findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly);
        for (QAction* action : childActions) actions.insert(action);

        if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            const auto spec = oldSpecs.constFind(button->icon().cacheKey());
            if (spec != oldSpecs.constEnd()) button->setIcon(get(spec->name, spec->tint));
        }
    }

    if (root) {
        const auto nestedActions = root->findChildren<QAction*>();
        for (QAction* action : nestedActions) actions.insert(action);
    }

    for (QAction* action : actions) {
        if (!action || action->icon().isNull()) continue;
        const auto spec = oldSpecs.constFind(action->icon().cacheKey());
        if (spec != oldSpecs.constEnd()) action->setIcon(get(spec->name, spec->tint));
    }

    const int preferred = toolbarIconSize();
    for (QWidget* widget : widgets) {
        auto* bar = qobject_cast<QToolBar*>(widget);
        if (!bar) continue;
        bar->setIconSize(QSize(preferred, preferred));
        const auto buttons = bar->findChildren<QToolButton*>();
        for (QToolButton* button : buttons) button->setIconSize(QSize(preferred, preferred));
    }
}

}} // namespace ui::icons
