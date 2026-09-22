// ============================================================================
// UniversalAssetPicker.h — seletor de imagens do LUDO Map Editor.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/AssetPickerCatalog.h"

#include <QDialog>
#include <QImage>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace ui {

class UniversalAssetPickerDialog final : public QDialog
{
public:
    struct Request {
        QString context;
        QString requiredMediaType = QStringLiteral("image");
        bool multiple = false;
        QString title;
        QString initialPath;
    };

    UniversalAssetPickerDialog(core::Editor& editor, const Request& request,
                               QWidget* parent = nullptr);

    QString selectedPath() const;
    QStringList selectedPaths() const;

    static QString chooseOne(core::Editor& editor, QWidget* parent,
                             const QString& context = QString(),
                             const QString& requiredMediaType = QStringLiteral("image"),
                             const QString& title = QString());
    static QStringList chooseMany(core::Editor& editor, QWidget* parent,
                                  const QString& context = QString(),
                                  const QString& requiredMediaType = QStringLiteral("image"),
                                  const QString& title = QString());

private:
    void synchronizeAssetDatabase(bool showErrors = false);
    void rebuildTree();
    void refreshPreview();
    void applyImageZoom();
    void addExternalFiles();
    QString currentCategoryId() const;
    QString currentDestinationDirectory() const;
    void selectAbsolutePath(const QString& absolutePath);
    bool selectionIsAcceptable() const;
    QString imageFileDialogFilter() const;

    core::Editor& m_ed;
    Request m_request;
    core::AssetPickerScope m_scope;
    QComboBox* m_category = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
    QScrollArea* m_previewScroll = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_path = nullptr;
    QLabel* m_info = nullptr;
    QComboBox* m_zoom = nullptr;
    QPushButton* m_external = nullptr;
    QCheckBox* m_pixelArt2x = nullptr;
    QImage m_previewImage;
    QString m_previewImagePath;
    quint64 m_previewGeneration = 0;
};

} // namespace ui
