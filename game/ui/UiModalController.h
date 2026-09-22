#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace game::ui {

enum class UiModalType { None, Confirm, NumberInput, TextInput, ItemSelection };

/// Estado de uma janela modal do jogo. Não conhece QWidget nem janela nativa:
/// teclado, gamepad e mouse alteram este estado e GameUiLayer apenas o desenha.
class UiModalController {
public:
    void openConfirm(const QString& title, const QString& prompt, bool defaultYes,
                     int openAnimationMs, int closeAnimationMs = -1);
    void openNumber(const QString& title, const QString& prompt,
                    int minimum, int maximum, int current, int openAnimationMs,
                    int closeAnimationMs = -1);
    void openText(const QString& title, const QString& prompt, const QString& current,
                  int maximumLength, bool allowCancel, int openAnimationMs,
                  int closeAnimationMs = -1);
    void openItems(const QString& title, const QString& prompt,
                   const QStringList& labels, const QVector<int>& values,
                   int openAnimationMs, int closeAnimationMs = -1);

    void update(double dt);
    void move(int delta);
    void adjustNumber(int delta, bool largeStep = false);
    bool appendText(const QString& text);
    bool backspaceText();
    void confirm();
    void cancel();
    void selectIndex(int index);

    bool active() const { return m_type != UiModalType::None; }
    bool acceptingInput() const {
        return active() && !m_closing &&
               (m_openAnimationSeconds <= 0.0 || m_elapsed >= m_openAnimationSeconds);
    }
    bool completed() const { return m_completed; }
    bool accepted() const { return m_accepted; }
    bool closing() const { return m_closing; }
    UiModalType type() const { return m_type; }

    const QString& title() const { return m_title; }
    const QString& prompt() const { return m_prompt; }
    const QStringList& labels() const { return m_labels; }
    int selected() const { return m_selected; }
    int numberValue() const { return m_number; }
    const QString& textValue() const { return m_text; }
    int maximumTextLength() const { return m_textMaximumLength; }
    bool textCancelAllowed() const { return m_textAllowCancel; }
    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    int selectedValue() const;

    /// 0..1 usado por fade/scale/slide. Durante o fechamento anda ao contrário.
    double visualProgress() const;
    void clear();

private:
    void begin(UiModalType type, const QString& title, const QString& prompt,
               int openAnimationMs, int closeAnimationMs);
    void requestClose(bool accepted);

    UiModalType m_type = UiModalType::None;
    QString m_title;
    QString m_prompt;
    QStringList m_labels;
    QVector<int> m_values;
    int m_selected = 0;
    int m_minimum = 0;
    int m_maximum = 0;
    int m_number = 0;
    QString m_text;
    int m_textMaximumLength = 32;
    bool m_textAllowCancel = true;
    double m_elapsed = 0.0;
    double m_closeElapsed = 0.0;
    double m_openAnimationSeconds = 0.14;
    double m_closeAnimationSeconds = 0.14;
    bool m_closing = false;
    bool m_completed = false;
    bool m_accepted = false;
};

} // namespace game::ui
