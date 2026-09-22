#include "GameSettingsDialog.h"

#include "AssetBrowser.h"
#include "Dialogs.h"
#include "EditorUiPrimitives.h"
#include "GameUiThemeDialog.h"
#include "LocalizationDialog.h"
#include "PictureDialogs.h"
#include "core/Editor.h"
#include "core/Localization.h"
#include "game/RuntimeGpuPolicy.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <memory>
#include <utility>

namespace ui {
namespace {
struct GameSettingsDraft {
    QString startMapId;
    QPoint startPosition;
    QString titleBackgroundPath;
    QColor titleBackgroundColor;
    QColor titleColor;
    QColor titleAccent;
};
}

GameSettingsDialog::GameSettingsDialog(core::Editor& editor,
                                       std::function<void()> configurePlayer,
                                       QWidget* parent)
    : QDialog(parent), m_editor(editor), m_configurePlayer(std::move(configurePlayer))
{
    setWindowTitle(tr("Configurações do Jogo"));
    resize(760, 560);
    setAccessibleName(tr("Configurações do Jogo"));

    auto draft = std::make_shared<GameSettingsDraft>();
    draft->startMapId = m_editor.startMapId;
    draft->startPosition = m_editor.startPosition;
    draft->titleBackgroundPath = m_editor.titleScreen.backgroundPath;
    draft->titleBackgroundColor = m_editor.titleScreen.backgroundColor;
    draft->titleColor = m_editor.titleScreen.titleColor;
    draft->titleAccent = m_editor.titleScreen.accentColor;

    auto* outer = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs, 1);
    const auto makePage = [&](const QString& text) {
        auto* page = new QWidget(tabs);
        auto* v = new QVBoxLayout(page);
        v->addWidget(primitives::hintLabel(text, page));
        return QPair<QWidget*, QVBoxLayout*>(page, v);
    };
    const auto button = [&](QVBoxLayout* layout, const QString& text, std::function<void()> fn) {
        auto* b = new QPushButton(text, this);
        b->setMinimumHeight(38);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, this, [fn = std::move(fn)] { fn(); });
    };

    // ----------------------------------------------------------- Jogabilidade
    auto gameplay = makePage(tr("Configurações do personagem, entrada e apresentação dos textos."));
    auto* startForm = new QFormLayout;
    auto* startPicker = new QPushButton(tr("Escolher visualmente…"), gameplay.first);
    auto* startLocation = new QLabel(gameplay.first);
    startLocation->setWordWrap(true);
    const auto refreshStartLabel = [this, draft, startLocation] {
        const core::MapDoc* map = m_editor.mapById(draft->startMapId);
        startLocation->setText(map
            ? tr("%1 — tile (%2, %3)").arg(map->name).arg(draft->startPosition.x()).arg(draft->startPosition.y())
            : tr("Não definido"));
    };
    refreshStartLabel();
    auto* startRow = new QWidget(gameplay.first);
    auto* startRowLayout = new QHBoxLayout(startRow);
    startRowLayout->setContentsMargins(0, 0, 0, 0);
    startRowLayout->addWidget(startLocation, 1);
    startRowLayout->addWidget(startPicker);
    startForm->addRow(tr("Início único do jogo:"), startRow);
    connect(startPicker, &QPushButton::clicked, this, [this, draft, refreshStartLabel] {
        StartPositionDialog picker(m_editor, draft->startMapId, draft->startPosition, this);
        if (picker.exec() != QDialog::Accepted) return;
        draft->startMapId = picker.selectedMapId();
        draft->startPosition = picker.selectedPosition();
        refreshStartLabel();
    });

    auto* autosave = new QCheckBox(tr("Autosave ao trocar de mapa"), gameplay.first);
    autosave->setChecked(m_editor.autosaveEnabled);
    auto* autosaveSlot = new QSpinBox(gameplay.first);
    autosaveSlot->setRange(1, 99);
    autosaveSlot->setValue(m_editor.autosaveSlot);
    auto* checkpointSlot = new QSpinBox(gameplay.first);
    checkpointSlot->setRange(1, 99);
    checkpointSlot->setValue(m_editor.checkpointSlot);
    startForm->addRow(tr("Slot reservado para Checkpoint:"), checkpointSlot);
    auto* quickSave = new QCheckBox(tr("Permitir quicksave / quickload"), gameplay.first);
    quickSave->setChecked(m_editor.quickSaveEnabled);
    auto* quickSaveSlot = new QSpinBox(gameplay.first);
    quickSaveSlot->setRange(1, 99);
    quickSaveSlot->setValue(m_editor.quickSaveSlot);
    startForm->addRow(autosave, autosaveSlot);
    startForm->addRow(quickSave, quickSaveSlot);
    gameplay.second->addLayout(startForm);
    button(gameplay.second, tr("Configurar personagem…"), [this] { if (m_configurePlayer) m_configurePlayer(); });
    button(gameplay.second, tr("Ludo Input System…"), [this] { ControlsDialog(m_editor, this).exec(); });
    button(gameplay.second, tr("Customizar Ludo Cutscene Skip…"), [this] { CutsceneSkipDialog(m_editor, this).exec(); });
    button(gameplay.second, tr("Configurar legendas…"), [this] { SubtitleStyleDialog(m_editor, this).exec(); });
    gameplay.second->addStretch(1);
    tabs->addTab(gameplay.first, tr("Jogabilidade"));

    // --------------------------------------------------------------- Dados
    auto data = makePage(tr("Dados e sistemas globais usados pelos comandos de evento."));
    button(data.second, tr("Banco de Dados RPG…"), [this] { DatabaseDialog(m_editor, this).exec(); });
    button(data.second, tr("Banco de Dados Personalizado…"), [this] { CustomDatabaseDialog(m_editor, this).exec(); });
    button(data.second, tr("Switches e Variáveis Globais…"), [this] { GameDataDialog(m_editor, this).exec(); });
    button(data.second, tr("Eventos comuns…"), [this] { CommonEventsDialog(m_editor, this).exec(); });
    button(data.second, tr("Localização / Idiomas…"), [this] { LocalizationDialog(m_editor, this).exec(); });
    button(data.second, tr("Extensões visuais…"), [this] { NoCodePluginManagerDialog(m_editor, this).exec(); });
    data.second->addStretch(1);
    tabs->addTab(data.first, tr("Banco de dados"));

    // ----------------------------------------------------------- Interface
    auto interfacePage = makePage(tr("Personalize as interfaces nativas do jogo sem editar código."));
    auto* titleForm = new QFormLayout;
    auto* titleText = new QLineEdit(m_editor.titleScreen.titleText, interfacePage.first);
    titleText->setPlaceholderText(m_editor.projectName);
    auto* titleTextKey = new QLineEdit(m_editor.titleScreen.titleTextKey, interfacePage.first);
    titleTextKey->setPlaceholderText(QStringLiteral("ui.title.title"));
    titleTextKey->setToolTip(tr("Opcional. Quando a localização estiver ativa, esta chave substitui o título; o texto acima permanece como alternativa."));
    auto* titleBackground = new QLineEdit(m_editor.titleScreen.backgroundPath, interfacePage.first);
    titleBackground->setReadOnly(true);
    auto* backgroundRow = new QWidget(interfacePage.first);
    auto* backgroundLayout = new QHBoxLayout(backgroundRow);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    auto* chooseBackground = new QPushButton(tr("Escolher…"), backgroundRow);
    auto* clearBackground = new QPushButton(tr("Limpar"), backgroundRow);
    backgroundLayout->addWidget(titleBackground, 1);
    backgroundLayout->addWidget(chooseBackground);
    backgroundLayout->addWidget(clearBackground);
    connect(chooseBackground, &QPushButton::clicked, this, [this, draft, titleBackground] {
        const QString path = AssetBrowserDialog::chooseImage(m_editor, this, QStringLiteral("Pictures"));
        if (path.isEmpty()) return;
        draft->titleBackgroundPath = m_editor.projectRelativePath(path);
        titleBackground->setText(draft->titleBackgroundPath);
    });
    connect(clearBackground, &QPushButton::clicked, this, [draft, titleBackground] { draft->titleBackgroundPath.clear(); titleBackground->clear(); });

    const auto colorButton = [this, interfacePage](QColor* color) {
        auto* b = new QPushButton(color->name(), interfacePage.first);
        const auto refresh = [b, color] {
            b->setText(color->name());
            b->setStyleSheet(QStringLiteral("background:%1;color:%2")
                                 .arg(color->name(), color->lightness() > 128 ? QStringLiteral("#111") : QStringLiteral("#fff")));
        };
        refresh();
        connect(b, &QPushButton::clicked, this, [this, color, refresh] {
            const QColor chosen = QColorDialog::getColor(*color, this, tr("Escolher cor"));
            if (chosen.isValid()) { *color = chosen; refresh(); }
        });
        return b;
    };
    auto* backgroundColorButton = colorButton(&draft->titleBackgroundColor);
    auto* titleColorButton = colorButton(&draft->titleColor);
    auto* accentButton = colorButton(&draft->titleAccent);
    auto* showContinue = new QCheckBox(tr("Mostrar Continuar quando houver save"), interfacePage.first);
    showContinue->setChecked(m_editor.titleScreen.showContinue);
    auto* titleShowMode = new QComboBox(interfacePage.first);
    titleShowMode->addItem(tr("Sempre mostrar a Tela de Título"), QStringLiteral("always"));
    titleShowMode->addItem(tr("Pular a Tela de Título"), QStringLiteral("skip"));
    titleShowMode->addItem(tr("Mostrar somente quando já existir um Save"), QStringLiteral("when-save-exists"));
    titleShowMode->addItem(tr("Mostrar somente enquanto ainda não existir Save"), QStringLiteral("when-no-save"));
    titleShowMode->setCurrentIndex(qMax(0, titleShowMode->findData(m_editor.titleScreen.showMode)));
    titleForm->addRow(tr("Título:"), titleText);
    titleForm->addRow(tr("Chave de localização:"), titleTextKey);
    titleForm->addRow(tr("Imagem de fundo:"), backgroundRow);
    titleForm->addRow(tr("Cor de fundo:"), backgroundColorButton);
    titleForm->addRow(tr("Cor do título:"), titleColorButton);
    titleForm->addRow(tr("Cor de destaque:"), accentButton);
    titleForm->addRow(tr("Exibição:"), titleShowMode);
    titleForm->addRow(showContinue);
    interfacePage.second->addLayout(titleForm);
    button(interfacePage.second, tr("Interface In-Game…"), [this] { GameUiThemeDialog(m_editor, this).exec(); });
    interfacePage.second->addStretch(1);
    tabs->addTab(interfacePage.first, tr("Interface"));

    // ------------------------------------------------------------ Recursos
    auto resources = makePage(tr("Recursos globais de imagens, ícones e fontes do projeto."));
    button(resources.second, tr("Biblioteca de imagens…"), [this] { PictureLibraryDialog(m_editor, this).exec(); });
    button(resources.second, tr("Folhas de ícones combinadas…"), [this] { IconSetDialog(m_editor, this).exec(); });
    button(resources.second, tr("Fontes do projeto…"), [this] { FontManagerDialog(m_editor, this).exec(); });
    resources.second->addStretch(1);
    tabs->addTab(resources.first, tr("Recursos"));

    // ------------------------------------------------------ Acessibilidade
    auto accessibilityPage = makePage(tr("Defina quais opções de acessibilidade estarão disponíveis ao jogador. As preferências pessoais ficam no PC do jogador, não no projeto."));
    auto* accessibilityForm = new QFormLayout;
    auto* accessibilityEnabled = new QCheckBox(tr("Ativar opções de acessibilidade"), accessibilityPage.first);
    accessibilityEnabled->setChecked(m_editor.accessibility.enabled);
    auto* allowUiScale = new QCheckBox(tr("Permitir escala de UI/texto (100%–150%)"), accessibilityPage.first);
    allowUiScale->setChecked(m_editor.accessibility.allowUiScale);
    auto* defaultUiScale = new QSpinBox(accessibilityPage.first);
    defaultUiScale->setRange(100, 150);
    defaultUiScale->setSuffix(QStringLiteral("%"));
    defaultUiScale->setValue(m_editor.accessibility.defaultUiScalePercent);
    auto* allowReducedMotion = new QCheckBox(tr("Permitir reduzir flashes e tremores"), accessibilityPage.first);
    allowReducedMotion->setChecked(m_editor.accessibility.allowReducedMotion);
    auto* reduceShakeDefault = new QCheckBox(tr("Reduzir tremores por padrão"), accessibilityPage.first);
    reduceShakeDefault->setChecked(m_editor.accessibility.reduceShakeDefault);
    auto* reduceFlashDefault = new QCheckBox(tr("Reduzir flashes por padrão"), accessibilityPage.first);
    reduceFlashDefault->setChecked(m_editor.accessibility.reduceFlashDefault);
    auto* strongFocusDefault = new QCheckBox(tr("Foco visual reforçado por padrão"), accessibilityPage.first);
    strongFocusDefault->setChecked(m_editor.accessibility.strongFocusDefault);
    auto* defaultTextSpeed = new QSpinBox(accessibilityPage.first);
    defaultTextSpeed->setRange(50, 200);
    defaultTextSpeed->setSuffix(QStringLiteral("%"));
    defaultTextSpeed->setValue(m_editor.accessibility.defaultTextSpeedPercent);
    accessibilityForm->addRow(accessibilityEnabled);
    accessibilityForm->addRow(allowUiScale);
    accessibilityForm->addRow(tr("Escala padrão:"), defaultUiScale);
    accessibilityForm->addRow(allowReducedMotion);
    accessibilityForm->addRow(reduceShakeDefault);
    accessibilityForm->addRow(reduceFlashDefault);
    accessibilityForm->addRow(strongFocusDefault);
    accessibilityForm->addRow(tr("Velocidade padrão do texto:"), defaultTextSpeed);
    accessibilityPage.second->addLayout(accessibilityForm);
    accessibilityPage.second->addStretch(1);
    tabs->addTab(accessibilityPage.first, tr("Acessibilidade"));

    // ------------------------------------------------------ Tela e gráficos
    auto graphics = makePage(tr("Configurações que pertencem ao projeto e serão usadas como padrão pelo Player exportado."));
    auto* graphicsForm = new QFormLayout;
    auto* resolution = new QPushButton(tr("Alterar resolução…"), graphics.first);
    graphicsForm->addRow(tr("Resolução lógica:"), resolution);
    auto* backend = new QComboBox(graphics.first);
    backend->addItem(tr("Automático — recomendado"), QStringLiteral("auto"));
