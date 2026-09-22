#include "core/Editor.h"
#include "core/GameData.h"
#include "core/LayerTree.h"
#include "core/ProjectIO.h"
#include "core/Renderer.h"
#include "core/FilterSystem.h"

#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QJsonDocument>
#include <QSet>
#include <QTextStream>

namespace {
constexpr quint32 kVersion = 8;
constexpr quint32 kEventsVersion = 5;
constexpr char kMagic[8] = {'L','U','D','O','S','W','1','\0'};
constexpr char kEventsMagic[8] = {'L','U','D','O','E','5','\0','\0'};

enum SwitchOpcode : qint32 {
    OpNone = 0,
    OpMapTransfer = 1,
    OpSwitchSet = 2,
    OpSelfSwitchSet = 3,
    OpVariableSet = 4,
    OpFlowExit = 5,
    OpWait = 6,
    OpJump = 7,
    OpJumpIfFalse = 8,
    OpRepeatBegin = 9,
    OpRepeatEnd = 10,
    OpCommonCall = 11,
    OpMapEventCall = 12,
    OpWaitUntil = 13,
    OpCommonReserve = 14,
    OpMessage = 15,
    OpChoice = 16,
    OpSubtitle = 17,
    OpStringSet = 18,
    OpValueGet = 19,
    OpScreenTone = 20,
    OpScreenFade = 21,
    OpScreenFlash = 22,
    OpWeather = 23,
    OpAudioPlay = 24,
    OpAudioStop = 25,
    OpUiOpen = 26,
    OpFilterSet = 27,
    OpFilterClear = 28,
    OpScreenShake = 29,
    OpCameraZoom = 30,
    OpCameraReset = 31
};

enum ValueMode : qint32 { ValueOff = 0, ValueOn = 1, ValueToggle = 2 };
enum VariableOp : qint32 { VarAssign = 0, VarAdd, VarSub, VarMul, VarDiv, VarMod, VarMin, VarMax, VarAbs };
enum NumberSource : qint32 { SourceConstant = 0, SourceVariable = 1, SourceRandom = 2 };
enum ConditionKind : qint32 { CondSwitch = 0, CondSelfSwitch = 1, CondVariable = 2, CondRandom = 3 };

struct PackedCommand {
    qint32 opcode = OpNone;
    qint32 a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0;
};

bool writeImageRaw(const QString& path, const QImage& source, QString* error);

struct StringPool {
    QVector<QByteArray> values;
    QHash<QString,int> index;
    int add(const QString& value) {
        const QString key=value;
        auto it=index.constFind(key); if(it!=index.cend()) return it.value();
        const int i=values.size(); values.push_back(value.toLatin1()); index.insert(key,i); return i;
    }
};

bool writeTextResources(const QString& path,const QString& fontPath,const core::Editor& ed,const StringPool& pool,QString* error)
{
    QFile file(path); if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){if(error)*error=QStringLiteral("Não foi possível criar %1").arg(path);return false;}
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian); out.writeRawData("LUDOTX1\0",8); out<<quint32(1)<<quint32(ed.strings.size())<<quint32(pool.values.size());
    for(const core::StringDef& def:ed.strings){const QByteArray b=def.initial.toLatin1();out<<qint32(def.id)<<quint32(b.size());if(!b.isEmpty())out.writeRawData(b.constData(),b.size());}
    for(const QByteArray& b:pool.values){out<<quint32(b.size());if(!b.isEmpty())out.writeRawData(b.constData(),b.size());}
    if(out.status()!=QDataStream::Ok){if(error)*error=QStringLiteral("Falha ao gravar textos do Switch.");return false;}
    QImage atlas(16*8,16*12,QImage::Format_RGBA8888); atlas.fill(Qt::transparent); QPainter painter(&atlas); QFont font(QStringLiteral("DejaVu Sans Mono"),8); font.setPixelSize(10); painter.setFont(font); painter.setPen(Qt::white);
    for(int c=0;c<256;++c){const int gx=(c%16)*8,gy=(c/16)*12;const QString ch=QString::fromLatin1(QByteArray(1,char(c)));painter.drawText(QRect(gx,gy,8,12),Qt::AlignCenter,ch);} painter.end();
    return writeImageRaw(fontPath,atlas,error);
}

bool writeImageRaw(const QString& path, const QImage& source, QString* error)
{
    if (source.isNull()) return true;
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Não foi possível criar %1").arg(path); return false;
    }
    const qsizetype rowBytes = qsizetype(image.width()) * 4;
    for (int y = 0; y < image.height(); ++y)
        if (file.write(reinterpret_cast<const char*>(image.constScanLine(y)), rowBytes) != rowBytes) {
            if (error) *error = QStringLiteral("Falha ao gravar %1").arg(path); return false;
        }
    return true;
}

QByteArray buildCollisionGrid(const core::Editor& ed, const core::MapDoc& doc)
{
    const int width=qMax(0,doc.map.width),height=qMax(0,doc.map.height);QByteArray grid(width*height,char(0));
    if(width<=0||height<=0)return grid;const int baseW=qMax(1,doc.map.tileWidth),baseH=qMax(1,doc.map.tileHeight);
    const QVector<core::LayerPtr> layers=core::flattenRenderableLayers(doc.layers);
    for(int gy=0;gy<height;++gy)for(int gx=0;gx<width;++gx){const int px0=gx*baseW,py0=gy*baseH;int mask=0;
        for(const core::LayerPtr& layer:layers){if(!layer||!layer->visible||layer->type!=core::LayerType::Tile)continue;const int lw=qMax(1,layer->tileWidth),lh=qMax(1,layer->tileHeight);
            const int lx0=px0/lw,ly0=py0/lh,lx1=(px0+baseW-1)/lw,ly1=(py0+baseH-1)/lh;
            for(int ly=ly0;ly<=ly1;++ly)for(int lx=lx0;lx<=lx1;++lx){if(!layer->inBounds(lx,ly))continue;const core::Cell& cell=layer->data2D[ly][lx];for(const core::TileRef& tile:cell)mask|=ed.collisionMask(tile.tilesetIdx,tile.tx,tile.ty);}}
        grid[gy*width+gx]=char(mask&int(core::Editor::SideAll));}
    return grid;
}

