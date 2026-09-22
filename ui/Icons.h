// ============================================================================
//  Icons.h — Ícones do editor + personalização local do usuário.
//
//  Os recursos incorporados continuam sendo o fallback seguro. O usuário pode
//  substituir qualquer ícone do catálogo nas Preferências; o arquivo escolhido
//  é copiado para a pasta de configuração local do LUDO e nunca é salvo no
//  projeto .ludo.
// ============================================================================
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace ui { namespace icons {

struct Descriptor {
    QString key;
    QString label;
};

/// Ícone por nome. Primeiro tenta a substituição local do usuário; depois usa
/// resources/custom_icons e, por fim, o fallback vetorial QPainter.
QIcon get(const QString& name, const QColor& tint = QColor());

/// Versão em QPixmap (para botões que precisam do bitmap direto).
QPixmap pixmap(const QString& name, int size, const QColor& tint = QColor());

/// Renderiza somente o ícone de fábrica, ignorando substituições locais.
QPixmap defaultPixmap(const QString& name, int size, const QColor& tint = QColor());

/// Renderiza um arquivo externo para prévia (PNG/JPG/WEBP/SVG/ICO...).
QPixmap filePixmap(const QString& filePath, int size);

/// Lista estável dos ícones que podem ser personalizados pela interface.
QVector<Descriptor> catalog();

/// true quando existe PNG/SVG de fábrica dedicado para a chave. Fallbacks
/// vetoriais continuam funcionando, mas aparecem como "Criar ícone" no editor.
bool hasDedicatedArtwork(const QString& name);
QStringList missingArtworkKeys();

/// Preset portátil de ícones. O arquivo .ludoicons é JSON e incorpora os
/// arquivos personalizados em Base64, então pode ser levado para outra
/// instalação/projeto sem depender dos caminhos originais.
bool exportPreset(const QString& filePath, QString* error = nullptr);
bool importPreset(const QString& filePath, QString* error = nullptr);

/// Caminho da substituição local ativa. Vazio significa "usar padrão".
QString customIconPath(const QString& name);

/// Copia sourceFile para a pasta local do LUDO e passa a usá-lo para `name`.
bool setCustomIcon(const QString& name, const QString& sourceFile, QString* error = nullptr);
void clearCustomIcon(const QString& name);
void clearAllCustomIcons();

/// Tamanho preferido dos ícones das toolbars principais.
int toolbarIconSize();
void setToolbarIconSize(int size);

/// Reaplica ícones já criados em ações/botões e atualiza o tamanho das toolbars.
/// Pode ser chamado depois de salvar as Preferências sem reiniciar o editor.
void refreshApplicationIcons(QWidget* root = nullptr);

}} // namespace ui::icons
