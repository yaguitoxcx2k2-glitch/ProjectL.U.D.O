#include "ProductReadiness.h"

#include "AssetWorkflow.h"
#include "Editor.h"
#include "ProjectHealth.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <utility>
#include <QStorageInfo>

namespace core {
namespace {
void add(ProductReadinessSnapshot& out, ProductReadinessCheck check)
{
    if (check.level == ProductReadinessLevel::Blocked) ++out.blockedCount;
    else if (check.level == ProductReadinessLevel::Attention) ++out.attentionCount;
    else ++out.readyCount;
    out.checks.push_back(std::move(check));
}
}

QString ProductReadiness::levelLabel(ProductReadinessLevel level)
{
    switch (level) {
    case ProductReadinessLevel::Ready: return QObject::tr("Pronto");
    case ProductReadinessLevel::Attention: return QObject::tr("Atenção");
    case ProductReadinessLevel::Blocked: return QObject::tr("Bloqueado");
    }
    return QObject::tr("Desconhecido");
}

ProductReadinessSnapshot ProductReadiness::inspect(const Editor& editor)
{
    ProductReadinessSnapshot out;
    const QFileInfo projectInfo(editor.projectPath);
    const QString root = projectInfo.absolutePath();

    if (editor.projectPath.isEmpty() || !projectInfo.exists()) {
        add(out, {QStringLiteral("project.saved"), QObject::tr("Projeto salvo"),
                  QObject::tr("Salve o projeto antes de criar uma versão para publicação."),
                  ProductReadinessLevel::Blocked});
    } else {
        add(out, {QStringLiteral("project.saved"), QObject::tr("Projeto salvo"),
                  QObject::tr("O projeto está salvo e pronto para ser verificado."), ProductReadinessLevel::Ready});
    }

    if (!root.isEmpty()) {
        const QFileInfo rootInfo(root);
        add(out, {QStringLiteral("project.writable"), QObject::tr("Permissão para salvar"),
                  rootInfo.isWritable() ? QObject::tr("A pasta do projeto permite salvar e atualizar arquivos.")
                                        : QObject::tr("A LUDO não consegue gravar nessa pasta. Salvamentos, recuperação automática e exportações podem falhar."),
                  rootInfo.isWritable() ? ProductReadinessLevel::Ready : ProductReadinessLevel::Blocked});

        const QString assets = QDir(root).filePath(QStringLiteral("Assets"));
        const QFileInfo assetsInfo(assets);
        const bool assetsOk = assetsInfo.exists() && assetsInfo.isDir() && assetsInfo.isWritable();
        add(out, {QStringLiteral("assets.folder"), QObject::tr("Pasta Assets"),
                  assetsOk ? QObject::tr("A pasta de arquivos do projeto está disponível e pode ser atualizada.")
                           : QObject::tr("A pasta Assets está ausente ou não pode ser atualizada. Use a manutenção do projeto para recriá-la."),
                  assetsOk ? ProductReadinessLevel::Ready : ProductReadinessLevel::Attention});

        QStorageInfo storage(root);
        if (storage.isValid() && storage.isReady()) {
            const qint64 free = storage.bytesAvailable();
            constexpr qint64 kBlocked = 64ll * 1024ll * 1024ll;
            constexpr qint64 kAttention = 512ll * 1024ll * 1024ll;
            const auto level = free < kBlocked ? ProductReadinessLevel::Blocked
                                               : (free < kAttention ? ProductReadinessLevel::Attention
                                                                    : ProductReadinessLevel::Ready);
            add(out, {QStringLiteral("storage.free"), QObject::tr("Espaço em disco"),
                      QObject::tr("%1 MB disponíveis no volume do projeto.").arg(free / (1024ll * 1024ll)), level});
        }
    }

    const ProjectHealthSnapshot health = ProjectHealth::inspect(editor);
    if (health.validation.hasErrors()) {
        add(out, {QStringLiteral("health.errors"), QObject::tr("Validação do projeto"),
                  QObject::tr("Erros que impedem a publicação: %1.").arg(health.validation.errorCount),
                  ProductReadinessLevel::Blocked});
    } else if (health.validation.warningCount > 0) {
        add(out, {QStringLiteral("health.errors"), QObject::tr("Validação do projeto"),
                  QObject::tr("O projeto pode ser publicado. Avisos que vale a pena revisar: %1.").arg(health.validation.warningCount),
                  ProductReadinessLevel::Attention});
    } else {
        add(out, {QStringLiteral("health.errors"), QObject::tr("Validação do projeto"),
                  QObject::tr("Nenhum problema de validação foi encontrado."), ProductReadinessLevel::Ready});
    }

    return out;
}

} // namespace core
