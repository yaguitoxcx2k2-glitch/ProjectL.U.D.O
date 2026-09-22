#include "ZipWriter.h"

#include <QFile>
#include <QObject>

namespace core { namespace zip {

quint32 crc32(const QByteArray& data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    for (int i = 0; i < data.size(); ++i) crc = (crc >> 8) ^ table[(crc ^ p[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

// --- helpers de escrita little-endian ---------------------------------------
static void put16(QByteArray& out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}
static void put32(QByteArray& out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

QByteArray build(const QVector<Entry>& entries)
{
    QByteArray local;      // cabeçalhos locais + dados
    QByteArray central;    // diretório central
    quint32 offset = 0;

    // 0x0021 = 1980-01-01 no formato de data do MS-DOS (mesmo valor do JS).
    const quint16 kDosDate = 0x21;

    for (const Entry& e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 crc = crc32(e.data);
        const quint32 size = quint32(e.data.size());

        // -------- local file header (30 bytes + nome) --------
        put32(local, 0x04034b50);      // assinatura
        put16(local, 20);              // versão necessária
        put16(local, 0);               // flags
        put16(local, 0);               // método: 0 = stored
        put16(local, 0);               // hora
        put16(local, kDosDate);        // data
        put32(local, crc);
        put32(local, size);            // tamanho comprimido
        put32(local, size);            // tamanho original
        put16(local, quint16(name.size()));
        put16(local, 0);               // extra field
        local.append(name);
        local.append(e.data);

        // -------- central directory header (46 bytes + nome) --------
        put32(central, 0x02014b50);
        put16(central, 20);            // versão de criação
        put16(central, 20);            // versão necessária
        put16(central, 0);             // flags
        put16(central, 0);             // método
        put16(central, 0);             // hora
        put16(central, kDosDate);      // data
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, quint16(name.size()));
        put16(central, 0);             // extra
        put16(central, 0);             // comentário
        put16(central, 0);             // disco inicial
        put16(central, 0);             // atributos internos
        put32(central, 0);             // atributos externos
        put32(central, offset);        // deslocamento do cabeçalho local
        central.append(name);

        offset += 30 + quint32(name.size()) + size;
    }

    QByteArray eocd;
    put32(eocd, 0x06054b50);
    put16(eocd, 0);                                  // número do disco
    put16(eocd, 0);                                  // disco do diretório central
    put16(eocd, quint16(entries.size()));            // entradas neste disco
    put16(eocd, quint16(entries.size()));            // entradas no total
    put32(eocd, quint32(central.size()));            // tamanho do diretório central
    put32(eocd, offset);                             // deslocamento do diretório central
    put16(eocd, 0);                                  // comentário

    return local + central + eocd;
}

bool write(const QString& path, const QVector<Entry>& entries, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QObject::tr("Não foi possível escrever em %1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray data = build(entries);
    if (f.write(data) != data.size()) {
        if (error) *error = QObject::tr("Falha ao gravar o arquivo %1.").arg(path);
        return false;
    }
    f.close();
    return true;
}

}} // namespace core::zip