bool writeBytes(const QString& path,const QByteArray& data,QString* error){QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){if(error)*error=QStringLiteral("Não foi possível criar %1").arg(path);return false;}if(file.write(data)!=data.size()){if(error)*error=QStringLiteral("Falha ao gravar %1").arg(path);return false;}return true;}
QImage playerCharset(const core::Editor& ed){return ed.player.hasCharset()?ed.player.charset:QImage();}
QImage eventGraphicFrame(const core::Editor& ed,const core::EventGraphic& graphic){if(graphic.kind==core::EventGraphic::Charset&&!graphic.charset.isNull()){const QRect source=graphic.charsetFrameRect();return source.isValid()?graphic.charset.copy(source):QImage();}if(graphic.kind==core::EventGraphic::Tile&&graphic.tile.isValid()&&graphic.tile.tilesetIdx>=0&&graphic.tile.tilesetIdx<ed.tilesets.size()){const core::Tileset& ts=ed.tilesets.at(graphic.tile.tilesetIdx);if(ts.image.isNull()||!ts.contains(graphic.tile.tx,graphic.tile.ty))return {};const int x=ts.margin+graphic.tile.tx*(ts.tilewidth+ts.spacing),y=ts.margin+graphic.tile.ty*(ts.tileheight+ts.spacing);return ts.image.copy(x,y,ts.tilewidth,ts.tileheight);}return {};}
qint32 triggerCode(core::EventTrigger trigger){switch(trigger){case core::EventTrigger::ActionKey:return 0;case core::EventTrigger::PlayerTouch:return 1;case core::EventTrigger::EventTouch:return 2;case core::EventTrigger::DirectionalSensor:return 3;case core::EventTrigger::Autorun:return 4;case core::EventTrigger::Parallel:return 5;}return 0;}
qint32 variableCompareCode(const QString& op){if(op==QLatin1String("=="))return 0;if(op==QLatin1String("!="))return 1;if(op==QLatin1String(">"))return 2;if(op==QLatin1String(">="))return 3;if(op==QLatin1String("<"))return 4;if(op==QLatin1String("<="))return 5;return 3;}
qint32 valueMode(const QVariantMap& p){const QString value=p.value(QStringLiteral("value"),QStringLiteral("on")).toString();if(value==QLatin1String("toggle"))return ValueToggle;if(value==QLatin1String("off"))return ValueOff;const QVariantMap spec=p.value(QStringLiteral("sourceSpec")).toMap();if(!spec.isEmpty()&&spec.value(QStringLiteral("source")).toString()==QLatin1String("constant"))return spec.value(QStringLiteral("value"),true).toBool()?ValueOn:ValueOff;return ValueOn;}
qint32 variableOpCode(const QString& op){if(op==QLatin1String("+"))return VarAdd;if(op==QLatin1String("-"))return VarSub;if(op==QLatin1String("*"))return VarMul;if(op==QLatin1String("/"))return VarDiv;if(op==QLatin1String("%"))return VarMod;if(op==QLatin1String("min"))return VarMin;if(op==QLatin1String("max"))return VarMax;if(op==QLatin1String("abs"))return VarAbs;return VarAssign;}

bool numberSource(const QVariantMap& p,qint32* mode,qint32* v1,qint32* v2)
{
    QVariantMap spec=p.value(QStringLiteral("sourceSpec")).toMap();
    if(spec.isEmpty()){const QString legacy=p.value(QStringLiteral("source"),QStringLiteral("const")).toString();if(legacy==QLatin1String("variable"))spec={{QStringLiteral("source"),QStringLiteral("variable")},{QStringLiteral("variableId"),p.value(QStringLiteral("valueVariable"),1)}};else spec={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),p.value(QStringLiteral("value"),0)}};}
    const QString src=spec.value(QStringLiteral("source"),QStringLiteral("constant")).toString();
    if(src==QLatin1String("constant")){*mode=SourceConstant;*v1=spec.value(QStringLiteral("value"),0).toInt();*v2=0;return true;}
    if(src==QLatin1String("variable")){*mode=SourceVariable;*v1=spec.value(QStringLiteral("variableId"),1).toInt();*v2=0;return true;}
    if(src==QLatin1String("random")){*mode=SourceRandom;*v1=spec.value(QStringLiteral("minimum"),0).toInt();*v2=spec.value(QStringLiteral("maximum"),0).toInt();return true;}
    return false;
}

bool packCondition(const QVariantMap& p,PackedCommand* packed,QStringList* unsupported)
{
    const QString kind=p.value(QStringLiteral("kind"),QStringLiteral("switch")).toString();
    if(!p.value(QStringLiteral("conditionTree")).toMap().isEmpty()){if(unsupported)unsupported->push_back(QStringLiteral("if (condição combinada)"));return false;}
    if(kind==QLatin1String("switch")){packed->a=CondSwitch;packed->b=p.value(QStringLiteral("id"),1).toInt();packed->c=p.value(QStringLiteral("value"),true).toBool()?1:0;return true;}
    if(kind==QLatin1String("selfSwitch")){const QString l=p.value(QStringLiteral("letter"),QStringLiteral("A")).toString().toUpper();packed->a=CondSelfSwitch;packed->b=qBound(0,l.isEmpty()?0:l.at(0).unicode()-QChar('A').unicode(),3);packed->c=p.value(QStringLiteral("value"),true).toBool()?1:0;return true;}
    if(kind==QLatin1String("variable")){packed->a=CondVariable;packed->b=p.value(QStringLiteral("id"),1).toInt();packed->c=variableCompareCode(p.value(QStringLiteral("op"),QStringLiteral(">=")).toString());QVariantMap right=p.value(QStringLiteral("rightSpec")).toMap();if(right.isEmpty()){const QString src=p.value(QStringLiteral("source")).toString();if(src==QLatin1String("variable"))right={{QStringLiteral("source"),QStringLiteral("variable")},{QStringLiteral("variableId"),p.value(QStringLiteral("valueVariable"),1)}};else right={{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),p.value(QStringLiteral("value"),0)}};}const QString src=right.value(QStringLiteral("source"),QStringLiteral("constant")).toString();if(src==QLatin1String("constant")){packed->d=SourceConstant;packed->e=right.value(QStringLiteral("value"),0).toInt();return true;}if(src==QLatin1String("variable")){packed->d=SourceVariable;packed->e=right.value(QStringLiteral("variableId"),1).toInt();return true;}if(unsupported)unsupported->push_back(QStringLiteral("if (fonte de variável avançada)"));return false;}
    if(kind==QLatin1String("random")){packed->a=CondRandom;packed->b=qBound(0,p.value(QStringLiteral("chance"),50).toInt(),100);return true;}
    if(unsupported)unsupported->push_back(QStringLiteral("if (%1)").arg(kind));return false;
}


