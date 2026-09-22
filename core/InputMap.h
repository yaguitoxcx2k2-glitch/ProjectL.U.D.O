// ============================================================================
//  InputMap.h — Mapa de teclas do runtime, gravado no projeto.
//
//  Cada ACAO do jogo (andar, confirmar, cancelar...) aponta para uma lista de
//  teclas. A lista permite o que o jogador espera: setas E WASD andando, Z E
//  Enter E Espaco confirmando, sem precisar escolher um so.
//
//  Fica no nucleo (e nao no runtime) porque tambem e editado pelo editor e
//  salvo/lido junto com o resto do projeto.
// ============================================================================
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace core {

/// Acoes que o jogo entende. Guardadas no arquivo pelo NOME (ver
/// gameActionId), nunca pelo numero — assim dá para reordenar sem quebrar
/// projetos salvos.
enum class GameAction {
    Up, Down, Left, Right,   ///< andar
    Confirm,                 ///< falar com evento / avancar mensagem
    Cancel,                  ///< cancelar (menus, adiante)
    ToggleHud,               ///< mostra/esconde o HUD
    ZoomIn, ZoomOut,         ///< zoom da janela de teste
    QuickSave, QuickLoad,    ///< atalhos opcionais de persistência
    SkipCutscene,            ///< Ludo Cutscene Skip
    Action1, Action2, Action3, Action4, ///< ações de gameplay livres para projetos No-Code
    Quit                     ///< sair do teste
};

QVector<GameAction> allGameActions();
QString      gameActionId(GameAction a);
GameAction   gameActionFromId(const QString& id, bool* ok = nullptr);
QString      gameActionLabel(GameAction a);

/// Mapa acao -> teclas (valores de Qt::Key).
class InputMap
{
public:
    /// Padrao de fabrica: setas + WASD, Z/Enter/Espaco confirmam, X/Esc cancela.
    static InputMap defaults();

    QVector<int> keysFor(GameAction a) const;
    void setKeys(GameAction a, const QVector<int>& keys);
    void addKey(GameAction a, int key);
    void clear(GameAction a);

    /// Esta tecla dispara esta acao?
    bool matches(GameAction a, int key) const;
    /// Alguma outra acao ja usa esta tecla? (devolve a acao ou nullptr)
    bool findConflict(GameAction ignore, int key, GameAction* other) const;

    /// Texto legivel das teclas ("Z, Enter, Espaço").
    QString describe(GameAction a) const;
    /// Nome legivel de uma tecla so.
    static QString keyName(int key);

    QJsonObject toJson() const;
    static InputMap fromJson(const QJsonObject& o);

    bool operator==(const InputMap& other) const { return m_keys == other.m_keys; }
    bool operator!=(const InputMap& other) const { return !(*this == other); }

private:
    QHash<int, QVector<int>> m_keys;   ///< int(GameAction) -> teclas
};

} // namespace core
