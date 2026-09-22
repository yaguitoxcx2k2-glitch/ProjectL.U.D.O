#pragma once

// LegacyProjectCompat.h
// Quarentena temporária dos dados pertencentes à antiga LUDO Game Engine.
// O LUDO Map Editor não usa estes campos para autoria nem exportação MZ; eles
// permanecem somente para abrir projetos LudoEngineProject antigos durante a
// migração para LudoMapProject.

#include "core/Database.h"
#include "core/DialogueContent.h"
#include "core/CustomDatabase.h"
#include "core/GameData.h"
#include "core/InputMap.h"
#include "core/Localization.h"
#include "core/GameAccessibility.h"
#include "core/NoCodePlugin.h"
#include "core/IconSet.h"
#include "core/Picture.h"
#include "core/SubtitleStyle.h"
#include "core/TextEffects.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMargins>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

struct ProjectFont {
    QString sourcePath;      ///< relativo a Assets/Fonts
    QString family;          ///< nome registrado pelo arquivo TTF/OTF
};

/// Configuração no-code do Ludo Cutscene Skip, gravada no projeto.
struct CutsceneSkipSettings {
    bool enabled = true;
    bool holdToSkip = true;
    int holdMs = 700;
    int fadeFrames = 18;
    QString label = QStringLiteral("Segure {key} para pular");
    QString labelTextKey; ///< chave opcional do catálogo; preserva {key}
    QColor panelColor = QColor(15, 18, 30, 210);
    QColor accentColor = QColor("#67d6ff");
    int corner = 3; // 0 sup-esq, 1 sup-dir, 2 inf-esq, 3 inf-dir
};

/// Preferências do Ludo Input System. O mapa de teclas continua em InputMap.
struct InputSystemSettings {
    bool adaptivePrompts = true;
    bool keyboardPrompts = true;
    bool controllerPrompts = true;
    bool analogMovement = true;
    double analogDeadzone = 0.25;
    QString forcedController; // vazio = teclado; "generic" = controle genérico
    QHash<QString,int> keyboardIcons;   ///< action id -> índice do IconSet
    QHash<QString,int> controllerIcons; ///< action id -> índice do IconSet
    QHash<QString,int> controllerButtons; ///< action id -> botão genérico
};

struct TitleScreenSettings {
    QString titleText;
    QString titleTextKey;              ///< opcional: localization key para o título
    QString backgroundPath;
    QColor backgroundColor = QColor("#111827");
    QColor titleColor = QColor("#f3f4f6");
    QColor accentColor = QColor("#67d6ff");
    bool showContinue = true;
    // LUDO 3.20 — regra No Code de entrada no jogo. Mantida no projeto para
    // que editor e LudoPlayer usem exatamente a mesma decisão.
    // always / skip / when-save-exists / when-no-save
    QString showMode = QStringLiteral("always");
};

/// Metadados No Code usados pelo UI Designer 2.3. Os retângulos principais
/// continuam em GameUiSettings para preservar compatibilidade com projetos
/// 3.3.x e com o runtime existente; esta estrutura adiciona composição,
/// responsividade e organização sem acoplar o jogo a QWidget.
/// Estado visual No Code de um elemento. Os valores são modificadores sobre o
/// retângulo-base normalizado; assim continuam independentes da resolução.
struct UiVisualStateSettings {
    QPointF offset{0.0, 0.0};                ///< deslocamento normalizado
    QSizeF scale{1.0, 1.0};                 ///< 1.0 = tamanho original
    double opacity = 1.0;                    ///< 0..1
    QColor tint = Qt::white;                 ///< multiplicador visual do preview/runtime
};

/// Keyframe genérico da Timeline. Ele usa o mesmo conjunto de propriedades dos
/// estados para que presets, triggers e futuras widgets reutilizem o formato.
struct UiAnimationKeyframeSettings {
    int timeMs = 0;
    QPointF offset{0.0, 0.0};
    QSizeF scale{1.0, 1.0};
    double opacity = 1.0;
    QString easing = QStringLiteral("ease-out");
};

/// Clip de animação No Code associado a um elemento do Designer. `trigger` é
/// declarativo: manual/open/close/hover/leave/pressed/focused/selected/disabled.
struct UiAnimationClipSettings {
    QString name = QStringLiteral("Animation");
    QString trigger = QStringLiteral("manual");
    int durationMs = 300;
    bool loop = false;
    QVector<UiAnimationKeyframeSettings> keyframes;
};

