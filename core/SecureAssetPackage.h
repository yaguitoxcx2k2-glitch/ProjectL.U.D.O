// ============================================================================
// SecureAssetPackage.h — pacote de Assets usado pela exportação protegida.
//
// Na exportação segura os arquivos de Assets/ não ficam soltos na pasta final:
// eles são serializados num único game.assets e o bloco inteiro recebe a mesma
// proteção autenticada LUDOCRYPT2 usada pelo game.ludo. Desde a 3.27.0 o
// container interno LUDOASSET2 inclui hash por entrada e validação defensiva de
// caminhos; o leitor mantém compatibilidade com LUDOASSET1. O LudoPlayer extrai
// o conteúdo apenas para uma pasta temporária durante a execução.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace core { namespace secure_assets {

struct Entry {
    QString relativePath; // sempre relativo ao projeto, ex.: Assets/BGM/theme.ogg
    QByteArray data;
};

QByteArray build(const QVector<Entry>& entries, QString* error = nullptr);
bool write(const QString& packagePath, const QVector<Entry>& entries, QString* error = nullptr);
bool extract(const QString& packagePath, const QString& destinationRoot, QString* error = nullptr);

}} // namespace core::secure_assets