#if defined(Q_OS_WIN)
    backend->addItem(tr("Direct3D 11"), QStringLiteral("d3d11"));
    backend->addItem(tr("Vulkan"), QStringLiteral("vulkan"));
#elif defined(Q_OS_MACOS)
    backend->addItem(tr("Metal"), QStringLiteral("metal"));
#else
    backend->addItem(tr("Vulkan"), QStringLiteral("vulkan"));
    backend->addItem(tr("OpenGL"), QStringLiteral("opengl"));
#endif
    backend->setCurrentIndex(qMax(0, backend->findData(m_editor.runtimeGpuBackend)));
    graphicsForm->addRow(tr("Backend padrão do Player:"), backend);
    auto* filter = new QComboBox(graphics.first);
    filter->addItem(tr("Nearest Neighbor — pixel art"), QStringLiteral("nearest"));
    filter->addItem(tr("Bilinear — suavizado"), QStringLiteral("bilinear"));
    filter->setCurrentIndex(qMax(0, filter->findData(m_editor.runtimeScaleFilter)));
    graphicsForm->addRow(tr("Filtro padrão do Player:"), filter);
    graphics.second->addLayout(graphicsForm);
    graphics.second->addWidget(primitives::infoBanner(
        tr("Estas configurações também são usadas por Testar Mapa (F5) e Testar Jogo (F6), mantendo o playtest fiel ao Player exportado."), graphics.first));
    graphics.second->addStretch(1);
    tabs->addTab(graphics.first, tr("Tela e gráficos"));
    connect(resolution, &QPushButton::clicked, this, [this] { GameResolutionDialog(m_editor, this).exec(); });

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(this, &QDialog::accepted, this, [=, this] {
        if (!draft->startMapId.isEmpty() &&
            (m_editor.startMapId != draft->startMapId || m_editor.startPosition != draft->startPosition)) {
            m_editor.startMapId = draft->startMapId;
            m_editor.startPosition = draft->startPosition;
        }
        m_editor.autosaveEnabled = autosave->isChecked();
        m_editor.autosaveSlot = autosaveSlot->value();
        m_editor.checkpointSlot = checkpointSlot->value();
        m_editor.quickSaveEnabled = quickSave->isChecked();
        m_editor.quickSaveSlot = quickSaveSlot->value();
        m_editor.titleScreen.titleText = titleText->text().trimmed();
        m_editor.titleScreen.titleTextKey = core::normalizeLocalizationKey(titleTextKey->text());
        m_editor.titleScreen.backgroundPath = draft->titleBackgroundPath;
        m_editor.titleScreen.backgroundColor = draft->titleBackgroundColor;
        m_editor.titleScreen.titleColor = draft->titleColor;
        m_editor.titleScreen.accentColor = draft->titleAccent;
        m_editor.titleScreen.showContinue = showContinue->isChecked();
        m_editor.titleScreen.showMode = titleShowMode->currentData().toString();
        m_editor.accessibility.enabled = accessibilityEnabled->isChecked();
        m_editor.accessibility.allowUiScale = allowUiScale->isChecked();
        m_editor.accessibility.defaultUiScalePercent = defaultUiScale->value();
        m_editor.accessibility.allowReducedMotion = allowReducedMotion->isChecked();
        m_editor.accessibility.reduceShakeDefault = reduceShakeDefault->isChecked();
        m_editor.accessibility.reduceFlashDefault = reduceFlashDefault->isChecked();
        m_editor.accessibility.strongFocusDefault = strongFocusDefault->isChecked();
        m_editor.accessibility.defaultTextSpeedPercent = defaultTextSpeed->value();
        m_editor.runtimeGpuBackend = game::normalizeRuntimeGpuBackend(backend->currentData().toString());
        m_editor.runtimeScaleFilter = filter->currentData().toString() == QLatin1String("bilinear")
            ? QStringLiteral("bilinear") : QStringLiteral("nearest");
        m_editor.markDirty();
    });
}

} // namespace ui