/// Condição declarativa do sistema de eventos do UI Designer 2.2. `source`
/// pode ser always/switch/variable/gold; `op` usa ==, !=, >, >=, <, <=.
/// IDs numéricos mantêm o formato independente do QWidget e do runtime.
struct UiEventConditionSettings {
    QString source = QStringLiteral("always");
    int id = 0;
    QString op = QStringLiteral("==");
    int value = 0;
};

/// Ação No Code. O formato é genérico para que widgets futuros reutilizem a
/// mesma lista: play-animation, stop-animation, set-state, show, hide,
/// set-switch, set-variable, add-variable, add-gold, common-event e close-ui.
struct UiEventActionSettings {
    QString type = QStringLiteral("play-animation");
    QString targetElementId;                 ///< vazio/self = elemento do evento
    QString textValue;                       ///< nome do clip/estado ou parâmetro textual
    int intValue = 0;                        ///< ID numérico (switch/variável/common event)
    int numberValue = 0;                     ///< valor/quantidade da ação
    bool boolValue = true;                   ///< usado por set-switch e flags futuras
};

/// Binding visual de evento. As condições são combinadas por ALL ou ANY e as
/// ações são executadas em ordem quando o trigger é disparado.
struct UiEventBindingSettings {
    QString trigger = QStringLiteral("click");
    bool enabled = true;
    QString conditionMode = QStringLiteral("all");
    QVector<UiEventConditionSettings> conditions;
    QVector<UiEventActionSettings> actions;
};

/// Nó do Visual Logic Graph (UI Designer 2.8 / LUDO 3.13). O grafo reutiliza
/// exatamente UiEventConditionSettings e UiEventActionSettings: o modo visual
/// avançado não cria uma segunda linguagem de lógica.
struct UiVisualLogicNodeSettings {
    QString id;                              ///< logicnode.<uuid>
    QString type = QStringLiteral("action"); // event/condition/action/delay/sequence
    QString label;
    QPointF position{40.0, 40.0};            ///< coordenadas do editor do grafo (px)
    QString trigger = QStringLiteral("click");
    UiEventConditionSettings condition;
    UiEventActionSettings action;
    int delayMs = 0;
};

/// Conexão dirigida do Visual Logic Graph. `fromPort` é next/true/false.
/// `order` mantém saídas Sequence determinísticas e estáveis no save/load.
struct UiVisualLogicLinkSettings {
    QString fromNodeId;
    QString fromPort = QStringLiteral("next");
    QString toNodeId;
    int order = 0;
};

/// Grafo No Code opcional associado a um elemento. Eventos Simples continuam
/// existindo em `eventBindings`; ambos usam o mesmo executor de Conditions/Actions.
struct UiVisualLogicGraphSettings {
    QString id;                              ///< logicgraph.<uuid>
    QString name = QStringLiteral("Visual Logic");
    bool enabled = true;
    QVector<UiVisualLogicNodeSettings> nodes;
    QVector<UiVisualLogicLinkSettings> links;
};

/// Data Binding declarativo do UI Designer 2.3. `property` define a propriedade
/// do elemento (text/value/maximum/visible/enabled/opacity/image/color/progress)
/// e `source` escolhe a origem. IDs e actorId são mantidos separados para o
/// Inspector oferecer seletores No Code em vez de expressões obrigatórias.
struct UiDataBindingSettings {
    QString property = QStringLiteral("visible");
    bool enabled = true;
    QString source = QStringLiteral("party.gold");
    int id = 0;                              ///< variável/switch; 0 = não usado
    QString actorId;                         ///< vazio = primeiro membro do grupo
    QString itemId;                          ///< usado por item.count
    QString constantValue;                   ///< constante textual/numérica/cor
    QString format = QStringLiteral("{value}"); ///< texto formatado; aceita tokens No Code
    QString fallback;                        ///< usado quando a fonte não existe
};

/// Override visual por estado de uma Style Class. Cores inválidas significam
/// "herdar do estado base"; opacityMultiplier é multiplicativo.
struct UiStyleStateSettings {
    QColor fillColor;
    QColor textColor;
    QColor accentColor;
    QColor borderColor;
    QColor innerBorderColor;
    double opacityMultiplier = 1.0;
};

/// Style Class reutilizável do UI Designer 2.6 (Bloco A / 3.9). O background
/// pode ser cor, Windowskin global, imagem 9-slice própria ou nenhum fundo.
/// A classe é puramente declarativa e continua independente de QWidget.
struct UiStyleClassSettings {
    QString id;                                ///< style.<uuid>
    QString name = QStringLiteral("Style");
    QString backgroundMode = QStringLiteral("color"); // color/window-skin/nine-slice/none
    QString imagePath;
    QImage image;
    QMargins slices{12, 12, 12, 12};