qint32 audioChannelCode(const QString& channel){if(channel==QLatin1String("bgm"))return 0;if(channel==QLatin1String("bgs"))return 1;if(channel==QLatin1String("me"))return 2;if(channel==QLatin1String("se"))return 3;if(channel==QLatin1String("voice"))return 4;return 3;}
qint32 filterTypeCode(const QString& type){if(type.endsWith(QLatin1String("chromaticAberration")))return 0;if(type.endsWith(QLatin1String("noise")))return 1;if(type.endsWith(QLatin1String("scanlines")))return 2;if(type.endsWith(QLatin1String("vignette")))return 3;if(type.endsWith(QLatin1String("blur")))return 4;if(type.endsWith(QLatin1String("tiltShift")))return 5;return -1;}
qint32 scopeMask(const QVariantMap& p){int mask=0;if(p.value(QStringLiteral("affectWorld"),true).toBool())mask|=1;if(p.value(QStringLiteral("affectPictures"),false).toBool())mask|=2;if(p.value(QStringLiteral("affectHud"),false).toBool())mask|=4;return mask;}

QVector<PackedCommand> packCommands(const core::Editor& ed,int mapIndex,const core::MapEvent* event,
                                    const QVector<core::EventCommand>& commands,const QHash<QString,int>& commonById,
                                    const QHash<int,int>& commonByNumber,const QHash<QString,int>& eventKeys,
                                    StringPool* strings, QStringList* unsupported)
{
    QVector<PackedCommand> out(commands.size());
    QHash<QString,int> labels;for(int i=0;i<commands.size();++i)if(commands.at(i).type==QLatin1String("label"))labels.insert(commands.at(i).params.value(QStringLiteral("name")).toString(),i);
    QVector<int> ifStack,elseStack,loopStack,repeatStack;
    QHash<int,int> ifElse,ifEnd,elseEnd,loopEnd,repeatEnd;
    for(int i=0;i<commands.size();++i){const QString t=commands.at(i).type;if(t==QLatin1String("if"))ifStack.push_back(i);else if(t==QLatin1String("else")&&!ifStack.isEmpty()){ifElse[ifStack.last()]=i;elseStack.push_back(i);}else if(t==QLatin1String("endIf")&&!ifStack.isEmpty()){const int b=ifStack.takeLast();ifEnd[b]=i;if(!elseStack.isEmpty()&&elseStack.last()>b)elseEnd[elseStack.takeLast()]=i;}else if(t==QLatin1String("loop.begin"))loopStack.push_back(i);else if(t==QLatin1String("loop.end")&&!loopStack.isEmpty()){const int b=loopStack.takeLast();loopEnd[b]=i;}else if(t==QLatin1String("repeat.begin"))repeatStack.push_back(i);else if(t==QLatin1String("repeat.end")&&!repeatStack.isEmpty()){const int b=repeatStack.takeLast();repeatEnd[b]=i;}}
    for(int i=0;i<commands.size();++i){const core::EventCommand& command=commands.at(i);const QVariantMap& p=command.params;PackedCommand pc;
        if(command.type==QLatin1String("map.transfer")){const int mi=ed.mapIndexById(p.value(QStringLiteral("mapId")).toString());if(mi<0){if(unsupported)unsupported->push_back(command.type+QStringLiteral(" (mapa inválido)"));}else{QPoint target(p.value(QStringLiteral("x")).toInt(),p.value(QStringLiteral("y")).toInt());if(p.value(QStringLiteral("useSpawn"),false).toBool()&&ed.docs.at(mi).hasSpawn)target=ed.docs.at(mi).spawn;pc.opcode=OpMapTransfer;pc.a=mi;pc.b=target.x();pc.c=target.y();pc.d=qBound(0,p.value(QStringLiteral("direction"),0).toInt(),7);}}
        else if(command.type==QLatin1String("switch.set")){pc.opcode=OpSwitchSet;pc.a=p.value(QStringLiteral("id"),1).toInt();pc.b=valueMode(p);}
        else if(command.type==QLatin1String("selfSwitch.set")){pc.opcode=OpSelfSwitchSet;const QString l=p.value(QStringLiteral("letter"),QStringLiteral("A")).toString().toUpper();pc.a=qBound(0,l.isEmpty()?0:l.at(0).unicode()-QChar('A').unicode(),3);pc.b=valueMode(p);}
        else if(command.type==QLatin1String("variable.set")){qint32 mode=0,v1=0,v2=0;if(numberSource(p,&mode,&v1,&v2)){pc.opcode=OpVariableSet;pc.a=p.value(QStringLiteral("id"),1).toInt();pc.b=qMax(pc.a,p.value(QStringLiteral("rangeEndId"),pc.a).toInt());pc.c=variableOpCode(p.value(QStringLiteral("op"),QStringLiteral("=")).toString());pc.d=mode;pc.e=v1;pc.f=v2;}else if(unsupported)unsupported->push_back(command.type+QStringLiteral(" (fonte avançada)"));}
        else if(command.type==QLatin1String("if")){pc.opcode=OpJumpIfFalse;if(packCondition(p,&pc,unsupported)){const int target=ifElse.contains(i)?ifElse.value(i)+1:ifEnd.value(i,i)+1;pc.h=target;}else pc.opcode=OpNone;}
        else if(command.type==QLatin1String("else")){pc.opcode=OpJump;pc.a=elseEnd.value(i,i)+1;}
        else if(command.type==QLatin1String("loop.end")){int begin=-1;for(auto it=loopEnd.cbegin();it!=loopEnd.cend();++it)if(it.value()==i){begin=it.key();break;}if(begin>=0){pc.opcode=OpJump;pc.a=begin+1;}}
        else if(command.type==QLatin1String("loop.break")){int target=commands.size();for(auto it=loopEnd.cbegin();it!=loopEnd.cend();++it)if(it.key()<i&&it.value()>i&&it.value()+1<target)target=it.value()+1;pc.opcode=OpJump;pc.a=target;}
        else if(command.type==QLatin1String("repeat.begin")){qint32 mode=SourceConstant,v1=p.value(QStringLiteral("count"),1).toInt(),v2=0;QVariantMap fake;fake[QStringLiteral("sourceSpec")]=p.value(QStringLiteral("countSpec"));fake[QStringLiteral("value")]=p.value(QStringLiteral("count"),1);if(!numberSource(fake,&mode,&v1,&v2)){mode=SourceConstant;v1=0;if(unsupported)unsupported->push_back(QStringLiteral("repeat.begin (fonte avançada)"));}pc.opcode=OpRepeatBegin;pc.a=repeatEnd.value(i,i)+1;pc.b=mode;pc.c=v1;pc.d=v2;}
        else if(command.type==QLatin1String("repeat.end")){int begin=-1;for(auto it=repeatEnd.cbegin();it!=repeatEnd.cend();++it)if(it.value()==i){begin=it.key();break;}pc.opcode=OpRepeatEnd;pc.a=begin;}
        else if(command.type==QLatin1String("repeat.break")){int target=commands.size();for(auto it=repeatEnd.cbegin();it!=repeatEnd.cend();++it)if(it.key()<i&&it.value()>i&&it.value()+1<target)target=it.value()+1;pc.opcode=OpJump;pc.a=target;}
        else if(command.type==QLatin1String("jump")){pc.opcode=OpJump;pc.a=labels.value(p.value(QStringLiteral("label")).toString(),i)+1;}
        else if(command.type==QLatin1String("wait")){pc.opcode=OpWait;pc.a=qBound(0,p.value(QStringLiteral("frames"),30).toInt(),360000);}
        else if(command.type==QLatin1String("wait.until")){pc.opcode=OpWaitUntil;if(packCondition(p,&pc,unsupported))pc.h=qBound(0,p.value(QStringLiteral("timeoutFrames"),0).toInt(),360000);else pc.opcode=OpNone;}
        else if(command.type==QLatin1String("common.call")||command.type==QLatin1String("common.reserve")){const QString id=p.value(QStringLiteral("commonId")).toString();const int n=p.value(QStringLiteral("number"),0).toInt();const int ci=!id.isEmpty()?commonById.value(id,-1):commonByNumber.value(n,-1);if(ci>=0){pc.opcode=command.type==QLatin1String("common.reserve")?OpCommonReserve:OpCommonCall;pc.a=ci;pc.b=qBound(-1000,p.value(QStringLiteral("priority"),0).toInt(),1000);}else if(unsupported)unsupported->push_back(command.type+QStringLiteral(" (destino inválido)"));if(!p.value(QStringLiteral("arguments")).toMap().isEmpty()&&unsupported)unsupported->push_back(command.type+QStringLiteral(" (argumentos ainda não portados)"));}
        else if(command.type==QLatin1String("map.event.call")){const QString mapId=p.value(QStringLiteral("mapId")).toString();const int mi=mapId.isEmpty()?mapIndex:ed.mapIndexById(mapId);const QString key=QString::number(mi)+QLatin1Char(':')+p.value(QStringLiteral("eventId")).toString();const int ek=eventKeys.value(key,-1);if(mi==mapIndex&&ek>=0){pc.opcode=OpMapEventCall;pc.a=ek;pc.b=p.value(QStringLiteral("pageIndex"),-1).toInt();}else if(unsupported)unsupported->push_back(command.type+QStringLiteral(" (chamada cruzada/invalid)"));}
        else if(command.type==QLatin1String("message")){pc.opcode=OpMessage;pc.a=strings?strings->add(p.value(QStringLiteral("text")).toString()):-1;pc.b=strings?strings->add(p.value(QStringLiteral("speaker")).toString()):-1;}
        else if(command.type==QLatin1String("choice.show")){const QVariantList list=p.value(QStringLiteral("choices")).toList();pc.opcode=OpChoice;pc.a=strings?strings->values.size():0;pc.b=qMin(8,list.size());for(int oi=0;strings&&oi<pc.b;++oi)strings->values.push_back(list.at(oi).toString().toLatin1());pc.c=p.value(QStringLiteral("resultVariable"),0).toInt();pc.d=p.value(QStringLiteral("cancelValue"),-1).toInt();if(!p.value(QStringLiteral("branches")).toList().isEmpty()&&unsupported)unsupported->push_back(QStringLiteral("choice.show (branches ainda não portadas; resultado é salvo na variável)"));}
        else if(command.type==QLatin1String("subtitle.show")||command.type==QLatin1String("subtitle.enqueue")){pc.opcode=OpSubtitle;pc.a=strings?strings->add(p.value(QStringLiteral("text")).toString()):-1;pc.b=qBound(1,p.value(QStringLiteral("duration"),180).toInt(),36000);pc.c=p.value(QStringLiteral("waitForInput"),false).toBool()?1:0;}
        else if(command.type==QLatin1String("string.set")){pc.opcode=OpStringSet;pc.a=p.value(QStringLiteral("id"),1).toInt();pc.b=p.value(QStringLiteral("op"),QStringLiteral("=")).toString()==QLatin1String("append")?1:0;QVariantMap spec=p.value(QStringLiteral("sourceSpec")).toMap();const QString text=spec.value(QStringLiteral("value"),p.value(QStringLiteral("value"))).toString();pc.c=strings?strings->add(text):-1;}
        else if(command.type==QLatin1String("value.get")){const QVariantMap q=p.value(QStringLiteral("query")).toMap();const QVariantMap target=p.value(QStringLiteral("target")).toMap();pc.opcode=OpValueGet;pc.a=strings?strings->add(q.value(QStringLiteral("key")).toString()):-1;const QString tk=target.value(QStringLiteral("target")).toString();pc.b=tk==QLatin1String("switch")?1:(tk==QLatin1String("string")?2:0);pc.c=target.value(QStringLiteral("id"),0).toInt();pc.d=q.value(QStringLiteral("x"),0).toInt();pc.e=q.value(QStringLiteral("y"),0).toInt();}
        else if(command.type==QLatin1String("ludo.screen.tone")||command.type==QLatin1String("ludo.screen.clearTone")){pc.opcode=OpScreenTone;pc.a=command.type==QLatin1String("ludo.screen.clearTone")?0:p.value(QStringLiteral("red"),0).toInt();pc.b=command.type==QLatin1String("ludo.screen.clearTone")?0:p.value(QStringLiteral("green"),0).toInt();pc.c=command.type==QLatin1String("ludo.screen.clearTone")?0:p.value(QStringLiteral("blue"),0).toInt();pc.d=command.type==QLatin1String("ludo.screen.clearTone")?0:p.value(QStringLiteral("gray"),0).toInt();pc.e=qBound(0,p.value(QStringLiteral("duration"),0).toInt(),36000);}
        else if(command.type==QLatin1String("ludo.screen.fade")){pc.opcode=OpScreenFade;pc.a=p.value(QStringLiteral("direction"),QStringLiteral("out")).toString()==QLatin1String("in")?1:0;pc.b=qBound(0,p.value(QStringLiteral("red"),0).toInt(),255);pc.c=qBound(0,p.value(QStringLiteral("green"),0).toInt(),255);pc.d=qBound(0,p.value(QStringLiteral("blue"),0).toInt(),255);pc.e=qBound(0,p.value(QStringLiteral("alpha"),255).toInt(),255);pc.f=qBound(0,p.value(QStringLiteral("duration"),30).toInt(),36000);}
        else if(command.type==QLatin1String("ludo.screen.flash")){pc.opcode=OpScreenFlash;pc.a=qBound(0,p.value(QStringLiteral("red"),255).toInt(),255);pc.b=qBound(0,p.value(QStringLiteral("green"),255).toInt(),255);pc.c=qBound(0,p.value(QStringLiteral("blue"),255).toInt(),255);pc.d=qBound(0,p.value(QStringLiteral("alpha"),160).toInt(),255);pc.e=qBound(1,p.value(QStringLiteral("duration"),20).toInt(),36000);}
        else if(command.type==QLatin1String("weather.set")){pc.opcode=OpWeather;const QString type=p.value(QStringLiteral("type")).toString();pc.a=type==QLatin1String("rain")?1:(type==QLatin1String("snow")?2:(type==QLatin1String("storm")?3:0));pc.b=qBound(0,p.value(QStringLiteral("intensity"),0).toInt(),100);}
        else if(command.type.startsWith(QLatin1String("audio."))&&command.type!=QLatin1String("audio.footstep")){
            const QString channel=command.type.mid(6);
            if(channel==QLatin1String("stop")){pc.opcode=OpAudioStop;pc.a=audioChannelCode(p.value(QStringLiteral("channel"),QStringLiteral("bgm")).toString());pc.b=qBound(0,p.value(QStringLiteral("fadeOutMs"),0).toInt(),60000);}
            else {pc.opcode=OpAudioPlay;pc.a=audioChannelCode(channel);pc.b=strings?strings->add(p.value(QStringLiteral("source")).toString()):-1;pc.c=qBound(0,p.value(QStringLiteral("volume"),90).toInt(),100);pc.d=p.value(QStringLiteral("loop"),channel==QLatin1String("bgm")||channel==QLatin1String("bgs")).toBool()?1:0;pc.e=qBound(50,p.value(QStringLiteral("pitch"),100).toInt(),200);pc.f=qBound(-100,p.value(QStringLiteral("pan"),0).toInt(),100);pc.g=qBound(0,p.value(QStringLiteral("fadeInMs"),0).toInt(),60000);pc.h=qBound(0,p.value(QStringLiteral("transitionMs"),0).toInt(),60000);}
        }
        else if(command.type==QLatin1String("game.ui.open")){pc.opcode=OpUiOpen;pc.a=strings?strings->add(p.value(QStringLiteral("screenId"),QStringLiteral("menu")).toString()):-1;pc.b=p.value(QStringLiteral("wait"),false).toBool()?1:0;}
        else if(command.type.startsWith(QLatin1String("ludo.filter."))&&command.type!=QLatin1String("ludo.filter.clear")){
            const int ft=filterTypeCode(command.type);if(ft>=0){pc.opcode=OpFilterSet;pc.a=ft;pc.b=qBound(1,p.value(QStringLiteral("slot"),1).toInt(),99);pc.c=scopeMask(p);pc.d=qBound(0,p.value(QStringLiteral("duration"),30).toInt(),36000);
                if(ft==0){auto cfg=core::ChromaticAberrationConfig::fromVariantMap(p);pc.e=qRound(cfg.intensityPixels*100);pc.f=qRound(cfg.edgeStart*1000);pc.g=qRound(cfg.mix*1000);pc.h=qRound(cfg.falloff*1000);}
                else if(ft==1){auto cfg=core::NoiseFilterConfig::fromVariantMap(p);pc.e=qRound(cfg.intensity*1000);pc.f=qRound(cfg.grainSizePixels*100);pc.g=cfg.seed;pc.h=qRound(cfg.colorAmount*1000);}
                else if(ft==2){auto cfg=core::ScanlineFilterConfig::fromVariantMap(p);pc.e=qRound(cfg.intensity*1000);pc.f=qRound(cfg.spacingPixels*100);pc.g=qRound(cfg.thickness*1000);pc.h=qRound(cfg.scrollSpeed*100);}
                else if(ft==3){auto cfg=core::VignetteFilterConfig::fromVariantMap(p);pc.e=qRound(cfg.intensity*1000);pc.f=qRound(cfg.radius*1000);pc.g=qRound(cfg.softness*1000);}
                else if(ft==4){auto cfg=core::BlurFilterConfig::fromVariantMap(p);pc.e=qRound(cfg.radiusPixels*100);pc.f=qRound(cfg.strength*1000);pc.g=int(cfg.direction);pc.h=qRound(cfg.angleDegrees*100);}
                else if(ft==5){auto cfg=core::TiltShiftFilterConfig::fromVariantMap(p);pc.e=qRound(cfg.blurPixels*100);pc.f=qRound(cfg.centerY*1000);pc.g=qRound(cfg.focusWidth*1000);pc.h=qRound(cfg.strength*1000);}
            }
        }
        else if(command.type==QLatin1String("ludo.filter.clear")){pc.opcode=OpFilterClear;pc.a=p.value(QStringLiteral("filter"),QStringLiteral("all")).toString()==QLatin1String("all")?-1:filterTypeCode(QStringLiteral("ludo.filter.")+p.value(QStringLiteral("filter")).toString());pc.b=p.value(QStringLiteral("allSlots"),false).toBool()?0:qBound(1,p.value(QStringLiteral("slot"),1).toInt(),99);pc.c=qBound(0,p.value(QStringLiteral("duration"),30).toInt(),36000);}
        else if(command.type==QLatin1String("ludo.screen.shake")){pc.opcode=OpScreenShake;pc.a=qRound(qMax(0.0,p.value(QStringLiteral("x"),6.0).toDouble())*100);pc.b=qRound(qMax(0.0,p.value(QStringLiteral("y"),3.0).toDouble())*100);pc.c=qBound(0,p.value(QStringLiteral("duration"),30).toInt(),36000);pc.d=qRound(qBound(0.5,p.value(QStringLiteral("frequency"),12.0).toDouble(),60.0)*100);}
        else if(command.type==QLatin1String("ludo.camera.zoomOnly")||command.type==QLatin1String("ludo.camera.move")){pc.opcode=OpCameraZoom;pc.a=qRound(qBound(0.25,p.value(QStringLiteral("zoom"),2.0).toDouble(),8.0)*1000.0);pc.b=qBound(0,p.value(QStringLiteral("duration"),30).toInt(),36000);pc.c=p.value(QStringLiteral("wait"),false).toBool()?1:0;}
        else if(command.type==QLatin1String("ludo.camera.reset")){pc.opcode=OpCameraReset;pc.a=qBound(0,p.value(QStringLiteral("duration"),30).toInt(),36000);pc.b=p.value(QStringLiteral("wait"),false).toBool()?1:0;}

        else if(command.type==QLatin1String("flow.exit")){pc.opcode=OpFlowExit;const QString scope=p.value(QStringLiteral("scope"),QStringLiteral("frame")).toString();pc.a=scope==QLatin1String("all")?1:(scope==QLatin1String("common")?2:(scope==QLatin1String("map")?3:0));}
        else if(command.type==QLatin1String("label")||command.type==QLatin1String("endIf")||command.type==QLatin1String("loop.begin")||command.type==QLatin1String("parallel.begin")||command.type==QLatin1String("parallel.end"))pc.opcode=OpNone;
        else {if(unsupported)unsupported->push_back(command.type);pc.opcode=OpNone;}
        out[i]=pc;
    }
    Q_UNUSED(event);return out;
}

