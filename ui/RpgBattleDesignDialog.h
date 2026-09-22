#pragma once

#include "core/Editor.h"

#include <QDialog>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace core { struct DatabaseRecord; }

namespace ui {

/// Editor visual No-Code para registros da categoria "animations".
class RpgAnimationEditorDialog : public QDialog
{
public:
    RpgAnimationEditorDialog(core::Editor& editor, const QVariantMap& data,
                             QWidget* parent = nullptr);
    ~RpgAnimationEditorDialog() override;
    QVariantMap animationData() const;

private:
    class Impl;
    Impl* m_impl = nullptr;
};

class EnemyAiEditorDialog : public QDialog
{
public:
    EnemyAiEditorDialog(const QVector<core::DatabaseRecord>& skills,
                        const QVector<core::DatabaseRecord>& states,
                        const QVariantList& rules, QWidget* parent = nullptr);
    ~EnemyAiEditorDialog() override;
    QVariantList rules() const;

private:
    class Impl;
    Impl* m_impl = nullptr;
};

class TroopFormationDialog : public QDialog
{
public:
    TroopFormationDialog(const QVector<core::DatabaseRecord>& enemies,
                         const QVariantList& formationData, QWidget* parent = nullptr);
    ~TroopFormationDialog() override;
    QVariantList enemySlots() const;

private:
    class Impl;
    Impl* m_impl = nullptr;
};

class TroopBattleEventsDialog : public QDialog
{
public:
    TroopBattleEventsDialog(core::Editor& editor,
                            const QVector<core::DatabaseRecord>& enemies,
                            const QVector<core::DatabaseRecord>& actors,
                            const QVector<core::DatabaseRecord>& states,
                            const QVariantList& events, QWidget* parent = nullptr);
    ~TroopBattleEventsDialog() override;
    QVariantList events() const;

private:
    class Impl;
    Impl* m_impl = nullptr;
};

} // namespace ui
