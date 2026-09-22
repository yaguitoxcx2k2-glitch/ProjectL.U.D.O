#pragma once
#include "Types.h"
#include <memory>

namespace ludo::core {

struct FileReadResult { bool ok = false; Bytes bytes; Text error; };
struct FileWriteResult { bool ok = false; Text error; };

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual bool exists(TextView path) const = 0;
    virtual bool isDirectory(TextView path) const = 0;
    virtual FileReadResult readAll(TextView path) const = 0;
    virtual FileWriteResult writeAll(TextView path, const Bytes& bytes) = 0;
    virtual FileWriteResult createDirectories(TextView path) = 0;
};

class NativeFileSystem final : public IFileSystem {
public:
    bool exists(TextView path) const override;
    bool isDirectory(TextView path) const override;
    FileReadResult readAll(TextView path) const override;
    FileWriteResult writeAll(TextView path, const Bytes& bytes) override;
    FileWriteResult createDirectories(TextView path) override;
};

} // namespace ludo::core