    QColor fillColor = QColor(30, 41, 59, 220);
    QColor textColor = Qt::white;
    QColor accentColor = QColor("#67d6ff");
    QColor borderColor = QColor("#94a3b8");
    QColor innerBorderColor = QColor(255, 255, 255, 36);
    double borderWidth = 1.0;
    double innerBorderWidth = 0.0;
    double radius = 6.0;
    double innerInset = 0.0;
    double opacity = 1.0;
    int paddingX = 8;
    int paddingY = 6;
    QString fontFamily;                        ///< vazio = fonte do tema
    int fontSize = 0;                          ///< 0 = tamanho do tema

    QHash<QString, UiStyleStateSettings> states; // normal/hover/pressed/focused/selected/disabled
};

/// Efeito 2D encadeável do UI Designer 2.9 / Bloco D (LUDO 3.14).
/// É deliberadamente backend-agnostic: o runtime transforma a cadeia em
/// UiEffectSpec no UiDrawList, mantendo CPU e QRhi com a mesma composição.
struct UiMaterialEffectSettings {
    QString type = QStringLiteral("none"); // grayscale/sepia/invert/blur/glow/pixelate/chromatic/glitch/wave/dissolve/scanlines/crt/vhs
    double intensity = 1.0;                ///< 0..1
    double amount = 0.5;                   ///< parâmetro principal do efeito, 0..1
    double speed = 1.0;                    ///< animação procedural; 0 = estático
    QColor color = QColor("#67d6ff");      ///< glow/tint auxiliar
    int seed = 0;
    bool enabled = true;
};

/// Dados de simulação usados somente pelo preview do Designer. Não alteram o
/// GameState nem o projeto em runtime; servem para visualizar bindings antes do Play.
struct UiDataPreviewSettings {
    QString actorName = QStringLiteral("Harold");
    int actorLevel = 15;
    int actorHp = 450;
    int actorMaxHp = 500;
    int actorMp = 82;
    int actorMaxMp = 120;
    int actorExp = 1200;
    int actorNextExp = 300;
    int gold = 2500;
    int partySize = 4;
    QHash<int, int> variables;
    QHash<int, bool> switches;
    QHash<QString, int> itemCounts;
};

/// Widget declarativo do UI Designer 2.4. Widgets são dados 2D independentes
/// de QWidget e compartilham hierarquia, anchors, estados, Timeline, Events e
/// Data Binding através de UiLayoutElementSettings, usando o mesmo `id`.
struct UiWidgetSettings {
    QString id;                                ///< chave estável: widget.<uuid>
    QString screen = QStringLiteral("menu"); ///< tela do UI Designer: built-in ou screen.<id>
    QString type = QStringLiteral("panel");  ///< tipo do Widget Framework
    QString name = QStringLiteral("Widget");
    QRectF rect{0.10, 0.10, 0.24, 0.12};     ///< normalizado 0..1

    // Conteúdo e aparência local. O Style System 3.9 poderá substituir esses
    // valores por classes/temas sem quebrar projetos 3.7.
    QString text = QStringLiteral("Widget");
    QString textKey;                           ///< opcional: chave do catálogo de localização
    QString placeholder;
    QString placeholderTextKey;
    QStringList itemTextKeys;                  ///< uma chave opcional por item/lista/tab
    // UI Designer 2.17 — referência preferencial à Biblioteca de Pictures.
    // `imagePath/image` permanecem apenas para compatibilidade com projetos
    // antigos e Data Bindings dinâmicos; widgets novos escolhem uma Picture.
    QString pictureAssetId;
    QString imagePath;
    QImage image;
    QColor fillColor = QColor(30, 41, 59, 220);
    QColor textColor = Qt::white;
    QColor accentColor = QColor("#67d6ff");

    // UI Designer 2.6 — Style System. `inherit` usa o estilo associado ao
    // tipo do widget; `class` força uma Style Class; `custom` mantém as cores
    // locais. Widgets novos usam inherit; projetos antigos com aparência padrão
    // migram para inherit, enquanto cores personalizadas permanecem em custom.
    QString styleMode = QStringLiteral("inherit"); // inherit/class/custom
    QString styleClassId;
    QString customBackgroundMode = QStringLiteral("color"); // color/window-skin/nine-slice/none
    QString customSkinPath;
    QImage customSkinImage;
    QMargins customSkinSlices{12, 12, 12, 12};

