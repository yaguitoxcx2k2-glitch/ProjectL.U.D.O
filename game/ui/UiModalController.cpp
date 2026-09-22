#include "UiModalController.h"

#include <QtGlobal>
#include <QCoreApplication>
#include <algorithm>

namespace game::ui {

void UiModalController::begin(UiModalType type, const QString& title,
                              const QString& prompt, int openAnimationMs,
                              int closeAnimationMs)
{
    m_type = type;
    m_title = title;
    m_prompt = prompt;
    m_labels.clear();
    m_values.clear();
    m_selected = 0;
    m_elapsed = 0.0;
    m_closeElapsed = 0.0;
    if (closeAnimationMs < 0) closeAnimationMs = openAnimationMs;
    m_openAnimationSeconds = qBound(0, openAnimationMs, 2000) / 1000.0;
    m_closeAnimationSeconds = qBound(0, closeAnimationMs, 2000) / 1000.0;
    m_closing = false;
    m_completed = false;
    m_accepted = false;
}

void UiModalController::openConfirm(const QString& title, const QString& prompt,
                                    bool defaultYes, int openAnimationMs,
                                    int closeAnimationMs)
{
    begin(UiModalType::Confirm, title, prompt, openAnimationMs, closeAnimationMs);
    m_labels = {QCoreApplication::translate("UiModalController", "Sim"),
                QCoreApplication::translate("UiModalController", "Não")};
    m_values = {1, 0};
    m_selected = defaultYes ? 0 : 1;
}

void UiModalController::openNumber(const QString& title, const QString& prompt,
                                   int minimum, int maximum, int current,
                                   int openAnimationMs, int closeAnimationMs)
{
    begin(UiModalType::NumberInput, title, prompt, openAnimationMs, closeAnimationMs);
    if (minimum > maximum) qSwap(minimum, maximum);
    m_minimum = minimum;
    m_maximum = maximum;
    m_number = qBound(minimum, current, maximum);
}

void UiModalController::openText(const QString& title, const QString& prompt,
                                 const QString& current, int maximumLength,
                                 bool allowCancel, int openAnimationMs, int closeAnimationMs)
{
    begin(UiModalType::TextInput, title, prompt, openAnimationMs, closeAnimationMs);
    m_textMaximumLength = qBound(1, maximumLength, 1024);
    m_text = current.left(m_textMaximumLength);
    m_textAllowCancel = allowCancel;
}

void UiModalController::openItems(const QString& title, const QString& prompt,
                                  const QStringList& labels, const QVector<int>& values,
                                  int openAnimationMs, int closeAnimationMs)
{
    begin(UiModalType::ItemSelection, title, prompt, openAnimationMs, closeAnimationMs);
    m_labels = labels;
    m_values = values;
    if (m_values.size() < m_labels.size()) m_values.resize(m_labels.size());
}

void UiModalController::update(double dt)
{
    if (!active()) return;
    if (!m_closing) {
        m_elapsed += qMax(0.0, dt);
        return;
    }
    m_closeElapsed += qMax(0.0, dt);
    if (m_closeAnimationSeconds <= 0.0 || m_closeElapsed >= m_closeAnimationSeconds)
        m_completed = true;
}

void UiModalController::move(int delta)
{
    if (!acceptingInput() || m_labels.isEmpty() || delta == 0) return;
    const int n = m_labels.size();
    m_selected = (m_selected + delta) % n;
    if (m_selected < 0) m_selected += n;
}

void UiModalController::adjustNumber(int delta, bool largeStep)
{
    if (!acceptingInput() || m_type != UiModalType::NumberInput || delta == 0) return;
    const qint64 step = largeStep ? 10 : 1;
    const qint64 next = qint64(m_number) + qint64(delta) * step;
    const qint64 bounded = std::clamp(next, qint64(m_minimum), qint64(m_maximum));
    m_number = int(bounded);
}

bool UiModalController::appendText(const QString& text)
{
    if (!acceptingInput() || m_type != UiModalType::TextInput || text.isEmpty()) return false;
    QString filtered;
    filtered.reserve(text.size());
    for (QChar ch : text) if (ch.isPrint()) filtered += ch;
    if (filtered.isEmpty() || m_text.size() >= m_textMaximumLength) return false;
    const QString before = m_text;
    m_text += filtered.left(m_textMaximumLength - m_text.size());
    return m_text != before;
}

bool UiModalController::backspaceText()
{
    if (!acceptingInput() || m_type != UiModalType::TextInput || m_text.isEmpty()) return false;
    m_text.chop(1);
    return true;
}

void UiModalController::selectIndex(int index)
{
    if (!acceptingInput() || m_labels.isEmpty()) return;
    m_selected = qBound(0, index, m_labels.size() - 1);
}

void UiModalController::confirm()
{
    if (!acceptingInput()) return;
    if (m_type == UiModalType::ItemSelection && m_labels.isEmpty()) {
        requestClose(false);
        return;
    }
    requestClose(true);
}

void UiModalController::cancel()
{
    if (!acceptingInput()) return;
    if (m_type == UiModalType::TextInput && !m_textAllowCancel) return;
    requestClose(false);
}

void UiModalController::requestClose(bool accepted)
{
    m_accepted = accepted;
    m_closing = true;
    m_closeElapsed = 0.0;
    if (m_closeAnimationSeconds <= 0.0) m_completed = true;
}

int UiModalController::selectedValue() const
{
    if (m_selected < 0 || m_selected >= m_values.size()) return 0;
    return m_values.at(m_selected);
}

double UiModalController::visualProgress() const
{
    if (!active()) return 0.0;
    if (m_closing) {
        if (m_closeAnimationSeconds <= 0.0) return 0.0;
        return 1.0 - qBound(0.0, m_closeElapsed / m_closeAnimationSeconds, 1.0);
    }
    if (m_openAnimationSeconds <= 0.0) return 1.0;
    return qBound(0.0, m_elapsed / m_openAnimationSeconds, 1.0);
}

void UiModalController::clear()
{
    *this = UiModalController();
}

} // namespace game::ui