bool writeEvents(const QString& path,const QDir& outputDir,const core::Editor& ed,StringPool* strings,QStringList* unsupported,QString* error)
{
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){if(error)*error=QStringLiteral("Não foi possível criar %1").arg(path);return false;}QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
    int eventCount=0;for(const core::MapDoc& doc:ed.docs)eventCount+=doc.events.size();
    QHash<QString,int> commonById;QHash<int,int> commonByNumber;for(int i=0;i<ed.commonEvents.size();++i){commonById.insert(ed.commonEvents.at(i).id,i);commonByNumber.insert(ed.commonEvents.at(i).number,i);}
    QHash<QString,int> eventKeys;int nextKey=1;for(int mi=0;mi<ed.docs.size();++mi)for(const core::MapEvent& ev:ed.docs.at(mi).events)eventKeys.insert(QString::number(mi)+QLatin1Char(':')+ev.id,nextKey++);
    out.writeRawData(kEventsMagic,int(sizeof(kEventsMagic)));out<<kEventsVersion<<quint32(ed.switches.size())<<quint32(ed.variables.size())<<quint32(eventCount)<<quint32(ed.commonEvents.size());
    for(const core::SwitchDef& def:ed.switches)out<<qint32(def.id)<<qint32(def.initial?1:0);for(const core::VariableDef& def:ed.variables)out<<qint32(def.id)<<qint32(def.initial);
    quint32 eventKey=1;
    for(int mapIndex=0;mapIndex<ed.docs.size();++mapIndex){const core::MapDoc& doc=ed.docs.at(mapIndex);for(const core::MapEvent& event:doc.events){out<<qint32(mapIndex)<<qint32(eventKey)<<qint32(event.cell.x())<<qint32(event.cell.y())<<quint32(event.pages.size());
        for(int pageIndex=0;pageIndex<event.pages.size();++pageIndex){const core::EventPage& page=event.pages.at(pageIndex);const core::PageConditions cond=core::PageConditions::fromJson(QJsonObject::fromVariantMap(page.conditions));quint32 flags=0;if(cond.useSwitchA)flags|=1u;if(cond.useSwitchB)flags|=2u;if(cond.useVariable)flags|=4u;if(cond.useSelfSwitch)flags|=8u;const QString sl=cond.selfSwitchLetter.toUpper();const qint32 si=qBound(0,sl.isEmpty()?0:sl.at(0).unicode()-QChar('A').unicode(),3);
            const QVector<PackedCommand> packed=packCommands(ed,mapIndex,&event,page.commands,commonById,commonByNumber,eventKeys,strings,unsupported);const QImage graphic=eventGraphicFrame(ed,page.graphic).convertToFormat(QImage::Format_RGBA8888);const qint32 gk=graphic.isNull()?0:1,gw=graphic.isNull()?0:graphic.width(),gh=graphic.isNull()?0:graphic.height();
            out<<qint32(triggerCode(page.trigger))<<qint32(page.priority==core::EventPriority::Same?1:(page.priority==core::EventPriority::Above?2:0))<<qint32(page.blocksPlayer?1:0)<<qint32(page.throughWall?1:0)<<quint32(flags)<<qint32(cond.switchAId)<<qint32(cond.switchBId)<<qint32(cond.variableId)<<qint32(variableCompareCode(cond.variableOp))<<qint32(cond.variableValue)<<qint32(si)<<gk<<gw<<gh<<qint32(qBound(0,page.graphic.opacity,255));
            for(int sensor=0;sensor<8;++sensor) out<<qint32(sensor<page.sensorRanges.size()?qMax(0,page.sensorRanges.at(sensor)):0);
            out<<quint32(packed.size());
            for(const PackedCommand& cmd:packed)out<<cmd.opcode<<cmd.a<<cmd.b<<cmd.c<<cmd.d<<cmd.e<<cmd.f<<cmd.g<<cmd.h;
            if(!graphic.isNull()){const QString name=QStringLiteral("event_%1_p%2.rgba").arg(eventKey,6,10,QLatin1Char('0')).arg(pageIndex,3,10,QLatin1Char('0'));if(!writeImageRaw(outputDir.filePath(name),graphic,error))return false;}}
        ++eventKey;}}
    // Rotinas comuns compartilham o mesmo bytecode e são chamadas pelo Interpreter Switch.
    for(int ci=0;ci<ed.commonEvents.size();++ci){const core::CommonEvent& ce=ed.commonEvents.at(ci);if((!ce.parameters.isEmpty()||!ce.locals.isEmpty()||ce.returnValue.enabled)&&unsupported)unsupported->push_back(QStringLiteral("common.call (assinatura avançada: %1)").arg(ce.name));if(ce.advancedTrigger&&unsupported)unsupported->push_back(QStringLiteral("evento comum (gatilho avançado: %1)").arg(ce.name));const QVector<PackedCommand> packed=packCommands(ed,-1,nullptr,ce.commands,commonById,commonByNumber,eventKeys,strings,unsupported);qint32 trig=ce.trigger==core::CommonTrigger::Autorun?1:(ce.trigger==core::CommonTrigger::Parallel?2:0);out<<qint32(ci)<<qint32(ce.number)<<trig<<qint32(ce.switchId)<<quint32(packed.size());for(const PackedCommand& cmd:packed)out<<cmd.opcode<<cmd.a<<cmd.b<<cmd.c<<cmd.d<<cmd.e<<cmd.f<<cmd.g<<cmd.h;}
    if(out.status()!=QDataStream::Ok){if(error)*error=QStringLiteral("Falha ao gravar os eventos do Switch.");return false;}return true;
}


