#pragma once
#include "Types.h"
#include <cstddef>

namespace ludo::core {

enum class PixelFormat { Rgba8 };

class ImageBuffer {
public:
    ImageBuffer() = default;
    explicit ImageBuffer(SizeI size, PixelFormat format = PixelFormat::Rgba8);

    SizeI size() const noexcept { return size_; }
    PixelFormat format() const noexcept { return format_; }
    bool empty() const noexcept { return pixels_.empty(); }
    std::size_t stride() const noexcept { return std::size_t(size_.width) * 4u; }
    Bytes& pixels() noexcept { return pixels_; }
    const Bytes& pixels() const noexcept { return pixels_; }

private:
    SizeI size_{};
    PixelFormat format_ = PixelFormat::Rgba8;
    Bytes pixels_;
};

} // namespace ludo::core
