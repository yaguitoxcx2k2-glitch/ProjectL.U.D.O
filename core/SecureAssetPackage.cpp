#include "SecureAssetPackage.h"
#include "ProjectIO.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QSet>

namespace core { namespace secure_assets {
namespace {

const QByteArray kAssetMagicV1("LUDOASSET1\n");
const QByteArray kAssetMagicV2("LUDOASSET2\n");
constexpr quint32 kMaxEntries = 100000;
constexpr quint32 kMaxPathBytes = 4096;
constexpr quint64 kMaxSingleAssetBytes = quint64(4) * 1024 * 1024 * 1024;

void put32(QByteArray& out, quint32 value)
{
    out.append(char(value)); out.append(char(value >> 8)); out.append(char(value >> 16)); out.append(char(value >> 24));
}
void put64(QByteArray& out, quint64 value)
{
    for (int shift=0; shift<64; shift+=8) out.append(char(value >> shift));
}
bool take32(const QByteArray& data, qsizetype& pos, quint32& value)
{
    if(pos<0||pos+4>data.size())return false;const auto*p=reinterpret_cast<const uchar*>(data.constData()+pos);
    value=quint32(p[0])|(quint32(p[1])<<8)|(quint32(p[2])<<16)|(quint32(p[3])<<24);pos+=4;return true;
}
bool take64(const QByteArray& data, qsizetype& pos, quint64& value)
{
    if(pos<0||pos+8>data.size())return false;const auto*p=reinterpret_cast<const uchar*>(data.constData()+pos);value=0;
    for(int shift=0;shift<64;shift+=8)value|=quint64(p[shift/8])<<shift;pos+=8;return true;
}
QString normalizedRelative(QString path)
{
    path=QDir::cleanPath(QDir::fromNativeSeparators(path));while(path.startsWith(QLatin1String("./")))path.remove(0,2);return path;
}
bool safeAssetPath(const QString& input, QString* normalized = nullptr)
{
    const QString path=normalizedRelative(input);
    if(path.isEmpty()||QDir::isAbsolutePath(path)||path==QLatin1String(".")||path==QLatin1String("..")||
       path.startsWith(QLatin1String("../"))||path.contains(QLatin1String("/../"))||
       !path.startsWith(QLatin1String("Assets/"),Qt::CaseInsensitive)||path.contains(QLatin1Char(':')))return false;
    if(normalized)*normalized=path;return true;
}
QByteArray entryHash(const QString& relative,const QByteArray& data)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(relative.toUtf8());
    hash.addData(QByteArray(1, '\0'));
    hash.addData(data);
    return hash.result();
}

} // namespace

QByteArray build(const QVector<Entry>& entries, QString* error)
{
    if(entries.size()>int(kMaxEntries)){if(error)*error=QObject::tr("Há assets demais no pacote protegido.");return {};}
    QByteArray payload=kAssetMagicV2;put32(payload,quint32(entries.size()));QSet<QString> seen;
    for(const Entry& entry:entries){
        QString relative;if(!safeAssetPath(entry.relativePath,&relative)){if(error)*error=QObject::tr("Caminho de asset inválido no pacote protegido: %1").arg(entry.relativePath);return {};}
        const QString key=relative.toLower();if(seen.contains(key)){if(error)*error=QObject::tr("Asset duplicado no pacote protegido: %1").arg(relative);return {};}seen.insert(key);
        const QByteArray pathBytes=relative.toUtf8();if(pathBytes.size()>int(kMaxPathBytes)||quint64(entry.data.size())>kMaxSingleAssetBytes){if(error)*error=QObject::tr("Asset excede os limites seguros do pacote: %1").arg(relative);return {};}
        const QByteArray hash=entryHash(relative,entry.data);
        put32(payload,quint32(pathBytes.size()));put64(payload,quint64(entry.data.size()));payload.append(hash);payload.append(pathBytes);payload.append(entry.data);
    }
    return core::io::protectProjectPayload(payload);
}

bool write(const QString& packagePath,const QVector<Entry>& entries,QString* error)
{
    const QByteArray bytes=build(entries,error);if(bytes.isEmpty()){if(error&&error->isEmpty())*error=QObject::tr("Não foi possível montar o pacote protegido de Assets.");return false;}
    QSaveFile file(packagePath);if(!file.open(QIODevice::WriteOnly)){if(error)*error=QObject::tr("Não foi possível criar %1: %2").arg(packagePath,file.errorString());return false;}
    if(file.write(bytes)!=bytes.size()||!file.commit()){if(error)*error=QObject::tr("Não foi possível concluir a gravação de %1.").arg(packagePath);return false;}return true;
}

bool extract(const QString& packagePath,const QString& destinationRoot,QString* error)
{
    QFile file(packagePath);if(!file.open(QIODevice::ReadOnly)){if(error)*error=QObject::tr("Não foi possível abrir o pacote de Assets %1: %2").arg(packagePath,file.errorString());return false;}
    const QByteArray payload=core::io::unprotectProjectPayload(file.readAll());const bool v2=payload.startsWith(kAssetMagicV2);const bool v1=payload.startsWith(kAssetMagicV1);
    if(!v2&&!v1){if(error)*error=QObject::tr("Pacote de Assets protegido inválido ou adulterado.");return false;}
    qsizetype pos=v2?kAssetMagicV2.size():kAssetMagicV1.size();quint32 count=0;if(!take32(payload,pos,count)||count>kMaxEntries){if(error)*error=QObject::tr("Cabeçalho do pacote de Assets é inválido.");return false;}
    const QString rootAbs=QDir(destinationRoot).absolutePath();QSet<QString> seen;
    for(quint32 index=0;index<count;++index){
        quint32 pathSize=0;quint64 dataSize=0;if(!take32(payload,pos,pathSize)||!take64(payload,pos,dataSize)||pathSize==0||pathSize>kMaxPathBytes||dataSize>kMaxSingleAssetBytes){if(error)*error=QObject::tr("Pacote de Assets contém tamanho inválido na entrada %1.").arg(index+1);return false;}
        QByteArray expectedHash;if(v2){if(pos+32>payload.size()){if(error)*error=QObject::tr("Pacote de Assets está incompleto na entrada %1.").arg(index+1);return false;}expectedHash=payload.mid(pos,32);pos+=32;}
        if(quint64(payload.size()-pos)<quint64(pathSize)+dataSize){if(error)*error=QObject::tr("Pacote de Assets está incompleto na entrada %1.").arg(index+1);return false;}
        const QString raw=QString::fromUtf8(payload.constData()+pos,int(pathSize));pos+=pathSize;QString relative;
        if(!safeAssetPath(raw,&relative)){if(error)*error=QObject::tr("Pacote de Assets contém caminho inseguro: %1").arg(raw);return false;}
        const QString key=relative.toLower();if(seen.contains(key)){if(error)*error=QObject::tr("Pacote de Assets contém caminho duplicado: %1").arg(relative);return false;}seen.insert(key);
        const QByteArray bytes=payload.mid(pos,qsizetype(dataSize));pos+=qsizetype(dataSize);
        if(v2&&expectedHash!=entryHash(relative,bytes)){if(error)*error=QObject::tr("Integridade do asset falhou: %1").arg(relative);return false;}
        const QString outputPath=QDir(rootAbs).absoluteFilePath(relative);const QString cleanOutput=QDir::cleanPath(outputPath);
        if(!(cleanOutput==rootAbs||cleanOutput.startsWith(rootAbs+QLatin1Char('/')))){if(error)*error=QObject::tr("Tentativa de extração fora da pasta temporária foi bloqueada.");return false;}
        if(!QDir().mkpath(QFileInfo(cleanOutput).absolutePath())){if(error)*error=QObject::tr("Não foi possível criar a pasta para %1.").arg(relative);return false;}
        QSaveFile output(cleanOutput);if(!output.open(QIODevice::WriteOnly)){if(error)*error=QObject::tr("Não foi possível extrair %1: %2").arg(relative,output.errorString());return false;}
        if(output.write(bytes)!=bytes.size()||!output.commit()){if(error)*error=QObject::tr("Falha ao extrair %1.").arg(relative);return false;}
    }
    if(pos!=payload.size()){if(error)*error=QObject::tr("Pacote de Assets contém dados extras inesperados.");return false;}
    return true;
}

}} // namespace core::secure_assets
