#include "CommandPreviewDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace ui {

CommandPreviewDialog::CommandPreviewDialog(const QString& title, QWidget* previewWidget,
                                           QWidget* parent, QLabel* infoWidget)
    : QDialog(parent)
{
    setWindowTitle(title);
    setModal(false);
    resize(920, 680);
    setMinimumSize(520, 420);

    auto* root = new QVBoxLayout(this);
    if (previewWidget) {
        previewWidget->setParent(this);
        root->addWidget(previewWidget, 1);
    }
    if (infoWidget) {
        infoWidget->setParent(this);
        infoWidget->setWordWrap(true);
        root->addWidget(infoWidget);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::hide);
}

void CommandPreviewDialog::present()
{
    show();
    raise();
    activateWindow();
}

} // namespace ui