    // UI Designer 2.6 / 3.10 — máscara 2D do próprio widget e clipping dos
    // filhos. image-alpha usa a transparência da imagem de máscara.
    QString maskShape = QStringLiteral("none"); // none/rect/rounded/circle/image-alpha
    double maskRadius = 8.0;
    QString maskImagePath;
    QImage maskImage;

    // Propriedades comuns para controles/listas/progresso. Mantidas genéricas
    // para que tipos compostos possam reutilizar o renderer sem subclasses.
    QString orientation = QStringLiteral("horizontal"); // horizontal/vertical
    QStringList items;
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 100.0;
    double step = 1.0;
    int selectedIndex = 0;
    int columns = 4;
    int rows = 0;
    int slotWidth = 1;
    int slotHeight = 1;
    int quantity = 1;
    bool checked = false;
    bool interactive = true;
    bool modal = false;
    bool clipChildren = false;

    // UI Designer 2.7 / 3.11 — navegação por teclado/gamepad. O modo `auto`
    // escolhe o vizinho geometricamente; `manual` usa os quatro alvos abaixo;
    // `none` remove o widget da navegação. O mesmo GameAction/InputMap atende
    // teclado e gamepad, portanto nenhuma tecla/botão fica hardcoded no Widget.
    QString navigationMode = QStringLiteral("auto"); // auto/manual/none
    QString navUp;
    QString navDown;
    QString navLeft;
    QString navRight;
    bool initialFocus = false;
    bool navigationWrap = true;

    // UI Designer 2.9 / LUDO 3.14 — Transform 2D avançado. O pivot continua
    // vindo de UiLayoutElementSettings para que Layout, Timeline e Transform
    // compartilhem o mesmo ponto de referência.
    double rotationDegrees = 0.0;          ///< -360..360
    double transformScaleX = 1.0;          ///< 0.05..10
    double transformScaleY = 1.0;
    double skewXDegrees = 0.0;             ///< -80..80
    double skewYDegrees = 0.0;
    QString blendMode = QStringLiteral("normal"); // normal/add/multiply/screen/darken/lighten
    bool materialAffectsChildren = false;
    QVector<UiMaterialEffectSettings> materialEffects;

    // UI Designer 2.10 / LUDO 3.15 — Particle Emitter 2D. O emissor usa tempo
    // determinístico do frame, portanto não depende de QWidget nem de estado
    // mutável no renderer. Quando shape=image, usa preferencialmente a Picture referenciada por `pictureAssetId`.
    bool particleEnabled = true;
    QString particleShape = QStringLiteral("circle"); // circle/square/image
    double particleSpawnRate = 18.0;       ///< partículas por segundo
    int particleMaxCount = 96;
    int particleLifetimeMs = 900;
    double particleDirectionDegrees = -90.0;
    double particleSpreadDegrees = 50.0;
    double particleSpeedMin = 25.0;
    double particleSpeedMax = 80.0;
    double particleGravityX = 0.0;
    double particleGravityY = 35.0;
    double particleStartSize = 8.0;
    double particleEndSize = 2.0;
    double particleStartOpacity = 1.0;
    double particleEndOpacity = 0.0;
    int particleFadeInMs = 0;              ///< envelope opcional de entrada (0 = sem fade extra)
    int particleFadeOutMs = 0;             ///< envelope opcional de saída (0 = sem fade extra)
    double particleEmissionWidth = 0.22;   ///< largura da área de nascimento relativa ao widget (0..4)
    double particleEmissionHeight = 0.22;  ///< altura da área de nascimento relativa ao widget (0..4)
    QColor particleStartColor = QColor("#ffffffff");
    QColor particleEndColor = QColor("#00ffffff");
    double particleSpinMin = -90.0;
    double particleSpinMax = 90.0;
    bool particleLocalSpace = true;
    bool particleBurst = false;
    int particleBurstCount = 24;
    bool particleLoop = true;
};

/// Override de um elemento dentro de um Screen State. Flags `has*` permitem
/// distinguir "herdar" de false. visualState e animationClip vazios herdam.
struct UiScreenStateElementSettings {
    bool hasVisible = false;
    bool visible = true;
    bool hasEnabled = false;
    bool enabled = true;
    QString visualState;
    QString animationClip;
};

/// Estado declarativo da tela inteira (UI Designer 2.7 / 3.12). Estados usam
/// os mesmos Widgets, Visual States e Animation Clips já existentes; não criam
/// um segundo sistema de tela/animação.
struct UiScreenStateSettings {
    QString id;                                ///< screenstate.<uuid>
    QString name = QStringLiteral("State");
    QString screen = QStringLiteral("menu");  ///< tela do UI Designer: built-in ou personalizada
    bool initial = false;
    QHash<QString, UiScreenStateElementSettings> elements;
};

