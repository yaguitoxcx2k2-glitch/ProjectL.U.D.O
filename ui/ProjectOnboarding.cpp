#include "ProjectOnboarding.h"

#include "core/Editor.h"
#include "core/ProjectHealth.h"

#include <QFileInfo>
#include <QObject>
#include <utility>

namespace ui {
namespace {
void append(OnboardingSnapshot& out, QString id, QString title, QString description, bool complete)
{
    out.steps.push_back({std::move(id), std::move(title), std::move(description), complete});
    ++out.totalCount;
    if (complete) ++out.completeCount;
}
}

OnboardingSnapshot ProjectOnboarding::inspect(const core::Editor& editor)
{
    OnboardingSnapshot out;
    bool hasEvent = false;
    for (const core::MapDoc& doc : editor.docs) {
        if (!doc.events.isEmpty()) { hasEvent = true; break; }
    }
    const bool hasStartMap = editor.mapIndexById(editor.startMapId) >= 0;
    const core::ProjectHealthSnapshot health = core::ProjectHealth::inspect(editor);

    append(out, QStringLiteral("saved"), QObject::tr("Salvar o projeto"),
           QObject::tr("Salve o projeto pela primeira vez para ativar a recuperação automática e permitir exportações."),
           !editor.projectPath.isEmpty() && QFileInfo::exists(editor.projectPath));
    append(out, QStringLiteral("map"), QObject::tr("Preparar um mapa"),
           QObject::tr("Tenha pelo menos um mapa no projeto."), !editor.docs.isEmpty());
    append(out, QStringLiteral("start"), QObject::tr("Definir início do jogo"),
           QObject::tr("Escolha o mapa inicial nas Configurações do Jogo."), hasStartMap);
    append(out, QStringLiteral("event"), QObject::tr("Criar o primeiro evento"),
           QObject::tr("Adicione um NPC, porta, diálogo ou outro evento a um mapa."), hasEvent);
    append(out, QStringLiteral("health"), QObject::tr("Validar o projeto"),
           QObject::tr("Corrija os problemas marcados como erro em Saúde do Projeto antes de publicar."),
           !health.validation.hasErrors());
    return out;
}

QString ProjectOnboarding::summaryText(const OnboardingSnapshot& snapshot)
{
    return snapshot.complete()
        ? QObject::tr("Tudo pronto! Você concluiu os primeiros passos do projeto.")
        : QObject::tr("Primeiros passos: %1 de %2 concluídos.").arg(snapshot.completeCount).arg(snapshot.totalCount);
}

} // namespace ui
