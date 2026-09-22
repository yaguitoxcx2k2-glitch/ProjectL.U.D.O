// ============================================================================
// SpriteSheetLayout.h — detecção e metadata compartilhadas de sprites.
//
// RC2.64.1: o seletor visual deixa de assumir que toda imagem em Characters
// possui o mesmo layout. A detecção produz candidatos com confiança/tipo e o
// layout escolhido pode ser persistido como metadata do Asset Database.
// ============================================================================
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace core {

enum class CharacterSpriteKind {
    Unknown,
    SingleCharacter,
    MultiCharacter,
    Object,
    Animation,
    Custom
};

struct CharacterSpriteLayout {
    int frames = 3;
    int directions = 4;
    int charactersX = 1;
    int charactersY = 1;
    CharacterSpriteKind kind = CharacterSpriteKind::Unknown;
    int confidence = 0;              ///< 0..100; >=70 pode ser aplicado automaticamente.
    bool inferred = true;            ///< false quando configurado explicitamente pelo usuário.
    QString reason;                  ///< diagnóstico curto; não é texto de UI obrigatório.

    bool isValid() const
    {
        return frames > 0 && directions > 0 && charactersX > 0 && charactersY > 0;
    }
    int totalColumns() const { return frames * charactersX; }
    int totalRows() const { return directions * charactersY; }
};

QString characterSpriteKindId(CharacterSpriteKind kind);
CharacterSpriteKind characterSpriteKindFromId(const QString& id);

/// Retorna candidatos plausíveis em ordem de preferência. O nome do arquivo é
/// usado apenas como dica de compatibilidade (prefixos $/!, door/object/fire),
/// nunca como única fonte de verdade.
QVector<CharacterSpriteLayout> detectCharacterSpriteLayouts(
    const QImage& image, const QString& fileName = QString());

/// Escolhe o melhor candidato. O frame preferido só desempata resultados de
/// confiança semelhante; nunca deve vencer uma detecção visual muito melhor.
CharacterSpriteLayout preferredCharacterSpriteLayout(
    const QVector<CharacterSpriteLayout>& candidates, int preferredFrames = 3);

/// Garante que a geometria realmente divide a imagem em células inteiras.
bool characterSpriteLayoutFitsImage(const CharacterSpriteLayout& layout, const QImage& image);

/// Formato compacto e compatível para metadata `spriteLayout` do AssetRecord.
QJsonObject characterSpriteLayoutToJson(const CharacterSpriteLayout& layout);
CharacterSpriteLayout characterSpriteLayoutFromJson(const QJsonObject& object);

} // namespace core
