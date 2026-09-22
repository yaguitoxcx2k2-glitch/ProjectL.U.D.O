#include "SpriteSheetLayout.h"

#include "TilesetOps.h"

#include <QFileInfo>

#include <algorithm>

namespace core {
namespace {

CharacterSpriteKind hintedKind(const QString& fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName().toLower();
    if (base.contains(QStringLiteral("fogo")) || base.contains(QStringLiteral("fire")) ||
        base.contains(QStringLiteral("flame")) || base.contains(QStringLiteral("effect")) ||
        base.contains(QStringLiteral("fx")))
        return CharacterSpriteKind::Animation;
    if (base.contains(QStringLiteral("door")) ||
        base.contains(QStringLiteral("porta")) || base.contains(QStringLiteral("object")) ||
        base.contains(QStringLiteral("objeto")) || base.contains(QStringLiteral("chest")) ||
        base.contains(QStringLiteral("bau")))
        return CharacterSpriteKind::Object;
    return CharacterSpriteKind::SingleCharacter;
}

void appendUnique(QVector<CharacterSpriteLayout>& out, CharacterSpriteLayout layout)
{
    if (!layout.isValid()) return;
    layout.confidence = qBound(0, layout.confidence, 100);
    for (CharacterSpriteLayout& existing : out) {
        if (existing.frames == layout.frames &&
            existing.directions == layout.directions &&
            existing.charactersX == layout.charactersX &&
            existing.charactersY == layout.charactersY) {
            if (layout.confidence > existing.confidence) existing = layout;
            return;
        }
    }
    out.push_back(layout);
}

void appendFromDetectedGrid(QVector<CharacterSpriteLayout>& out, const QSize& grid,
                            CharacterSpriteKind hint)
{
    if (!grid.isValid() || grid.width() <= 0 || grid.height() <= 0) return;

    // Grade de um único sprite: aceita 2..16 frames e 4 direções. Isso cobre
    // 3x4, 4x4 e folhas como 8x4 sem transformar tudo em "3 ou 4 frames".
    if (grid.height() == 4 && grid.width() >= 2 && grid.width() <= 16) {
        appendUnique(out, CharacterSpriteLayout{
            grid.width(), 4, 1, 1, hint, 98, true, QStringLiteral("transparent-grid-single")});
    }

    // Folhas multi-personagem tradicionais: cada bloco tem 3x4 ou 4x4.
    if (grid.width() % 3 == 0 && grid.height() % 4 == 0) {
        const int cx = grid.width() / 3;
        const int cy = grid.height() / 4;
        if (cx >= 1 && cy >= 1)
            appendUnique(out, CharacterSpriteLayout{
                3, 4, cx, cy,
                (cx * cy > 1 && hint == CharacterSpriteKind::SingleCharacter)
                    ? CharacterSpriteKind::MultiCharacter : hint,
                94, true, QStringLiteral("transparent-grid-3x4")});
    }
    if (grid.width() % 4 == 0 && grid.height() % 4 == 0) {
        const int cx = grid.width() / 4;
        const int cy = grid.height() / 4;
        if (cx >= 1 && cy >= 1)
            appendUnique(out, CharacterSpriteLayout{
                4, 4, cx, cy,
                (cx * cy > 1 && hint == CharacterSpriteKind::SingleCharacter)
                    ? CharacterSpriteKind::MultiCharacter : hint,
                88, true, QStringLiteral("transparent-grid-4x4")});
    }
}

} // namespace

QString characterSpriteKindId(CharacterSpriteKind kind)
{
    switch (kind) {
    case CharacterSpriteKind::SingleCharacter: return QStringLiteral("single");
    case CharacterSpriteKind::MultiCharacter: return QStringLiteral("multi");
    case CharacterSpriteKind::Object: return QStringLiteral("object");
    case CharacterSpriteKind::Animation: return QStringLiteral("animation");
    case CharacterSpriteKind::Custom: return QStringLiteral("custom");
    case CharacterSpriteKind::Unknown: break;
    }
    return QStringLiteral("unknown");
}

CharacterSpriteKind characterSpriteKindFromId(const QString& id)
{
    const QString value = id.trimmed().toLower();
    if (value == QLatin1String("single")) return CharacterSpriteKind::SingleCharacter;
    if (value == QLatin1String("multi")) return CharacterSpriteKind::MultiCharacter;
    if (value == QLatin1String("object")) return CharacterSpriteKind::Object;
    if (value == QLatin1String("animation")) return CharacterSpriteKind::Animation;
    if (value == QLatin1String("custom")) return CharacterSpriteKind::Custom;
    return CharacterSpriteKind::Unknown;
}

QVector<CharacterSpriteLayout> detectCharacterSpriteLayouts(const QImage& image,
                                                            const QString& fileName)
{
    QVector<CharacterSpriteLayout> out;
    if (image.isNull()) return out;

    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return out;

    const QString base = QFileInfo(fileName).completeBaseName();
    const bool singlePrefix = base.startsWith(QLatin1Char('$'));
    const CharacterSpriteKind hint = hintedKind(fileName);

    // 1) Separadores/transparência reais têm prioridade máxima.
    appendFromDetectedGrid(out, detectCharsetGrid(image), hint);

    // 2) Convenção editores de RPG de `$Nome`: uma única folha 3x4.
    if (singlePrefix && w % 3 == 0 && h % 4 == 0)
        appendUnique(out, CharacterSpriteLayout{
            3, 4, 1, 1, CharacterSpriteKind::SingleCharacter, 100, true,
            QStringLiteral("single-prefix")});

    // 3) Folha quadrada 12x12: formato comum de 4x3 personagens, cada um 3x4.
    // É preferida ao falso 12x8 que pode aparecer quando algumas poses são
    // totalmente transparentes.
    if (w == h && w >= 192 && w % 12 == 0)
        appendUnique(out, CharacterSpriteLayout{
            3, 4, 4, 3, CharacterSpriteKind::MultiCharacter, 97, true,
            QStringLiteral("square-4x3-characters")});

    // 4) Folha larga/alta padrão de 8 personagens (4x2), usada também por
    // portas/objetos. O conteúdo pode ocupar células inteiras e não possuir
    // separadores transparentes, então dimensões + hint são necessários.
    if (w >= 192 && h >= 192 && w % 12 == 0 && h % 8 == 0) {
        CharacterSpriteKind kind = hint;
        if (kind == CharacterSpriteKind::SingleCharacter) kind = CharacterSpriteKind::MultiCharacter;
        appendUnique(out, CharacterSpriteLayout{
            3, 4, 4, 2, kind,
            (hint == CharacterSpriteKind::Object ? 96 : 89), true,
            QStringLiteral("classic-4x2-characters")});
    }

    // 5) Objetos/animações de um único bloco frequentemente usam 4x4.
    if ((hint == CharacterSpriteKind::Object || hint == CharacterSpriteKind::Animation) &&
        w % 4 == 0 && h % 4 == 0)
        appendUnique(out, CharacterSpriteLayout{
            4, 4, 1, 1, hint, 84, true, QStringLiteral("object-animation-4x4")});

    // 6) Candidatos de baixa confiança. Eles permitem visualizar uma grade,
    // mas a UI NÃO os aceita automaticamente: o usuário configura uma vez e
    // o Asset Database passa a lembrar a escolha.
    if (w % 3 == 0 && h % 4 == 0)
        appendUnique(out, CharacterSpriteLayout{
            3, 4, 1, 1, CharacterSpriteKind::Unknown, 55, true,
            QStringLiteral("fallback-3x4")});
    if (w % 4 == 0 && h % 4 == 0)
        appendUnique(out, CharacterSpriteLayout{
            4, 4, 1, 1, CharacterSpriteKind::Unknown, 52, true,
            QStringLiteral("fallback-4x4")});

    if (out.isEmpty())
        appendUnique(out, CharacterSpriteLayout{
            1, 1, 1, 1, CharacterSpriteKind::Unknown, 10, true,
            QStringLiteral("unresolved")});

    std::sort(out.begin(), out.end(), [](const CharacterSpriteLayout& a,
                                         const CharacterSpriteLayout& b) {
        return a.confidence > b.confidence;
    });
    return out;
}

CharacterSpriteLayout preferredCharacterSpriteLayout(
    const QVector<CharacterSpriteLayout>& candidates, int preferredFrames)
{
    if (candidates.isEmpty()) return CharacterSpriteLayout{};
    int bestIndex = 0;
    int bestScore = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        const CharacterSpriteLayout& candidate = candidates.at(i);
        const int score = candidate.confidence + (candidate.frames == preferredFrames ? 4 : 0);
        if (score > bestScore) { bestScore = score; bestIndex = i; }
    }
    return candidates.at(bestIndex);
}

