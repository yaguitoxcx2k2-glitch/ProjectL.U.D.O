#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QVector>

namespace game {

struct SpeechBubble {
    QString id;
    QString target=QStringLiteral("player");
    QString text;
    QString speaker;
    double duration=3.0;
    double remaining=3.0;
    int maxWidth=280;
    QColor bgColor=QColor(12,16,28,230);
    QColor textColor=Qt::white;
    int fontSize=18;
    QString tailDirection=QStringLiteral("down");
    int offsetX=0,offsetY=-16;
    bool typewriter=false;
    double charsPerSecond=36.0;
    int revealed=0;
    double revealAccumulator=0.0;
    double opacity=0.0;
    bool notification=false;
    QString screenPosition=QStringLiteral("top-right");
};

class SpeechBubbleManager {
public:
    void setFont(const QFont& font){m_font=font;}
    const QFont& font() const{return m_font;}
    QString show(SpeechBubble bubble);
    QString showNotification(SpeechBubble notification);
    void hide(const QString& target);
    void hideAll();
    void hideNotification();
    void update(double dt);
    const QVector<SpeechBubble>& active() const{return m_active;}
    bool empty() const{return m_active.isEmpty();}
private:
    QFont m_font;
    QVector<SpeechBubble> m_active;
};

} // namespace game