struct UiLayoutElementSettings {
    QString parentId;                         ///< vazio = raiz da tela
    QString anchor = QStringLiteral("top-left");
    QPointF pivot{0.5, 0.5};
    QSizeF minSize{0.05, 0.05};              ///< normalizado (0..1)
    QSizeF maxSize{1.0, 1.0};
    bool locked = false;
    bool visible = true;
    QString layoutMode = QStringLiteral("free"); // free, horizontal, vertical, grid
    double layoutPadding = 0.02;             ///< proporção do retângulo pai
    double layoutSpacing = 0.01;             ///< proporção do retângulo pai
    int gridColumns = 2;
    int zOrder = 0;

    // UI Designer 2.1 — estados e animações No Code. Estados ausentes herdam
    // implicitamente o estado Normal. Clips usam nomes por elemento.
    QHash<QString, UiVisualStateSettings> visualStates;
    QVector<UiAnimationClipSettings> animationClips;

    // UI Designer 2.2 — eventos/condições/ações No Code.
    QVector<UiEventBindingSettings> eventBindings;

    // UI Designer 2.8 / LUDO 3.13 — Visual Logic Graph opcional. O modo
    // simples acima permanece disponível; o grafo reutiliza as mesmas ações.
    QVector<UiVisualLogicGraphSettings> visualLogicGraphs;

    // UI Designer 2.3 — Data Binding No Code. Cada propriedade pode ser ligada
    // a uma fonte de dados do jogo; o formato continua declarativo e sem QWidget.
    QVector<UiDataBindingSettings> dataBindings;
};

/// Propriedade pública de um Component/Prefab. `targetTemplateId` aponta para
/// um nó interno da definição e `property` usa nomes estáveis do Inspector
/// (text/image/fill/text-color/accent/value/checked/quantity/visible/enabled).
struct UiComponentExposedPropertySettings {
    QString id;
    QString label;
    QString targetTemplateId;
    QString property = QStringLiteral("text");
};

/// Definição reutilizável do UI Designer 2.5. Os widgets e metadados são
/// snapshots declarativos, com ids locais `node.*`, e nunca entram diretamente
/// no runtime até serem instanciados. Isso mantém Components independentes de
/// QWidget e compatíveis com o mesmo pipeline 2D já existente.
struct UiComponentDefinitionSettings {
    QString id;                              ///< component.<uuid>
    QString name = QStringLiteral("Component");
    QString category = QStringLiteral("Meus Components");
    QString description;
    QString rootTemplateId;
    QHash<QString, UiWidgetSettings> widgets;
    QHash<QString, UiLayoutElementSettings> layoutElements;
    QVector<UiComponentExposedPropertySettings> exposedProperties;
};

/// Instância de Component. `elementMap` liga ids locais da definição aos ids
/// reais `widget.*` do Canvas. Overrides são mantidos como chaves estáveis
/// `node|property`; os próprios widgets continuam sendo a fonte de verdade em
/// runtime, portanto projetos seguem robustos mesmo se uma definição for removida.
struct UiComponentInstanceSettings {
    QString id;                              ///< instance.<uuid>
    QString componentId;
    QString screen = QStringLiteral("menu");
    QHash<QString, QString> elementMap;      ///< templateId -> widgetId real
    QHash<QString, QString> overrides;       ///< chave declarativa -> valor serializado
};

/// Aparência e feedback da interface desenhada DENTRO do jogo. Não depende de
/// QWidget: o editor apenas edita estes dados e o runtime os transforma em
/// UiTheme/UiCanvas. Imagens são mantidas em memória para o playtest e o caminho
/// relativo é preservado para exportação/ResourceManager.
struct GameUiSettings {
    QString windowSkinPath;
    QImage windowSkin;
    QMargins windowSkinSlices{12, 12, 12, 12};
    QString cursorPath;
    QImage cursorImage;

    QColor windowFill = QColor(12, 16, 32, 225);
    QColor windowBorder = QColor("#cfd8ff");
    QColor innerBorder = QColor(90, 110, 180, 180);
    QColor textColor = QColor("#d7dcf0");
    QColor selectedTextColor = Qt::white;
    QColor accentColor = QColor("#cfd8ff");
    QColor selectionColor = QColor(70, 105, 190, 180);

