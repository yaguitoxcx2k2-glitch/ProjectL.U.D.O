#pragma once

#include "GameData.h"

#include <QString>
#include <QVariantMap>
#include <QVector>

namespace core {

// RC2.43 / Bloco C
// Catálogo único dos valores consultáveis pelo sistema No-Code. Editor,
// Validator e runtime usam as mesmas chaves/tipos para evitar contratos
// paralelos e opções que existam apenas na interface.
struct GameValueDescriptor {
    QString key;
    QString label;
    CommonValueType type = CommonValueType::Number;
    bool needsEvent = false;
    bool needsPicture = false;
    bool needsMapPosition = false;
    bool needsAction = false;
};

const QVector<GameValueDescriptor>& gameValueDescriptors();
const GameValueDescriptor* gameValueDescriptor(const QString& key);
bool isKnownGameValue(const QString& key);
CommonValueType gameValueType(const QString& key, CommonValueType fallback = CommonValueType::Number);

// Validação estrutural única da consulta. Retorna vazio quando a consulta é
// válida; Editor/Validator podem exibir o texto retornado e o runtime continua
// consumindo exatamente o mesmo formato.
QString gameValueQueryProblem(const QVariantMap& query);

/// Ponte textual do ExpressionEvaluator para o mesmo catálogo de Game Values.
/// Retorna query vazia quando a função é direta (v/s/switch) ou desconhecida.
bool isKnownUniversalExpressionFunction(const QString& name);
CommonValueType universalExpressionFunctionType(const QString& name);
QVariantMap gameValueQueryFromInline(const QString& reference, QString* error = nullptr);
QVariantMap gameValueQueryForExpressionFunction(const QString& name,
                                                const QVariantList& arguments,
                                                QString* error = nullptr);

// Bloco 12 — Value Resolver em campos gerais. Em vez de cada handler decidir
// sozinho quais parâmetros aceitam valor dinâmico, este catálogo declara os
// campos escalares que podem persistir um ValueSpec. Constantes antigas
// continuam válidas no mesmo parâmetro.
struct CommandValueFieldDescriptor {
    QString commandType;
    QString parameter;
    CommonValueType type = CommonValueType::Number;
    QVariant defaultValue;
};

const QVector<CommandValueFieldDescriptor>& commandValueFieldDescriptors();
QVector<CommandValueFieldDescriptor> commandValueFields(const QString& commandType);
const CommandValueFieldDescriptor* commandValueFieldDescriptor(const QString& commandType,
                                                                const QString& parameter);
bool isValueSpec(const QVariant& value);
QVariantMap valueSpecFromLegacy(const QVariant& value, const QVariant& fallback = QVariant());

} // namespace core
