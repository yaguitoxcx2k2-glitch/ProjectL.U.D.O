#pragma once

#include <QString>
#include <QVector>

namespace core { class Editor; }

namespace ui {

struct OnboardingStep {
    QString id;
    QString title;
    QString description;
    bool complete = false;
};

struct OnboardingSnapshot {
    QVector<OnboardingStep> steps;
    int completeCount = 0;
    int totalCount = 0;

    bool complete() const { return totalCount > 0 && completeCount == totalCount; }
};

/// Checklist derivado do estado real do projeto. Não grava campos extras no
/// .ludo e não cria uma segunda fonte de verdade para progresso do editor.
class ProjectOnboarding {
public:
    static OnboardingSnapshot inspect(const core::Editor& editor);
    static QString summaryText(const OnboardingSnapshot& snapshot);
};

} // namespace ui
