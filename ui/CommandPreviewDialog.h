#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QWidget;
QT_END_NAMESPACE

namespace ui {

/// RC2.64 / Bloco M — janela compartilhada para previews de comandos.
/// O preview deixa de ocupar uma coluna permanente no editor: o widget real de
/// preview continua vivo e atualizando, mas só aparece quando o autor pede.
class CommandPreviewDialog final : public QDialog
{
public:
    explicit CommandPreviewDialog(const QString& title, QWidget* previewWidget,
                                  QWidget* parent = nullptr,
                                  QLabel* infoWidget = nullptr);

    /// Mostra sem bloquear a janela do comando. Assim alterações feitas no
    /// formulário continuam atualizando o mesmo preview enquanto ele estiver aberto.
    void present();
};

} // namespace ui
