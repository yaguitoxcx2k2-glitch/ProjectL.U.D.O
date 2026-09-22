#include "RuntimeTrace.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
namespace game {
void RuntimeTraceRecorder::begin(QString name){if(!m_enabled)return;if(!m_clock.isValid())m_clock.start();m_events.append(QJsonObject{{"name",name},{"ph","B"},{"ts",double(m_clock.nsecsElapsed()/1000)},{"pid",1},{"tid",1}});}
void RuntimeTraceRecorder::end(QString name){if(!m_enabled)return;if(!m_clock.isValid())m_clock.start();m_events.append(QJsonObject{{"name",name},{"ph","E"},{"ts",double(m_clock.nsecsElapsed()/1000)},{"pid",1},{"tid",1}});}
bool RuntimeTraceRecorder::writeChromeTrace(const QString& path,QString* error) const {if(error)error->clear();QFile f(path);if(!f.open(QIODevice::WriteOnly)){if(error)*error=f.errorString();return false;}f.write(QJsonDocument(QJsonObject{{"traceEvents",m_events}}).toJson(QJsonDocument::Compact));return true;}
}
