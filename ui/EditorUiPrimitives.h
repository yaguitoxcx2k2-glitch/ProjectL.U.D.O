#pragma once

class QLabel;
class QFrame;
class QWidget;
class QVBoxLayout;
class QString;

namespace ui::primitives {

QLabel* hintLabel(const QString& text, QWidget* parent = nullptr);
QLabel* sectionTitle(const QString& text, QWidget* parent = nullptr);
QFrame* infoBanner(const QString& text, QWidget* parent = nullptr);
QFrame* warningBanner(const QString& text, QWidget* parent = nullptr);

} // namespace ui::primitives
