#pragma once

#include <QString>
#include <QVector>

namespace core {

class Editor;

enum class ProductReadinessLevel { Ready, Attention, Blocked };

struct ProductReadinessCheck {
    QString code;
    QString title;
    QString detail;
    ProductReadinessLevel level = ProductReadinessLevel::Ready;
};

struct ProductReadinessSnapshot {
    QVector<ProductReadinessCheck> checks;
    int blockedCount = 0;
    int attentionCount = 0;
    int readyCount = 0;

    bool canPublish() const { return blockedCount == 0; }
};

/// Diagnóstico operacional para publicação. Complementa o ProjectValidator:
/// ele verifica o conteúdo autoral; este serviço verifica também filesystem,
/// permissões e espaço disponível sem modificar o projeto.
class ProductReadiness {
public:
    static ProductReadinessSnapshot inspect(const Editor& editor);
    static QString levelLabel(ProductReadinessLevel level);
};

} // namespace core