bool writeUiSettings(const QString& path,const core::Editor& ed,QString* error)
{
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){if(error)*error=QStringLiteral("Não foi possível criar %1").arg(path);return false;}
    QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);out.writeRawData("LUDOUI1\0",8);out<<quint32(1);
    auto color=[&](const QColor& c){out<<quint8(c.red())<<quint8(c.green())<<quint8(c.blue())<<quint8(c.alpha());};
    color(ed.gameUi.windowFill);color(ed.gameUi.windowBorder);color(ed.gameUi.textColor);color(ed.gameUi.selectedTextColor);color(ed.gameUi.accentColor);color(ed.gameUi.selectionColor);
    out<<qint32(ed.gameUi.paddingX)<<qint32(ed.gameUi.paddingY)<<qint32(ed.gameUi.fontSize)<<qint32(ed.gameUi.windowOpacity)<<qint32(ed.gameUi.animationMs);
    out<<quint32(ed.gameUi.screenNames.size());for(auto it=ed.gameUi.screenNames.cbegin();it!=ed.gameUi.screenNames.cend();++it){const QByteArray id=it.key().toUtf8(),name=it.value().toUtf8();out<<quint32(id.size());out.writeRawData(id.constData(),id.size());out<<quint32(name.size());out.writeRawData(name.constData(),name.size());out<<qint32(ed.gameUi.screenTransparent.value(it.key(),false)?1:0);}
    return out.status()==QDataStream::Ok;
}

