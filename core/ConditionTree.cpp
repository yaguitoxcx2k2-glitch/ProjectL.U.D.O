#include "ConditionTree.h"

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>

namespace core {
namespace {

constexpr auto kNode = "node";
constexpr auto kGroup = "group";
constexpr auto kCondition = "condition";
constexpr auto kMode = "mode";
constexpr auto kChildren = "children";

QVariantMap legacyPredicate(QVariantMap params)
{
    // Metadados do comando não pertencem à condição folha.
    params.remove(QStringLiteral("conditionTree"));
    params.remove(QStringLiteral("timeoutFrames"));
    params.remove(QStringLiteral("createElse"));
    return params;
}

void collectStats(const QVariantMap& node, int depth, ConditionTreeStats& stats)
{
    stats.maxDepth = qMax(stats.maxDepth, depth);
    const QString kind = node.value(QStringLiteral("node")).toString();
    if (kind == QLatin1String(kCondition)) {
        ++stats.conditions;
        return;
    }
    if (kind != QLatin1String(kGroup)) return;
    ++stats.groups;
    const QVariantList children = node.value(QStringLiteral("children")).toList();
    for (const QVariant& child : children) collectStats(child.toMap(), depth + 1, stats);
}

void collectPredicates(const QVariantMap& node, QVector<QVariantMap>& out)
{
    const QString kind = node.value(QStringLiteral("node")).toString();
    if (kind == QLatin1String(kCondition)) {
        out.push_back(node.value(QStringLiteral("condition")).toMap());
        return;
    }
    if (kind != QLatin1String(kGroup)) return;
    for (const QVariant& child : node.value(QStringLiteral("children")).toList())
        collectPredicates(child.toMap(), out);
}

QString structureProblem(const QVariantMap& node, int depth, int maxDepth,
                         int& conditionCount, int maxConditions)
{
    if (node.isEmpty()) return QObject::tr("A árvore de condições está vazia.");
    if (depth > maxDepth)
        return QObject::tr("A árvore de condições excede o limite de %1 níveis.").arg(maxDepth);

    const QString kind = node.value(QStringLiteral("node")).toString();
    if (kind == QLatin1String(kCondition)) {
        ++conditionCount;
        if (conditionCount > maxConditions)
            return QObject::tr("A árvore de condições excede o limite de %1 condições.").arg(maxConditions);
        const QVariantMap predicate = node.value(QStringLiteral("condition")).toMap();
        if (predicate.isEmpty() || predicate.value(QStringLiteral("kind")).toString().isEmpty())
            return QObject::tr("Uma condição da árvore não possui um predicado válido.");
        return {};
    }
    if (kind != QLatin1String(kGroup))
        return QObject::tr("A árvore contém um tipo de nó desconhecido.");

    const QString mode = node.value(QStringLiteral("mode")).toString();
    if (mode != QLatin1String("all") && mode != QLatin1String("any"))
        return QObject::tr("Um grupo da árvore usa um modo lógico desconhecido.");
    const QVariantList children = node.value(QStringLiteral("children")).toList();
    if (children.isEmpty())
        return QObject::tr("Um grupo AND/OR não pode ficar vazio.");
    for (const QVariant& child : children) {
        const QVariantMap childMap = child.toMap();
        if (childMap.isEmpty()) return QObject::tr("A árvore contém um nó filho inválido.");
        const QString problem = structureProblem(childMap, depth + 1, maxDepth,
                                                 conditionCount, maxConditions);
        if (!problem.isEmpty()) return problem;
    }
    return {};
}

bool evaluateNode(const QVariantMap& node, const ConditionPredicateEvaluator& evaluator,
                  int depth, int maxDepth)
{
    if (depth > maxDepth || !evaluator) return false;
    const QString kind = node.value(QStringLiteral("node")).toString();
    if (kind == QLatin1String(kCondition))
        return evaluator(node.value(QStringLiteral("condition")).toMap());
    if (kind != QLatin1String(kGroup)) return false;

    const QVariantList children = node.value(QStringLiteral("children")).toList();
    if (children.isEmpty()) return false;
    const bool any = normalizedConditionGroupMode(node.value(QStringLiteral("mode")).toString()) == QLatin1String("any");
    if (any) {
        for (const QVariant& child : children)
            if (evaluateNode(child.toMap(), evaluator, depth + 1, maxDepth)) return true;
        return false;
    }
    for (const QVariant& child : children)
        if (!evaluateNode(child.toMap(), evaluator, depth + 1, maxDepth)) return false;
    return true;
}

QVariantMap simplifyNode(const QVariantMap& node, bool root)
{
    if (node.value(QStringLiteral("node")).toString() != QLatin1String(kGroup)) return node;
    QVariantList children;
    for (const QVariant& child : node.value(QStringLiteral("children")).toList()) {
        const QVariantMap simplified = simplifyNode(child.toMap(), false);
        if (!simplified.isEmpty()) children.push_back(simplified);
    }
    if (!root && children.size() == 1) return children.first().toMap();
    return conditionGroupNode(node.value(QStringLiteral("mode")).toString(), children);
}

QString defaultAtomText(const QVariantMap& atom)
{
    const QString kind = atom.value(QStringLiteral("kind"), QStringLiteral("condition")).toString();
    const QString op = atom.value(QStringLiteral("op")).toString();
    QVariant right = atom.value(QStringLiteral("value"));
    if (atom.contains(QStringLiteral("rightSpec"))) right = atom.value(QStringLiteral("rightSpec"));
    QString text = kind;
    if (!op.isEmpty()) text += QLatin1Char(' ') + op;
    if (right.isValid()) {
        if (right.metaType().id() == QMetaType::QVariantMap)
            text += QStringLiteral(" …");
        else
            text += QLatin1Char(' ') + right.toString();
    }
    return text;
}

QString nodeText(const QVariantMap& node, const ConditionAtomFormatter& formatter, int parentPrecedence)
{
    if (node.value(QStringLiteral("node")).toString() == QLatin1String(kCondition)) {
        const QVariantMap atom = node.value(QStringLiteral("condition")).toMap();
        return formatter ? formatter(atom) : defaultAtomText(atom);
    }
    if (node.value(QStringLiteral("node")).toString() != QLatin1String(kGroup)) return QObject::tr("condição inválida");
    const bool any = normalizedConditionGroupMode(node.value(QStringLiteral("mode")).toString()) == QLatin1String("any");
    const int precedence = any ? 1 : 2;
    QStringList parts;
    for (const QVariant& child : node.value(QStringLiteral("children")).toList())
        parts.push_back(nodeText(child.toMap(), formatter, precedence));
    QString text = parts.join(any ? QObject::tr(" OU ") : QObject::tr(" E "));
    if (precedence < parentPrecedence && parts.size() > 1) text = QLatin1Char('(') + text + QLatin1Char(')');
    return text;
}

} // namespace

QVariantMap conditionLeafNode(const QVariantMap& condition)
{
    return {{QStringLiteral("node"), QStringLiteral("condition")},
            {QStringLiteral("condition"), condition}};
}

QVariantMap conditionGroupNode(const QString& mode, const QVariantList& children)
{
    return {{QStringLiteral("node"), QStringLiteral("group")},
            {QStringLiteral("mode"), normalizedConditionGroupMode(mode)},
            {QStringLiteral("children"), children}};
}

QVariantMap conditionTreeFromLegacyParams(const QVariantMap& params)
{
    return conditionGroupNode(QStringLiteral("all"),
                              {conditionLeafNode(legacyPredicate(params))});
}

QVariantMap conditionTreeFromCommandParams(const QVariantMap& params)
{
    const QVariantMap tree = params.value(QStringLiteral("conditionTree")).toMap();
    return tree.isEmpty() ? conditionTreeFromLegacyParams(params) : tree;
}

bool isConditionTreeNode(const QVariantMap& node)
{
    const QString kind = node.value(QStringLiteral("node")).toString();
    return kind == QLatin1String(kGroup) || kind == QLatin1String(kCondition);
}

QString normalizedConditionGroupMode(const QString& mode)
{
    return mode.compare(QLatin1String("any"), Qt::CaseInsensitive) == 0 ||
           mode.compare(QLatin1String("or"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("any") : QStringLiteral("all");
}

ConditionTreeStats conditionTreeStats(const QVariantMap& tree)
{
    ConditionTreeStats stats;
    collectStats(tree, 1, stats);
    return stats;
}

QVector<QVariantMap> conditionTreePredicates(const QVariantMap& tree)
{
    QVector<QVariantMap> predicates;
    collectPredicates(tree, predicates);
    return predicates;
}

QVariantMap simplifyConditionTree(const QVariantMap& tree)
{
    return simplifyNode(tree, true);
}

bool conditionTreesDeepEqual(const QVariantMap& left, const QVariantMap& right)
{
    const QJsonObject a = QJsonObject::fromVariantMap(simplifyConditionTree(left));
    const QJsonObject b = QJsonObject::fromVariantMap(simplifyConditionTree(right));
    return QJsonDocument(a).toJson(QJsonDocument::Compact) ==
           QJsonDocument(b).toJson(QJsonDocument::Compact);
}

int conditionTreeAtomCount(const QVariantMap& tree)
{
    return conditionTreeStats(tree).conditions;
}

int conditionTreeMaxDepth(const QVariantMap& tree)
{
    return conditionTreeStats(tree).maxDepth;
}

QString conditionTreeToString(const QVariantMap& tree, const ConditionAtomFormatter& formatter)
{
    return nodeText(tree, formatter, 0);
}

QString conditionTreeStructureProblem(const QVariantMap& tree, int maxDepth, int maxConditions)
{
    int count = 0;
    return structureProblem(tree, 1, maxDepth, count, maxConditions);
}

bool evaluateConditionTree(const QVariantMap& tree,
                           const ConditionPredicateEvaluator& evaluator,
                           int maxDepth)
{
    return evaluateNode(tree, evaluator, 1, maxDepth);
}

} // namespace core
