#pragma once

#include "core/ProjectReferenceIndex.h"

#include <QVector>

namespace ui {

/// Histórico de navegação da sessão (estilo voltar/avançar de IDE). Não é
/// persistido no projeto nem substitui recentes/favoritos.
class NavigationTrail final
{
public:
    void clear();
    void push(const core::ProjectReferenceLocation& location);
    bool canBack() const;
    bool canForward() const;
    core::ProjectReferenceLocation back();
    core::ProjectReferenceLocation forward();
    int size() const { return m_entries.size(); }

private:
    static bool same(const core::ProjectReferenceLocation& a,
                     const core::ProjectReferenceLocation& b);
    QVector<core::ProjectReferenceLocation> m_entries;
    int m_index = -1;
};

} // namespace ui