void collectAudioSources(const QVector<core::EventCommand>& commands,QSet<QString>* out)
{
    if(!out)return;for(const auto& command:commands){if(command.type.startsWith(QLatin1String("audio."))&&command.type!=QLatin1String("audio.stop")&&command.type!=QLatin1String("audio.footstep")){const QString source=command.params.value(QStringLiteral("source")).toString().trimmed();if(!source.isEmpty())out->insert(source);}}
}
bool copyReferencedAudio(const core::Editor& ed,const QDir& outputDir,QStringList* unsupported,QString* error)
{
    QSet<QString> sources;for(const auto& doc:ed.docs)for(const auto& ev:doc.events)for(const auto& page:ev.pages)collectAudioSources(page.commands,&sources);for(const auto& ce:ed.commonEvents)collectAudioSources(ce.commands,&sources);
    for(const QString& source:sources){const QString absolute=QFileInfo(source).isAbsolute()?source:QDir(ed.projectRoot()).filePath(source);QFileInfo fi(absolute);if(!fi.exists()){if(unsupported)unsupported->push_back(QStringLiteral("áudio ausente: %1").arg(source));continue;}if(fi.suffix().compare(QStringLiteral("wav"),Qt::CaseInsensitive)!=0){if(unsupported)unsupported->push_back(QStringLiteral("áudio Switch requer WAV PCM nesta etapa: %1").arg(source));continue;}QString rel=source;rel.replace('\\','/');while(rel.startsWith('/'))rel.remove(0,1);const QString dst=outputDir.filePath(rel);QDir().mkpath(QFileInfo(dst).absolutePath());QFile::remove(dst);if(!QFile::copy(absolute,dst)){if(error)*error=QStringLiteral("Falha ao copiar áudio %1").arg(source);return false;}}
    return true;
}

