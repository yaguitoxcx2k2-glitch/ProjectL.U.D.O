#pragma once

#include "core/ProjectBootstrap.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QComboBox;
QT_END_NAMESPACE

namespace ui {

/// Criação de um projeto de mapas LUDO. O usuário escolhe a engine-alvo
/// (RPG Maker MV ou MZ) no nascimento do projeto.
class ProjectCreationDialog final : public QDialog
{
public:
    explicit ProjectCreationDialog(QWidget* parent = nullptr,
                                   const QString& initialParentFolder = QString());

    core::ProjectCreationRequest request() const;

private:
    QLineEdit* m_name = nullptr;
    QLineEdit* m_folder = nullptr;
    QComboBox* m_engine = nullptr;
};

} // namespace ui
