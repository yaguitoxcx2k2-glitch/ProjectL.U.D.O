// ============================================================================
//  ZipWriter.h — Escritor de arquivos .zip mínimo (método "stored", sem
//  compressão), porte de makeZip()/crc32() da ferramenta web.
//
//  Por que "stored" e não deflate: o conteúdo exportado são PNGs, que já vêm
//  comprimidos — comprimir de novo não ganharia espaço. É exatamente o que a
//  versão web faz, então os ZIPs gerados aqui têm a mesma estrutura.
//
//  Sem dependências externas: usa só QtCore.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace core { namespace zip {

struct Entry {
    QString    name;   ///< caminho dentro do zip (ex.: "chao.png")
    QByteArray data;
};

/// CRC-32 (polinômio 0xEDB88320), igual ao crc32() do JS.
quint32 crc32(const QByteArray& data);

/// Monta o .zip em memória.
QByteArray build(const QVector<Entry>& entries);

/// Monta e grava em disco. Devolve false com a explicação em `error`.
bool write(const QString& path, const QVector<Entry>& entries, QString* error);

}} // namespace core::zip
