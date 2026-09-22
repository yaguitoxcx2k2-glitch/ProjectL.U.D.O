#include "NarrativePreviewWidget.h"

#include "core/DialogueContent.h"
#include "core/Editor.h"
#include "core/Localization.h"
#include "game/TextBox.h"
#include "game/TextDraw.h"
#include "game/ui/UiTheme.h"

#include <QPainter>
#include <QRegularExpression>
#include <QTimer>

namespace ui {

NarrativePreviewWidget::NarrativePreviewWidget(core::Editor& editor, QWidget* parent)
    : QWidget(parent), m_editor(editor), m_timer(new QTimer(this))
{
    setMinimumSize(480, 300);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

QSize NarrativePreviewWidget::sizeHint() const
{
    const QSize logical = m_editor.gameResolution;
    return logical.isValid() ? logical.boundedTo(QSize(960, 540)) : QSize(640, 360);
}

void NarrativePreviewWidget::setCommand(const core::EventCommand& command)
{
    m_command = command; m_pausedMs = 0; m_playing = false; m_timer->stop(); update();
}

void NarrativePreviewWidget::clearCommand()
{
    m_command = {}; pause(); update();
}

bool NarrativePreviewWidget::isPlayable() const
{
    return m_command.type == QLatin1String("message") ||
           m_command.type == QLatin1String("subtitle.show") ||
           m_command.type == QLatin1String("subtitle.enqueue") ||
           m_command.type == QLatin1String("choice.show");
}

void NarrativePreviewWidget::play()
{
    if (!isPlayable()) return;
    m_pausedMs = 0; m_clock.restart(); m_playing = true; m_timer->start(); update();
}

void NarrativePreviewWidget::pause()
{
    if (m_playing) m_pausedMs = m_clock.elapsed();
    m_playing = false; m_timer->stop();
}

QString NarrativePreviewWidget::resolvedText() const
{
    const QVariantMap& params = m_command.params;
    core::DialogueContent dialogue = core::DialogueContent::fromVariantMap(params.value(QStringLiteral("dialogueContent")).toMap());
    const QString fallback = dialogue.text.isEmpty() ? params.value(QStringLiteral("text")).toString() : dialogue.text;
    QString result = m_editor.localization.resolve(params.value(QStringLiteral("localizationKey")).toString(), fallback,
                                                   core::currentPlayerLocale(m_editor.localization));
    QRegularExpression token(QStringLiteral("\\\\LOC\\[([^\\]|]+)(?:\\|([^\\]]*))?\\]"), QRegularExpression::CaseInsensitiveOption);
    auto matches=token.globalMatch(result);QVector<QPair<QPair<int,int>,QString>> replacements;
    while(matches.hasNext()){const auto match=matches.next();const QString key=match.captured(1).trimmed();const QString direct=match.captured(2).isNull()?key:match.captured(2);replacements.push_back({{match.capturedStart(),match.capturedLength()},m_editor.localization.resolve(key,direct,core::currentPlayerLocale(m_editor.localization))});}
    for(int i=replacements.size()-1;i>=0;--i)result.replace(replacements[i].first.first,replacements[i].first.second,replacements[i].second);
    return result;
}

QString NarrativePreviewWidget::resolvedSpeaker() const
{
    const QVariantMap& params = m_command.params;
    const core::DialogueContent dialogue = core::DialogueContent::fromVariantMap(params.value(QStringLiteral("dialogueContent")).toMap());
    QString fallback = params.value(QStringLiteral("speaker")).toString();
    if (const core::SpeakerProfile* profile = m_editor.speakerDatabase.findById(dialogue.speakerId)) fallback = profile->name;
    return m_editor.localization.resolve(params.value(QStringLiteral("speakerLocalizationKey")).toString(), fallback,
                                         core::currentPlayerLocale(m_editor.localization));
}

int NarrativePreviewWidget::revealedGlyphs(int total) const
{
    if (!m_playing) return total;
    const double seconds = m_clock.elapsed() / 1000.0;
    return qBound(0, int(seconds * 40.0), total);
}

void NarrativePreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#151923")));
    // Palco deliberadamente neutro: o conteúdo usa tema, fonte e resolução do projeto.
    painter.fillRect(rect().adjusted(12, 12, -12, -12), QColor(QStringLiteral("#253044")));
    const game::ui::UiTheme theme = game::ui::UiTheme::fromSettings(m_editor.gameUi);
    QFont font(m_editor.mainFontFamily); font.setPixelSize(qMax(10, m_editor.gameUi.fontSize));

    if (m_command.type == QLatin1String("choice.show")) {
        QStringList choices = m_command.params.value(QStringLiteral("choices")).toStringList();
        if (choices.isEmpty()) for (const QVariant& value : m_command.params.value(QStringLiteral("choices")).toList()) choices.push_back(value.toString());
        const int width = qMin(420, this->width() - 48), line = qMax(34, QFontMetrics(font).height() + 14);
        const QRect box(this->width() - width - 24, qMax(24, (height() - choices.size() * line) / 2), width, qMax(line, choices.size() * line));
        painter.setPen(QPen(theme.choiceWindow.border, theme.choiceWindow.borderWidth)); painter.setBrush(theme.choiceWindow.fill); painter.drawRoundedRect(box, theme.choiceWindow.radius, theme.choiceWindow.radius);
        for (int i = 0; i < choices.size(); ++i) {
            const auto pages = game::layoutMessage(choices.at(i), font, width - 36, 1, {}, &m_editor.iconSet);
            if (pages.isEmpty()) continue;
            game::TextDrawOpts options; options.color = i == 0 ? theme.selectedText : theme.text; options.icons = &m_editor.iconSet;
            game::drawTextPage(painter, pages.first(), font, QRectF(box.left() + 18, box.top() + i * line + 6, width - 36, line - 8), options);
        }
        return;
    }

    if (m_command.type != QLatin1String("message") && m_command.type != QLatin1String("subtitle.show") &&
        m_command.type != QLatin1String("subtitle.enqueue")) {
        painter.setPen(QColor(QStringLiteral("#b8c2d8"))); painter.setFont(font);
        painter.drawText(rect().adjusted(30,30,-30,-30), Qt::AlignCenter|Qt::TextWordWrap,
                         tr("Selecione uma mensagem, legenda ou escolha para visualizar."));
        return;
    }

    const QString text = resolvedText(), speaker = resolvedSpeaker();
    const bool subtitle = m_command.type.startsWith(QLatin1String("subtitle."));
    const int boxHeight = subtitle ? qMax(80, height() / 4) : qMax(120, height() / 3);
    QRect box(24, height() - boxHeight - 24, width() - 48, boxHeight);
    const QString position = m_command.params.value(QStringLiteral("position"), QStringLiteral("bottom")).toString();
    if (position == QLatin1String("top")) box.moveTop(24); else if (position == QLatin1String("middle")) box.moveTop((height() - box.height()) / 2);
    painter.setPen(QPen(theme.window.border, theme.window.borderWidth)); painter.setBrush(theme.window.fill); painter.drawRoundedRect(box, theme.window.radius, theme.window.radius);
    int textTop = box.top() + 18;
    if (!speaker.isEmpty()) { painter.setFont(font); painter.setPen(theme.accent); painter.drawText(QRect(box.left()+20,textTop,box.width()-40,QFontMetrics(font).height()), speaker); textTop += QFontMetrics(font).height()+6; }
    const auto pages = game::layoutMessage(text, font, box.width() - 40, 4, {}, &m_editor.iconSet);
    if (pages.isEmpty()) return;
    game::TextDrawOpts options; options.color = theme.text; options.icons = &m_editor.iconSet; options.revealed = revealedGlyphs(pages.first().drawableCount()); options.time = (m_playing ? m_clock.elapsed() : m_pausedMs) / 1000.0;
    options.effects = core::TextEffectStack::fromVariantMap(m_command.params.value(QStringLiteral("textEffects")).toMap());
    options.gradient = core::TextGradientSpec::fromVariantMap(m_command.params.value(QStringLiteral("textGradient")).toMap());
    game::drawTextPage(painter, pages.first(), font, QRectF(box.left()+20,textTop,box.width()-40,box.bottom()-textTop-12), options);
}

} // namespace ui
