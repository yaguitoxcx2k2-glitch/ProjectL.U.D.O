#pragma once
#include "core/pure/Core.h"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVariant>

namespace ludo::platform::qt {

QString toQString(core::TextView text);
core::Text toText(const QString& text);
QPoint toQPoint(core::PointI point);
core::PointI toPointI(const QPoint& point);
QSize toQSize(core::SizeI size);
core::SizeI toSizeI(const QSize& size);
QRect toQRect(core::RectI rect);
core::RectI toRectI(const QRect& rect);
QColor toQColor(core::ColorRgba color);
core::ColorRgba toColorRgba(const QColor& color);
QVariant toQVariant(const core::Value& value);
core::Value toValue(const QVariant& value);
QJsonValue toQJsonValue(const core::JsonValue& value);
core::JsonValue toJsonValue(const QJsonValue& value);
QImage toQImage(const core::ImageBuffer& image);
core::ImageBuffer toImageBuffer(const QImage& image);

} // namespace ludo::platform::qt
