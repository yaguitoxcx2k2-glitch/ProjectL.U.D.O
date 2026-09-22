#pragma once

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace game::ui {

/// Navegação de foco independente de QWidget. O mesmo fluxo recebe GameAction
/// do teclado e do gamepad; os widgets nunca conhecem teclas/botões físicos.
int moveFocusIndex(int current, int count, int delta, bool wrap = true);

enum class UiNavDirection { Up, Down, Left, Right };

/// Escolhe geometricamente o próximo elemento. Prioriza candidatos na direção
/// solicitada e usa distância perpendicular como penalidade. Quando wrap=true
/// e não há candidato à frente, procura o extremo oposto mais alinhado.
QString spatialFocusNeighbor(const QString& currentId,
                             const QStringList& candidates,
                             const QHash<QString, QRectF>& rects,
                             UiNavDirection direction,
                             bool wrap = true);

class UiFocusController {
public:
    void setCount(int count);
    void setIndex(int index);
    void setWrap(bool wrap) { m_wrap = wrap; }
    int count() const { return m_count; }
    int index() const { return m_index; }
    bool move(int delta);

private:
    int m_count = 0;
    int m_index = 0;
    bool m_wrap = true;
};

} // namespace game::ui
