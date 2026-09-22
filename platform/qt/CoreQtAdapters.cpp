#include "CoreQtAdapters.h"
#include <QMetaType>
#include <cstring>

namespace ludo::platform::qt {

QString toQString(core::TextView text) { return QString::fromUtf8(text.data(), qsizetype(text.size())); }
core::Text toText(const QString& text) { const QByteArray utf8 = text.toUtf8(); return core::Text(utf8.constData(), std::size_t(utf8.size())); }
QPoint toQPoint(core::PointI p) { return {p.x, p.y}; }
core::PointI toPointI(const QPoint& p) { return {p.x(), p.y()}; }
QSize toQSize(core::SizeI s) { return {s.width, s.height}; }
core::SizeI toSizeI(const QSize& s) { return {s.width(), s.height()}; }
QRect toQRect(core::RectI r) { return {r.x, r.y, r.width, r.height}; }
core::RectI toRectI(const QRect& r) { return {r.x(), r.y(), r.width(), r.height()}; }
QColor toQColor(core::ColorRgba c) { return {c.r, c.g, c.b, c.a}; }
core::ColorRgba toColorRgba(const QColor& c) { return {std::uint8_t(c.red()), std::uint8_t(c.green()), std::uint8_t(c.blue()), std::uint8_t(c.alpha())}; }

QVariant toQVariant(const core::Value& value) {
    const auto& storage = value.storage();
    if (std::holds_alternative<std::monostate>(storage)) return {};
    if (const auto* v = std::get_if<bool>(&storage)) return *v;
    if (const auto* v = std::get_if<std::int64_t>(&storage)) return QVariant::fromValue<qlonglong>(*v);
    if (const auto* v = std::get_if<double>(&storage)) return *v;
    if (const auto* v = std::get_if<core::Text>(&storage)) return toQString(*v);
    return {};
}

core::Value toValue(const QVariant& value) {
    if (!value.isValid() || value.isNull()) return {};
    const int type = value.metaType().id();
    if (type == QMetaType::Bool) return core::Value(value.toBool());
    if (type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt || type == QMetaType::ULongLong)
        return core::Value(std::int64_t(value.toLongLong()));
    if (type == QMetaType::Double || type == QMetaType::Float) return core::Value(value.toDouble());
    return core::Value(toText(value.toString()));
}

QJsonValue toQJsonValue(const core::JsonValue& value) {
    switch (value.kind()) {
    case core::JsonValue::Kind::Null: return {};
    case core::JsonValue::Kind::Boolean: return value.asBool();
    case core::JsonValue::Kind::Number: return value.asNumber();
    case core::JsonValue::Kind::String: return toQString(value.asString());
    case core::JsonValue::Kind::Array: { QJsonArray out; for (const auto& v : value.asArray()) out.push_back(toQJsonValue(v)); return out; }
    case core::JsonValue::Kind::Object: { QJsonObject out; for (const auto& [k,v] : value.asObject()) out.insert(toQString(k), toQJsonValue(v)); return out; }
    }
    return {};
}

core::JsonValue toJsonValue(const QJsonValue& value) {
    if (value.isNull() || value.isUndefined()) return {};
    if (value.isBool()) return core::JsonValue(value.toBool());
    if (value.isDouble()) return core::JsonValue(value.toDouble());
    if (value.isString()) return core::JsonValue(toText(value.toString()));
    if (value.isArray()) { core::JsonValue::Array out; const auto array=value.toArray(); out.reserve(std::size_t(array.size())); for (const auto& v:array) out.push_back(toJsonValue(v)); return core::JsonValue(std::move(out)); }
    core::JsonValue::Object out; const auto object=value.toObject(); for (auto it=object.begin(); it!=object.end(); ++it) out.emplace(toText(it.key()), toJsonValue(it.value())); return core::JsonValue(std::move(out));
}

QImage toQImage(const core::ImageBuffer& image) {
    if (image.empty()) return {};
    QImage out(image.size().width, image.size().height, QImage::Format_RGBA8888);
    for (int y=0; y<image.size().height; ++y)
        std::memcpy(out.scanLine(y), image.pixels().data()+std::size_t(y)*image.stride(), image.stride());
    return out;
}

core::ImageBuffer toImageBuffer(const QImage& source) {
    const QImage image=source.convertToFormat(QImage::Format_RGBA8888);
    core::ImageBuffer out({image.width(), image.height()});
    for (int y=0; y<image.height(); ++y)
        std::memcpy(out.pixels().data()+std::size_t(y)*out.stride(), image.constScanLine(y), out.stride());
    return out;
}

} // namespace ludo::platform::qt
