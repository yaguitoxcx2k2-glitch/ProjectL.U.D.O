#pragma once

#include <memory>
#include <QString>

namespace core {
class Editor;

/// Runtime-only project boundary. It owns an executable snapshot and keeps the
/// legacy Editor implementation private while runtime APIs migrate away from it.
class RuntimeProject {
public:
    RuntimeProject();
    ~RuntimeProject();
    RuntimeProject(RuntimeProject&&) noexcept;
    RuntimeProject& operator=(RuntimeProject&&) noexcept;
    RuntimeProject(const RuntimeProject&) = delete;
    RuntimeProject& operator=(const RuntimeProject&) = delete;

    static std::unique_ptr<RuntimeProject> fromEditor(const Editor& source, QString* error = nullptr);
    /// Takes ownership of an already validated executable snapshot.
    static std::unique_ptr<RuntimeProject> adoptSnapshot(std::unique_ptr<Editor> snapshot);
    bool refreshFromEditor(const Editor& source, QString* error = nullptr);

    /// Transitional bridge. Only runtime implementation/adapters may use this;
    /// UI/player callers should depend on RuntimeProject itself.
    Editor& legacyModel();
    const Editor& legacyModel() const;

    QString projectId() const;
    QString projectName() const;
    QString projectRoot() const;
    QString currentMapId() const;

private:
    explicit RuntimeProject(std::unique_ptr<Editor> model);
    std::unique_ptr<Editor> m_model;
};
}
