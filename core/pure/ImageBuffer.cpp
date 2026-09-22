#include "ImageBuffer.h"
#include <stdexcept>
namespace ludo::core {
ImageBuffer::ImageBuffer(SizeI size, PixelFormat format) : size_(size), format_(format) {
    if (size.width < 0 || size.height < 0) throw std::invalid_argument("negative image size");
    pixels_.resize(std::size_t(size.width) * std::size_t(size.height) * 4u);
}
} // namespace ludo::core
