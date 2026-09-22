#pragma once

#include <QVariant>
#include <QStringList>
#include <functional>

namespace core {

using ExpressionFunctionResolver = std::function<QVariant(
    const QString& name, const QVariantList& arguments, bool* handled, QString* error)>;

struct ExpressionContext {
    ExpressionFunctionResolver resolveFunction;
    int maxGameValueDepth = 8;
};

struct ExpressionResult {
    QVariant value;
    QString error;
    int errorPosition = -1;
    QStringList warnings;
    bool ok() const { return error.isEmpty(); }
};

/// Avaliador determinístico usado por ValueSpec. Não executa script nem acessa
/// o sistema operacional; somente matemática, lógica, strings e funções
/// resolvidas explicitamente pelo contexto do jogo.
ExpressionResult evaluateExpression(const QString& expression,
                                    const ExpressionContext& context = {});
bool looksLikeExpression(const QString& text);

} // namespace core
