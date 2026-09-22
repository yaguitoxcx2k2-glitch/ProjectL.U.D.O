#include "RuntimeProject.h"
#include "Editor.h"
#include "RuntimeProjectSnapshot.h"
#include <utility>

namespace core {
RuntimeProject::RuntimeProject() = default;
RuntimeProject::~RuntimeProject() = default;
RuntimeProject::RuntimeProject(RuntimeProject&&) noexcept = default;
RuntimeProject& RuntimeProject::operator=(RuntimeProject&&) noexcept = default;
RuntimeProject::RuntimeProject(std::unique_ptr<Editor> model) : m_model(std::move(model)) {}

std::unique_ptr<RuntimeProject> RuntimeProject::fromEditor(const Editor& source, QString* error)
{
    auto snapshot = makeRuntimeEditorSnapshot(source, error);
    if (!snapshot) return {};
    // makeRuntimeEditorSnapshot limpa caches por segurança ao criar uma nova
    // geração. Porém F5/F6/Player podem estar clonando um snapshot já
    // preparado pelo RuntimePreloader dentro da MESMA geração executável.
    // Nesse caso o cache é deliberadamente transferido para não perder o
    // planejamento/aquecimento antes de construir o RhiGameWindow.
    if (source.runtimePreloadCache())
        snapshot->setRuntimePreloadCache(source.runtimePreloadCache());
    return std::unique_ptr<RuntimeProject>(new RuntimeProject(std::move(snapshot)));
}

std::unique_ptr<RuntimeProject> RuntimeProject::adoptSnapshot(std::unique_ptr<Editor> snapshot)
{
    if (!snapshot) return {};
    return std::unique_ptr<RuntimeProject>(new RuntimeProject(std::move(snapshot)));
}

bool RuntimeProject::refreshFromEditor(const Editor& source, QString* error)
{
    if (!m_model) {
        auto fresh = fromEditor(source, error);
        if (!fresh) return false;
        *this = std::move(*fresh);
        return true;
    }
    return refreshRuntimeEditorSnapshot(*m_model, source, error);
}

Editor& RuntimeProject::legacyModel() { return *m_model; }
const Editor& RuntimeProject::legacyModel() const { return *m_model; }
QString RuntimeProject::projectId() const { return m_model ? m_model->projectId : QString(); }
QString RuntimeProject::projectName() const { return m_model ? m_model->projectName : QString(); }
QString RuntimeProject::projectRoot() const { return m_model ? m_model->projectRoot() : QString(); }
QString RuntimeProject::currentMapId() const { return (m_model && m_model->doc()) ? m_model->doc()->id : QString(); }
}