    int paddingX = 12;
    int paddingY = 12;
    QString fontFamily; // vazio = fonte principal do projeto
    int fontSize = 16;
    int windowOpacity = 100; // percentual, 0..100

    QString openAnimation = QStringLiteral("scale-fade"); // none, fade, scale-fade, slide-*
    QString closeAnimation = QStringLiteral("fade");       // none, fade, scale-fade, slide-*
    QString animationEasing = QStringLiteral("ease-out");  // linear, ease-in, ease-out, ease-in-out, back
    int animationMs = 140;

    QString cursorSePath;
    QString confirmSePath;
    QString cancelSePath;
    int soundVolume = 80;

    // UI Designer 2.6 — Style System + Theme. A Windowskin do projeto pode
    // ser herdada automaticamente por superfícies/controles, mas cada widget
    // pode optar por uma Style Class ou visual Custom.
    QString themeName = QStringLiteral("LUDO Theme");
    bool widgetsInheritWindowSkin = true;
    QHash<QString, UiStyleClassSettings> styleClasses;
    QHash<QString, QString> widgetTypeStyles; // widget type -> style class id
    // LUDO 4.0 RC2.57 — estilos dos componentes nativos da UI in-game.
    // A chave identifica o componente (message, name-box, choices, menu, modal,
    // battle, shop, hud, selection...) e o valor referencia uma Style Class.
    // Vazio/ausente = herdar o Theme global, preservando projetos antigos.
    QHash<QString, QString> nativeComponentStyles;

    // Layouts normalizados (0..1) editáveis pelo UI Designer. Mantê-los no
    // projeto faz o mesmo desenho valer em qualquer resolução e nos dois backends.
    QRectF menuListRect{0.04, 0.18, 0.38, 0.68};
    QRectF menuDetailRect{0.44, 0.18, 0.52, 0.68};
    QRectF battleArenaRect{0.02, 0.02, 0.96, 0.48};
    QRectF battlePartyRect{0.02, 0.53, 0.56, 0.45};
    QRectF battleCommandRect{0.59, 0.53, 0.39, 0.45};
    QRectF shopListRect{0.04, 0.14, 0.44, 0.72};
    QRectF shopDetailRect{0.51, 0.14, 0.45, 0.72};

    // UI Designer 2.15 — telas livres. Os ids internos menu/battle/shop/title/save/load
    // continuam reservados, mas o projeto pode criar qualquer quantidade de telas.
    QHash<QString, QString> screenNames;      ///< screen.<uuid> -> nome amigável
    QStringList screenOrder;                  ///< ordem das telas personalizadas no Designer
    QHash<QString, bool> screenTransparent;  ///< true = sem painel/fundo base automático; Widgets continuam visíveis

    // UI Designer 2.4 — Widget Framework 2D. Widgets customizados usam ids
    // próprios e os mesmos metadados declarativos de layout/estado/eventos.
    QHash<QString, UiWidgetSettings> widgets;

    // UI Designer 2.5 — Components / Prefabs. Definições e instâncias são
    // persistidas separadamente dos widgets reais para permitir Apply/Revert,
    // biblioteca reutilizável e propriedades expostas sem afetar o runtime.
    QHash<QString, UiComponentDefinitionSettings> components;
    QHash<QString, UiComponentInstanceSettings> componentInstances;

    // UI Designer 2.7 / 3.12 — Screen States reutilizam os mesmos elementos
    // e Animation Clips. O estado inicial é marcado por tela; projetos antigos
    // simplesmente não possuem estados e preservam o comportamento 3.10.
    QHash<QString, UiScreenStateSettings> screenStates;

    // UI Designer 2.4 — metadados por elemento. Chaves estáveis:
    // menu.list/menu.detail, battle.arena/battle.party/battle.commands,
    // shop.list/shop.detail. Projetos antigos simplesmente começam com o mapa
    // vazio e recebem os padrões no primeiro uso do Designer.
    QHash<QString, UiLayoutElementSettings> layoutElements;
    bool designerSnapEnabled = true;
    int designerGridDivisions = 20;

    // UI Designer 2.3 — valores fictícios persistidos apenas para o Preview Data.
    UiDataPreviewSettings designerPreviewData;
};


