#include "EditorUiPrimitives.h"

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

namespace ui::primitives {
namespace {
QFrame* banner(const QString& text, const char* role, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("uiRole", QString::fromLatin1(role));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(10, 8, 10, 8);
    auto* label = hintLabel(text, frame);
    label->setWordWrap(true);
    layout->addWidget(label);
    return frame;
}
}

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("uiRole", QStringLiteral("hint"));
    label->setWordWrap(true);
    return label;
}

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("uiRole", QStringLiteral("sectionTitle"));
    return label;
}

QFrame* infoBanner(const QString& text, QWidget* parent)
{
    return banner(text, "infoBanner", parent);
}

QFrame* warningBanner(const QString& text, QWidget* parent)
{
    return banner(text, "warningBanner", parent);
}

} // namespace ui::primitives
