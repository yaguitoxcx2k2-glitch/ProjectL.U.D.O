#pragma once

#include "core/EventModel.h"

#include <QSize>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

struct PreviewContext {
    QVariantMap state;
    const core::Editor* editor=nullptr;
    QSize viewportSize=QSize(800,600);
    bool animated=true;
};

/// Contrato comum de preview. Cada provedor representa um domínio visual,
/// mas recebe o EventCommand canônico e o mesmo contexto do Event Editor.
class PreviewFramework
{
public:
    virtual ~PreviewFramework()=default;
    virtual QString id() const=0;
    virtual bool supports(const core::EventCommand& command) const=0;
    virtual QWidget* createPreview(const core::EventCommand& command,
                                   const PreviewContext& context,
                                   QWidget* parent=nullptr) const=0;

    static const PreviewFramework* providerFor(const core::EventCommand& command);
    static QString previewDomainId(const core::EventCommand& command);
    static bool supportsPreview(const core::EventCommand& command);
    static QWidget* create(const core::EventCommand& command,
                           const PreviewContext& context,
                           QWidget* parent=nullptr);
};

} // namespace ui