struct LegacyProjectCompatState {
    // Ponto inicial da antiga runtime. No LudoMapProject moderno o RPG Maker
    // MZ é a autoridade; estes campos existem apenas durante importação.
    QString startMapId;
    QPoint startPosition{0, 0};

/// Ajustes do personagem usados pelo runtime (F5).
struct PlayerSettings {
    // ---- enumeracoes da "Configuracao de Personagem" -----------------
    /// Em quantas direcoes o charset DESENHA o personagem.
    /// 4 = so as cardeais; 8 = com diagonais (ver `diagonalsInSameRow`).
    enum SpriteDirs  { Sprite4Dir = 4, Sprite8Dir = 8 };
    /// Padroes prontos de quadros de caminhada (colunas do charset).
    /// Qualquer outro numero de 2 a 16 tambem vale: veja `animFrames`.
    enum AnimPattern { Anim3 = 3, Anim5 = 5 };
    /// Como percorrer os quadros: vaivem (0,1,2,1) ou ciclo (0,1,2,0).
    enum AnimOrder   { PingPong = 0, LoopOrder = 1 };
    /// Em quantas direcoes o personagem pode CAMINHAR (independente do
    /// numero de linhas do charset).
    enum WalkDirs    { Walk4 = 4, Walk8 = 8 };
    /// Tamanho do passo: meia celula ou uma celula inteira.
    enum MoveStep    { StepFull = 0, StepHalf = 1 };
    /// Area de colisao do personagem: 1x1 celula ou 1x0,5 (so os pes).
    enum HitboxKind  { HitboxFull = 0, HitboxHalf = 1 };

    QImage  charset;                 ///< folha de sprites (opcional)
    QString charsetPath;
    int     frameCols = 3;           ///< colunas de animacao (fatiamento)
    int     frameRows = 4;           ///< linhas = direcoes (fatiamento)
    double  tilesPerSecond = 5.0;    ///< velocidade de caminhada

    SpriteDirs  spriteDirs  = Sprite4Dir;
    /// Arranjo das 8 direcoes na folha:
    ///   false = 8 LINHAS (0..3 cardeais, 4..7 diagonais);
    ///   true  = 4 linhas, cada uma com a cardeal seguida da diagonal
    ///           (ex.: 6 quadros = 3 normais + 3 diagonais).
    bool        diagonalsInSameRow = false;
    /// Quantos quadros de caminhada o charset tem (colunas). 3 e 5 sao os
    /// padroes prontos do dialogo; folhas com 4, 6, 8... tambem funcionam.
    int         animFrames  = Anim3;
    /// Quadro usado com o personagem parado. -1 = automatico (o do meio).
    int         idleFrame   = -1;
    AnimOrder   animOrder   = PingPong;
    WalkDirs    walkDirs    = Walk4;
    bool        shadow      = true;  ///< sombra desenhada sob os pes
    MoveStep    moveStep    = StepFull;
    HitboxKind  hitbox      = HitboxFull;
    /// Footsteps: automático pelo chão quando o override fica vazio.
    bool        footstepsEnabled = true;
    QString     footstepSurfaceId;
    int         footstepVolume = 100;

