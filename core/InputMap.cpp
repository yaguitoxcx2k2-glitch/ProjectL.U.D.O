#include "InputMap.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QKeySequence>

namespace core {

QVector<GameAction> allGameActions()
{
    return { GameAction::Up, GameAction::Down, GameAction::Left, GameAction::Right,
             GameAction::Confirm, GameAction::Cancel, GameAction::ToggleHud,
             GameAction::ZoomIn, GameAction::ZoomOut, GameAction::QuickSave,
             GameAction::QuickLoad, GameAction::SkipCutscene,
             GameAction::Action1, GameAction::Action2, GameAction::Action3, GameAction::Action4,
             GameAction::Quit };
}

QString gameActionId(GameAction a)
{
    switch (a) {
    case GameAction::Up:        return QStringLiteral("up");
    case GameAction::Down:      return QStringLiteral("down");
    case GameAction::Left:      return QStringLiteral("left");
    case GameAction::Right:     return QStringLiteral("right");
    case GameAction::Confirm:   return QStringLiteral("confirm");
    case GameAction::Cancel:    return QStringLiteral("cancel");
    case GameAction::ToggleHud: return QStringLiteral("toggleHud");
    case GameAction::ZoomIn:    return QStringLiteral("zoomIn");
    case GameAction::ZoomOut:  return QStringLiteral("zoomOut");
    case GameAction::QuickSave:return QStringLiteral("quickSave");
    case GameAction::QuickLoad:return QStringLiteral("quickLoad");
    case GameAction::SkipCutscene:return QStringLiteral("skipCutscene");
    case GameAction::Action1: return QStringLiteral("action1");
    case GameAction::Action2: return QStringLiteral("action2");
    case GameAction::Action3: return QStringLiteral("action3");
    case GameAction::Action4: return QStringLiteral("action4");
    case GameAction::Quit:     return QStringLiteral("quit");
    }
    return QStringLiteral("up");
}

GameAction gameActionFromId(const QString& id, bool* ok)
{
    for (GameAction a : allGameActions())
        if (gameActionId(a) == id) { if (ok) *ok = true; return a; }
    if (ok) *ok = false;
    return GameAction::Up;
}

QString gameActionLabel(GameAction a)
{
    switch (a) {
    case GameAction::Up:        return QCoreApplication::translate("InputMap", "Andar para cima");
    case GameAction::Down:      return QCoreApplication::translate("InputMap", "Andar para baixo");
    case GameAction::Left:      return QCoreApplication::translate("InputMap", "Andar para a esquerda");
    case GameAction::Right:     return QCoreApplication::translate("InputMap", "Andar para a direita");
    case GameAction::Confirm:   return QCoreApplication::translate("InputMap", "Falar / confirmar");
    case GameAction::Cancel:    return QCoreApplication::translate("InputMap", "Cancelar");
    case GameAction::ToggleHud: return QCoreApplication::translate("InputMap", "Mostrar/esconder HUD");
    case GameAction::ZoomIn:    return QCoreApplication::translate("InputMap", "Aumentar zoom");
    case GameAction::ZoomOut:  return QCoreApplication::translate("InputMap", "Diminuir zoom");
    case GameAction::QuickSave:return QCoreApplication::translate("InputMap", "Salvar rapidamente");
    case GameAction::QuickLoad:return QCoreApplication::translate("InputMap", "Carregar save rápido");
    case GameAction::SkipCutscene:return QCoreApplication::translate("InputMap", "Pular cutscene");
    case GameAction::Action1: return QCoreApplication::translate("InputMap", "Ação de jogo 1");
    case GameAction::Action2: return QCoreApplication::translate("InputMap", "Ação de jogo 2");
    case GameAction::Action3: return QCoreApplication::translate("InputMap", "Ação de jogo 3");
    case GameAction::Action4: return QCoreApplication::translate("InputMap", "Ação de jogo 4");
    case GameAction::Quit:     return QCoreApplication::translate("InputMap", "Sair do teste");
    }
    return QString();
}

InputMap InputMap::defaults()
{
    InputMap m;
    m.setKeys(GameAction::Up,      { Qt::Key_Up,    Qt::Key_W });
    m.setKeys(GameAction::Down,    { Qt::Key_Down,  Qt::Key_S });
    m.setKeys(GameAction::Left,    { Qt::Key_Left,  Qt::Key_A });
    m.setKeys(GameAction::Right,   { Qt::Key_Right, Qt::Key_D });
    m.setKeys(GameAction::Confirm, { Qt::Key_Z, Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space });
    m.setKeys(GameAction::Cancel,  { Qt::Key_X, Qt::Key_Escape });
    m.setKeys(GameAction::ToggleHud, { Qt::Key_F1 });
    m.setKeys(GameAction::ZoomIn,  { Qt::Key_Plus, Qt::Key_Equal });
    m.setKeys(GameAction::ZoomOut, { Qt::Key_Minus });
    m.setKeys(GameAction::QuickSave, { Qt::Key_F6 });
    m.setKeys(GameAction::QuickLoad, { Qt::Key_F7 });
    m.setKeys(GameAction::SkipCutscene, { Qt::Key_C });
    m.setKeys(GameAction::Quit,    { Qt::Key_Escape });
    return m;
}

QVector<int> InputMap::keysFor(GameAction a) const
{
    return m_keys.value(int(a));
}

void InputMap::setKeys(GameAction a, const QVector<int>& keys)
{
    QVector<int> limpas;
    for (int k : keys)
        if (k != 0 && !limpas.contains(k)) limpas.push_back(k);
    m_keys[int(a)] = limpas;
}

void InputMap::addKey(GameAction a, int key)
{
    QVector<int> k = keysFor(a);
    if (!k.contains(key)) k.push_back(key);
    setKeys(a, k);
}

void InputMap::clear(GameAction a)
{
    m_keys[int(a)] = QVector<int>();
}

bool InputMap::matches(GameAction a, int key) const
{
    return m_keys.value(int(a)).contains(key);
}

bool InputMap::findConflict(GameAction ignore, int key, GameAction* other) const
{
    for (GameAction a : allGameActions()) {
        if (a == ignore) continue;
        if (matches(a, key)) { if (other) *other = a; return true; }
    }
    return false;
}

QString InputMap::keyName(int key)
{
    switch (key) {
    case Qt::Key_Space:  return QCoreApplication::translate("InputMap", "Espaço");
    case Qt::Key_Return: return QStringLiteral("Enter");
    case Qt::Key_Enter:  return QCoreApplication::translate("InputMap", "Enter (numérico)");
    case Qt::Key_Escape: return QStringLiteral("Esc");
    case Qt::Key_Up:     return QCoreApplication::translate("InputMap", "Seta ↑");
    case Qt::Key_Down:   return QCoreApplication::translate("InputMap", "Seta ↓");
    case Qt::Key_Left:   return QCoreApplication::translate("InputMap", "Seta ←");
    case Qt::Key_Right:  return QCoreApplication::translate("InputMap", "Seta →");
    default: break;
    }
    const QString s = QKeySequence(key).toString(QKeySequence::NativeText);
    return s.isEmpty() ? QCoreApplication::translate("InputMap", "tecla %1").arg(key) : s;
}

QString InputMap::describe(GameAction a) const
{
    const QVector<int> keys = keysFor(a);
    if (keys.isEmpty()) return QCoreApplication::translate("InputMap", "(nenhuma)");
    QStringList nomes;
    for (int k : keys) nomes << keyName(k);
    return nomes.join(QStringLiteral(", "));
}

QJsonObject InputMap::toJson() const
{
    QJsonObject o;
    for (GameAction a : allGameActions()) {
        QJsonArray arr;
        for (int k : keysFor(a)) arr.append(k);
        o[gameActionId(a)] = arr;
    }
    return o;
}

InputMap InputMap::fromJson(const QJsonObject& o)
{
    // Comeca do padrao: acao que o arquivo nao cita continua funcionando.
    InputMap m = defaults();
    for (GameAction a : allGameActions()) {
        const QString id = gameActionId(a);
        if (!o.contains(id)) continue;
        QVector<int> keys;
        for (const QJsonValue& v : o.value(id).toArray())
            if (v.isDouble()) keys.push_back(v.toInt());
        m.setKeys(a, keys);
    }
    return m;
}

} // namespace core