bool writeManifest(const QString& path, const core::Editor& ed, const QVector<QImage>& maps,
                   const QImage& player, int startMapIndex, const QPoint& start, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Não foi possível criar %1").arg(path);
        return false;
    }
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData(kMagic, int(sizeof(kMagic)));
    const auto& ps = ed.player;
    out << kVersion << qint32(startMapIndex) << qint32(start.x()) << qint32(start.y())
        << quint32(player.width()) << quint32(player.height())
        << quint32((player.isNull() ? 0u : 1u) | 2u | 4u | 8u)
        << qint32(qMax(1, ps.frameCols)) << qint32(qMax(1, ps.frameRows))
        << qint32(ps.spriteDirs) << qint32(ps.diagonalsInSameRow ? 1 : 0)
        << qint32(qMax(1, ps.framesPerDirection())) << qint32(ps.effectiveIdleFrame())
        << qint32(ps.animOrder) << qint32(ps.walkDirs)
        << qint32(ps.halfStep() ? 1 : 0)
        << qint32(ps.effectiveHitbox() == core::Editor::PlayerSettings::HitboxHalf ? 1 : 0)
        << qint32(qRound(qBound(0.1, ps.tilesPerSecond, 20.0) * 1000.0))
        << qint32(qMax(1, ed.gameResolution.width()))
        << qint32(qMax(1, ed.gameResolution.height()))
        // GameSession::restartGame() usa zoom inicial 2.0. Mantemos o mesmo
        // contrato no Switch; comandos de câmera podem alterá-lo em runtime.
        << qint32(2000)
        << quint32(ed.docs.size());
    for (int i = 0; i < ed.docs.size(); ++i) {
        const core::MapDoc& doc = ed.docs.at(i);
        const QImage& map = maps.at(i);
        out << quint32(map.width()) << quint32(map.height())
            << quint32(qMax(1, doc.map.tileWidth)) << quint32(qMax(1, doc.map.tileHeight))
            << quint32(qMax(0, doc.map.width)) << quint32(qMax(0, doc.map.height));
    }
    if (out.status() != QDataStream::Ok) {
        if (error) *error = QStringLiteral("Falha ao gravar o manifesto Switch.");
        return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    if (argc != 3) {
        err << "Uso: LudoSwitchPackager <projeto.ludo> <pasta-saida>\n";
        return 2;
    }

    const QString projectPath = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString outputPath = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    core::Editor ed;
    QString error;
    if (!core::io::loadProject(ed, projectPath, &error, core::io::ProjectLoadMode::ReadOnlyPreview)) {
        err << "Não foi possível abrir o projeto: " << error << '\n';
        return 3;
    }
    if (ed.docs.isEmpty()) {
        err << "O projeto não possui mapas.\n";
        return 4;
    }

    int startMapIndex = ed.mapIndexById(ed.startMapId);
    if (startMapIndex < 0) startMapIndex = 0;
    const core::MapDoc& startMap = ed.docs.at(startMapIndex);

    QDir dir;
    if (!dir.mkpath(outputPath)) {
        err << "Não foi possível criar a pasta de saída.\n";
        return 7;
    }
    QDir outputDir(outputPath);

    core::RenderOptions options;
    options.skipReferenceLayers = true;
    options.fillBackground = true;
    options.drawObjectFrames = false;
    options.highlightActive = false;
    options.cacheMaskPaths = false;

    QVector<QImage> renderedMaps;
    renderedMaps.reserve(ed.docs.size());
    constexpr qint64 maxPixels = 32ll * 1024ll * 1024ll;
    for (int i = 0; i < ed.docs.size(); ++i) {
        const core::MapDoc& doc = ed.docs.at(i);
        QImage map = core::renderMapToImage(ed, doc, options);
        if (map.isNull()) {
            err << "Não foi possível renderizar o mapa: " << doc.name << '\n';
            return 5;
        }
        if (qint64(map.width()) * qint64(map.height()) > maxPixels) {
            err << "O mapa “" << doc.name << "” é grande demais para este protótipo Switch.\n";
            return 6;
        }
        const QString suffix = QStringLiteral("%1").arg(i, 3, 10, QLatin1Char('0'));
        if (!writeImageRaw(outputDir.filePath(QStringLiteral("map_%1.rgba").arg(suffix)), map, &error) ||
            !writeBytes(outputDir.filePath(QStringLiteral("collision_%1.bin").arg(suffix)), buildCollisionGrid(ed, doc), &error)) {
            err << error << '\n';
            return 8;
        }
        renderedMaps.push_back(map);
    }

    const QImage player = playerCharset(ed);
    if (!player.isNull() && !writeImageRaw(outputDir.filePath(QStringLiteral("player.rgba")), player, &error)) {
        err << error << '\n';
        return 8;
    }

    QStringList unsupported; StringPool strings;
    if (!writeEvents(outputDir.filePath(QStringLiteral("events.bin")), outputDir, ed, &strings, &unsupported, &error) ||
        !writeTextResources(outputDir.filePath(QStringLiteral("text.bin")), outputDir.filePath(QStringLiteral("font.rgba")), ed, strings, &error) ||
        !writeUiSettings(outputDir.filePath(QStringLiteral("ui.bin")), ed, &error) ||
        !copyReferencedAudio(ed, outputDir, &unsupported, &error) ||
        !writeManifest(outputDir.filePath(QStringLiteral("game.lsw")), ed, renderedMaps, player,
                       startMapIndex, ed.startPosition, &error)) {
        err << error << '\n';
        return 8;
    }

    QFile runtime(outputDir.filePath(QStringLiteral("game.ludo")));
    if (!runtime.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err << "Não foi possível criar game.ludo.\n";
        return 9;
    }
    QJsonObject payload = core::io::buildRuntimeProjectPayload(ed);
    payload[QStringLiteral("runtimeTargetPlatform")] = QStringLiteral("switch-homebrew");
    runtime.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    runtime.close();

    unsupported.removeDuplicates();
    QFile info(outputDir.filePath(QStringLiteral("LEIA-ME.txt")));
    if (info.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream text(&info);
        const double initialZoom = 2.0;
        const double visibleWorldW = ed.gameResolution.width() / initialZoom;
        const double visibleWorldH = ed.gameResolution.height() / initialZoom;
        text << "LUDO Switch SP8.2 Performance + Viewport Diagnostics\n\n"
             << "Mapas exportados: " << ed.docs.size() << "\n"
             << "Mapa inicial: " << startMap.name << "\n"
             << "Início: " << ed.startPosition.x() << ", " << ed.startPosition.y() << "\n"
             << "Resolução lógica: " << ed.gameResolution.width() << "x" << ed.gameResolution.height() << "\n"
             << "Zoom inicial: 2.0x\n"
             << "Área de mundo visível no zoom inicial: " << int(visibleWorldW) << "x" << int(visibleWorldH) << " px\n"
             << "Mapa inicial em pixels: " << renderedMaps.at(startMapIndex).width() << "x" << renderedMaps.at(startMapIndex).height() << " px\n"
             << ((renderedMaps.at(startMapIndex).width() < visibleWorldW) ? "AVISO: mapa mais estreito que a câmera; haverá margem lateral como no runtime desktop.\n" : "")
             << ((renderedMaps.at(startMapIndex).height() < visibleWorldH) ? "AVISO: mapa mais baixo que a câmera; haverá margem vertical como no runtime desktop.\n" : "")
             << "Movimento: velocidade, passo inteiro/meio passo, hitbox e 4/8 direções conforme o projeto.\n"
             << "Sprites de eventos: página ativa, prioridade e opacidade.\n"
             << "Eventos: ação, toque, automático e paralelo.\n"
             << "Interpreter: fluxo estrutural, condições simples, loops/repetições, waits, chamadas de evento/comum, transferências, switches e variáveis.\n";
        if (!unsupported.isEmpty()) {
            text << "\nComandos ainda não executados no Switch:\n";
            for (const QString& type : unsupported) text << "- " << type << "\n";
        }
    }

    out << "Bundle Switch v8 (SP8.2 Performance + Viewport Diagnostics) criado em: " << outputPath << '\n'
        << "Mapas exportados: " << ed.docs.size() << '\n'
        << "Eventos exportados: events.bin\n"
        << "Textos/UI: text.bin + font.rgba + ui.bin\n"
        << "Agora copie todo o conteúdo do bundle ao lado do .nro (incluindo ui.bin e arquivos WAV referenciados).\n";
    if (!unsupported.isEmpty())
        out << "Aviso: " << unsupported.size() << " tipo(s) de comando ainda não são executados nesta etapa de paridade. Veja LEIA-ME.txt.\n";
    return 0;
}
