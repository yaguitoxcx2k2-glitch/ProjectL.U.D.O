#include "ScreenToneDialog.h"

#include "core/Renderer.h"
#include "MapPreviewRenderer.h"
#include "CommandPreviewDialog.h"

#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ui {
namespace {

/// Preview isolado da tonalidade. A equação de cor é a mesma usada no runtime
/// CPU (core::applyScreenTone); este widget cuida apenas da apresentação.
class ScreenTonePreview final : public QWidget
{
public:
    explicit ScreenTonePreview(const core::Editor& editorRef, QWidget* parent = nullptr)
        : QWidget(parent), ed(editorRef)
    {
        setMinimumSize(400, 270);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override { return QSize(520, 325); }

    void setTone(int red, int green, int blue, int gray)
    {
        red = qBound(-255, red, 255);
        green = qBound(-255, green, 255);
        blue = qBound(-255, blue, 255);
        gray = qBound(0, gray, 255);
        if (m_red == red && m_green == green && m_blue == blue && m_gray == gray)
            return;
        m_red = red;
        m_green = green;
        m_blue = blue;
        m_gray = gray;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#101216"));

        ensureBaseImage();
        if (m_base.isNull()) {
            painter.setPen(QColor("#8b9098"));
            painter.drawText(rect(), Qt::AlignCenter, tr("Prévia indisponível"));
            return;
        }

        QImage toned = m_base;
        core::applyScreenTone(toned, m_red, m_green, m_blue, m_gray);

        const QRectF available = QRectF(rect()).adjusted(10, 10, -10, -32);
        const double scale = qMin(available.width() / qMax(1, toned.width()),
                                  available.height() / qMax(1, toned.height()));
        const QSizeF drawSize(toned.width() * scale, toned.height() * scale);
        const QRectF target(available.center() -
                                QPointF(drawSize.width() / 2.0, drawSize.height() / 2.0),
                            drawSize);

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(target, toned);
        painter.setPen(QPen(QColor(255, 255, 255, 55), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(target);

        painter.setPen(QColor("#cbd2dc"));
        painter.drawText(QRectF(8, height() - 24, width() - 16, 18),
                         Qt::AlignCenter,
                         tr("R %1  ·  G %2  ·  B %3  ·  Cinza %4")
                             .arg(m_red).arg(m_green).arg(m_blue).arg(m_gray));
    }

private:
    void ensureBaseImage()
    {
        if (!m_base.isNull()) return;

        const core::MapDoc* current = ed.doc();
        if (current) m_base = renderMapPreview(ed, *current, QSize(480, 300));
    }

    const core::Editor& ed;
    QImage m_base;
    int m_red = 0;
    int m_green = 0;
    int m_blue = 0;
    int m_gray = 0;
};

} // namespace

bool isScreenToneCommand(const QString& type)
{
    return type == QLatin1String("ludo.screen.tone") ||
           type == QLatin1String("ludo.screen.clearTone");
}

bool editScreenToneCommand(core::Editor& ed, core::EventCommand& cmd, QWidget* parent)
{
    const bool clear = cmd.type == QLatin1String("ludo.screen.clearTone");
    QDialog dialog(parent);
    dialog.setWindowTitle(clear ? QObject::tr("Remover tonalidade da tela")
                                : QObject::tr("Ajustar tonalidade da tela"));

    auto* layout = new QVBoxLayout(&dialog);
    auto* info = new QLabel(QObject::tr(
        "A tonalidade é aplicada sobre a tela inteira, como no editores de RPG."), &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* form = new QFormLayout;
    QSpinBox* red = nullptr;
    QSpinBox* green = nullptr;
    QSpinBox* blue = nullptr;
    QSpinBox* gray = nullptr;

    if (!clear) {
        dialog.resize(660, 650);
        red = new QSpinBox(&dialog);
        green = new QSpinBox(&dialog);
        blue = new QSpinBox(&dialog);
        gray = new QSpinBox(&dialog);
        for (QSpinBox* spin : {red, green, blue}) {
            spin->setRange(-255, 255);
            spin->setSingleStep(5);
        }
        gray->setRange(0, 255);
        gray->setSingleStep(5);
        red->setValue(cmd.params.value(QStringLiteral("red"), 0).toInt());
        green->setValue(cmd.params.value(QStringLiteral("green"), 0).toInt());
        blue->setValue(cmd.params.value(QStringLiteral("blue"), 0).toInt());
        gray->setValue(cmd.params.value(QStringLiteral("gray"), 0).toInt());
        form->addRow(QObject::tr("Vermelho:"), red);
        form->addRow(QObject::tr("Verde:"), green);
        form->addRow(QObject::tr("Azul:"), blue);
        form->addRow(QObject::tr("Cinza / dessaturação:"), gray);
    }

    auto* duration = new QSpinBox(&dialog);
    duration->setRange(0, 3600);
    duration->setSuffix(QObject::tr(" quadros"));
    duration->setValue(cmd.params.value(QStringLiteral("duration"), 30).toInt());
    auto* wait = new QCheckBox(QObject::tr("Esperar a tonalidade terminar"), &dialog);
    wait->setChecked(cmd.params.value(QStringLiteral("wait"), false).toBool());
    form->addRow(QObject::tr("Duração:"), duration);
    form->addRow(wait);

    if (red) {
        layout->addLayout(form);
        auto* preview = new ScreenTonePreview(ed, &dialog);
        auto* previewDialog = new CommandPreviewDialog(QObject::tr("Prévia da tonalidade"), preview, &dialog);
        auto* previewButton = new QPushButton(QObject::tr("Ver prévia"), &dialog);
        previewButton->setToolTip(QObject::tr("Abre a pré-visualização em uma janela sem ocupar espaço do comando."));
        layout->addWidget(previewButton);
        QObject::connect(previewButton, &QPushButton::clicked, previewDialog, &CommandPreviewDialog::present);
        dialog.resize(660, 650);

        const auto refresh = [preview, red, green, blue, gray] {
            preview->setTone(red->value(), green->value(), blue->value(), gray->value());
        };
        QObject::connect(red, &QSpinBox::valueChanged, &dialog, [refresh](int) { refresh(); });
        QObject::connect(green, &QSpinBox::valueChanged, &dialog, [refresh](int) { refresh(); });
        QObject::connect(blue, &QSpinBox::valueChanged, &dialog, [refresh](int) { refresh(); });
        QObject::connect(gray, &QSpinBox::valueChanged, &dialog, [refresh](int) { refresh(); });
        refresh();
    } else {
        layout->addLayout(form);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    cmd.params.clear();
    if (red) {
        cmd.params[QStringLiteral("red")] = red->value();
        cmd.params[QStringLiteral("green")] = green->value();
        cmd.params[QStringLiteral("blue")] = blue->value();
        cmd.params[QStringLiteral("gray")] = gray->value();
    }
    cmd.params[QStringLiteral("duration")] = duration->value();
    cmd.params[QStringLiteral("wait")] = wait->isChecked();
    return true;
}

} // namespace ui
