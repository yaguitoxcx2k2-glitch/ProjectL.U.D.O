#pragma once

#include <QVariantMap>
#include <QVector>
#include <QString>
#include <functional>

namespace core {

// Árvore canônica de condições usada por If, Wait Until e por futuras
// superfícies que precisem compartilhar exatamente o mesmo contrato lógico.
//
// Nó grupo:
//   { node:"group", mode:"all"|"any", children:[ ... ] }
// Nó folha:
//   { node:"condition", condition:{ kind:"switch", ... } }
//
// Projetos antigos continuam válidos: os params históricos de uma condição
// simples são convertidos em uma folha somente quando a árvore é solicitada.
struct ConditionTreeStats {
    int groups = 0;
    int conditions = 0;
    int maxDepth = 0;
};

QVariantMap conditionLeafNode(const QVariantMap& condition);
QVariantMap conditionGroupNode(const QString& mode, const QVariantList& children = {});
QVariantMap conditionTreeFromLegacyParams(const QVariantMap& params);
QVariantMap conditionTreeFromCommandParams(const QVariantMap& params);

bool isConditionTreeNode(const QVariantMap& node);
QString normalizedConditionGroupMode(const QString& mode);
ConditionTreeStats conditionTreeStats(const QVariantMap& tree);
QVector<QVariantMap> conditionTreePredicates(const QVariantMap& tree);

/// Utilitários canônicos usados pelo Builder, Validator e testes. `simplify`
/// remove grupos redundantes de um único filho sem alterar a semântica.
QVariantMap simplifyConditionTree(const QVariantMap& tree);
bool conditionTreesDeepEqual(const QVariantMap& left, const QVariantMap& right);
int conditionTreeAtomCount(const QVariantMap& tree);
int conditionTreeMaxDepth(const QVariantMap& tree);
using ConditionAtomFormatter = std::function<QString(const QVariantMap&)>;
QString conditionTreeToString(const QVariantMap& tree,
                              const ConditionAtomFormatter& formatter = {});

// Problema puramente estrutural. Referências a Switch/Variable/GameValue etc.
// continuam pertencendo ao EventCommandValidator, que percorre as folhas.
QString conditionTreeStructureProblem(const QVariantMap& tree,
                                      int maxDepth = 10,
                                      int maxConditions = 64);

using ConditionPredicateEvaluator = std::function<bool(const QVariantMap&)>;
bool evaluateConditionTree(const QVariantMap& tree,
                           const ConditionPredicateEvaluator& evaluator,
                           int maxDepth = 12);

} // namespace core
