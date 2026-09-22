#pragma once

#include "UiTheme.h"

#include <QFont>

namespace game::ui {

/// Estilo resolvido e backend-agnostic compartilhado entre os Widgets do
/// UI Designer e os componentes nativos (Mensagem, Name Box, Escolhas,
/// Menu, Modal, Batalha, Loja e HUD). Centralizar a resolução evita que
/// CPU/QRhi/editor tenham regras diferentes de herança/estado.
struct UiResolvedStyle {
    UiPanelStyle panel;
    QColor text = Qt::white;
    QColor accent = QColor("#67d6ff");
    QString backgroundMode = QStringLiteral("color");
    QImage backgroundImage;
    QMargins slices{12,12,12,12};
    qreal opacity = 1.0;
    int paddingX = 8;
    int paddingY = 6;
    QFont font;
    QColor stateOverlay;
};

UiResolvedStyle resolveStyleClass(const UiTheme& theme, const core::Editor& editor,
                                  const core::UiStyleClassSettings& style,
                                  const QString& stateId, const QFont& fallbackFont);

/// Resolve um componente nativo. Quando não existe override, herda o Theme
/// global. O `componentId` é estável e persistido apenas como chave de mapeamento.
UiResolvedStyle resolveNativeStyle(const UiTheme& theme, const core::Editor& editor,
                                   const QString& componentId, const QString& stateId,
                                   const QFont& fallbackFont);
UiResolvedStyle resolveNativeStyle(const UiTheme& theme, const QString& componentId,
                                   const QString& stateId, const QFont& fallbackFont);

void appendResolvedBackground(UiDrawList& list, const QRectF& rect, const UiTheme& theme,
                              const UiResolvedStyle& style, qreal opacity = 1.0);

QString nativeUiComponentLabel(const QString& componentId);
QStringList nativeUiComponentIds();

} // namespace game::ui