    bool    hasCharset() const { return !charset.isNull(); }
    QSize   frameSize() const {
        if (charset.isNull() || frameCols <= 0 || frameRows <= 0) return QSize();
        return QSize(charset.width() / frameCols, charset.height() / frameRows);
    }
    /// Quantas LINHAS a folha tem, conforme o arranjo escolhido.
    int     charsetRows() const {
        return (spriteDirs == Sprite8Dir && !diagonalsInSameRow) ? 8 : 4;
    }
    /// Quantos quadros de caminhada existem POR DIRECAO. No arranjo
    /// "3 normais + 3 diagonais" a linha guarda duas direcoes, entao e
    /// metade das colunas.
    int     framesPerDirection() const {
        const int n = qMax(1, animFrames);
        return diagonalsInSameRow ? qMax(1, n / 2) : n;
    }
    /// Coluna em que comecam os quadros da diagonal (0 quando a folha usa
    /// linhas separadas para elas).
    int     diagonalColumnOffset() const {
        return diagonalsInSameRow ? framesPerDirection() : 0;
    }
    /// Mantem o fatiamento coerente com os padroes escolhidos.
    void    syncFrameGrid() {
        frameCols = qBound(1, animFrames, 64);
        frameRows = charsetRows();
    }
    /// Andar de celula inteira nao permite meia hitbox: o personagem
    /// sempre ocupa a celula toda (e o que o rotulo do dialogo promete).
    HitboxKind effectiveHitbox() const {
        return moveStep == StepFull ? HitboxFull : hitbox;
    }
    /// Quadro parado de verdade: o escolhido, ou o do meio. Sempre
    /// medido DENTRO da direcao (nao na linha inteira).
    int     effectiveIdleFrame() const {
        const int n = framesPerDirection();
        return (idleFrame >= 0 && idleFrame < n) ? idleFrame : n / 2;
    }
    bool    walksDiagonally() const { return walkDirs == Walk8; }
    bool    halfStep()        const { return moveStep == StepHalf; }
};
PlayerSettings player;

/// Teclas e recursos avançados do Ludo Input System.
InputMap inputMap = InputMap::defaults();
InputSystemSettings inputSystem;
CutsceneSkipSettings cutsceneSkip;
TitleScreenSettings titleScreen;
GameUiSettings gameUi;
LocalizationSettings localization;
GameAccessibilitySettings accessibility;

/// Padrões das legendas (Configurações do Jogo > Jogabilidade).
SubtitleStyle subtitleStyle;
// RC2.55 / Bloco D: presets personalizados do motor unificado de texto.
// Built-ins não são serializados; só overrides/presets criados pelo autor.
QHash<QString, TextEffectPreset> textEffectPresets;

/// Resolucao LOGICA do jogo, em pixels: e o tamanho da "tela" que o autor
/// desenha. A janela pode ser maior — a imagem é ampliada proporcionalmente
/// e centralizada, com barras pretas em volta e filtro suave. Sem isto,
/// maximizar a janela mostrava MAIS MAPA em vez de ampliar o jogo, e a
/// interface mudava de tamanho.
QSize gameResolution = QSize(800, 600);
/// Padrões do Player exportado. Diferente das preferências locais de F5/F6,
/// estes valores pertencem ao projeto e viajam no .ludo.
QString runtimeGpuBackend = QStringLiteral("auto");
QString runtimeScaleFilter = QStringLiteral("nearest");
bool autosaveEnabled = false;
int autosaveSlot = 99;
int checkpointSlot = 97;
bool quickSaveEnabled = true;
int quickSaveSlot = 98;

/// Folha de icones do projeto: usada por \I[n] nas mensagens, legendas e
/// imagens de texto.
IconSet iconSet;

/// Fontes disponíveis ao runtime. A principal vale para mensagens, HUD,
/// legendas e textos que não escolherem uma família específica.
QVector<ProjectFont> projectFonts;
QString mainFontFamily;

// ---- banco de dados do jogo ------------------------------------------
/// Interruptores e variaveis: SO os valores iniciais e os nomes. O valor
/// que muda durante a partida vive no runtime (game::GameState).
QVector<SwitchDef>   switches;
QVector<VariableDef> variables;
QVector<StringDef>   strings;
QVector<CommonEvent> commonEvents;
QVector<CommandTemplate> commandTemplates;
SpeakerDatabase speakerDatabase;
/// Superfícies de passos compartilhadas por tiles, terrenos e atores.
QVector<FootstepSurface> footstepSurfaces;
FootstepSettings footstepSettings;
QHash<QString,QVector<DatabaseRecord>> database;
/// Bancos No-Code do projeto. Estrutura/valores read-only ficam aqui;
/// bancos Runtime ganham uma cópia mutável em game::GameState.
QVector<CustomDatabaseDefinition> customDatabases;
QVector<NoCodePlugin> plugins;

/// Nome legivel de um interruptor/variavel (para a interface).
QString switchName(int id) const;
QString variableName(int id) const;
QString stringName(int id) const;
bool    switchInitial(int id) const;
int     variableInitial(int id) const;
QString stringInitial(int id) const;
const CommonEvent* commonEventByNumber(int numero) const;
const CommonEvent* commonEventById(const QString& id) const;
CommonEvent* commonEventById(const QString& id);
const FootstepSurface* footstepSurfaceById(const QString& id) const;
FootstepSurface* footstepSurfaceById(const QString& id);
const CustomDatabaseDefinition* customDatabase(const QString& id) const;
CustomDatabaseDefinition* customDatabase(const QString& id);

// ---- biblioteca de imagens (pictures) --------------------------------
/// Imagens que os eventos podem mostrar na tela. Ficam embutidas no
/// projeto, como o charset e os tilesets: um arquivo so, sem caminho
/// quebrado quando o autor move a pasta.
QVector<PictureAsset> pictures;

const PictureAsset* pictureById(const QString& id) const;
const PictureAsset* pictureByName(const QString& nome) const;
/// Procura pelo id e, se nao achar, pelo nome (o nome e o que sobrevive
/// quando alguem monta o .json na mao).
const PictureAsset* pictureFor(const QString& id, const QString& nome) const;
int  pictureIndexById(const QString& id) const;

    void resetLegacyProjectState();
};

} // namespace core