bool characterSpriteLayoutFitsImage(const CharacterSpriteLayout& layout, const QImage& image)
{
    if (!layout.isValid() || image.isNull()) return false;
    const int cols = layout.totalColumns();
    const int rows = layout.totalRows();
    return cols > 0 && rows > 0 && image.width() % cols == 0 && image.height() % rows == 0;
}

QJsonObject characterSpriteLayoutToJson(const CharacterSpriteLayout& layout)
{
    QJsonObject object;
    object.insert(QStringLiteral("frames"), layout.frames);
    object.insert(QStringLiteral("directions"), layout.directions);
    object.insert(QStringLiteral("charactersX"), layout.charactersX);
    object.insert(QStringLiteral("charactersY"), layout.charactersY);
    object.insert(QStringLiteral("kind"), characterSpriteKindId(layout.kind));
    object.insert(QStringLiteral("confidence"), layout.confidence);
    object.insert(QStringLiteral("inferred"), layout.inferred);
    if (!layout.reason.isEmpty()) object.insert(QStringLiteral("reason"), layout.reason);
    return object;
}

CharacterSpriteLayout characterSpriteLayoutFromJson(const QJsonObject& object)
{
    CharacterSpriteLayout layout;
    layout.frames = qMax(1, object.value(QStringLiteral("frames")).toInt(3));
    layout.directions = qMax(1, object.value(QStringLiteral("directions")).toInt(4));
    layout.charactersX = qMax(1, object.value(QStringLiteral("charactersX")).toInt(1));
    layout.charactersY = qMax(1, object.value(QStringLiteral("charactersY")).toInt(1));
    layout.kind = characterSpriteKindFromId(object.value(QStringLiteral("kind")).toString());
    layout.confidence = qBound(0, object.value(QStringLiteral("confidence")).toInt(100), 100);
    layout.inferred = object.value(QStringLiteral("inferred")).toBool(false);
    layout.reason = object.value(QStringLiteral("reason")).toString();
    return layout;
}

} // namespace core
