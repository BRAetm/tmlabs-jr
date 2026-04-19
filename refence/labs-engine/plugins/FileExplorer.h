#pragma once
// FileExplorer.h — File browser with GCVWorker script templates

#include "../helios_core/IPlugin.h"
#include <QObject>
#include <QString>

namespace Helios {

class FileExplorerPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit FileExplorerPlugin(QObject* parent = nullptr);
    ~FileExplorerPlugin() override = default;

    QString  name()        const override { return "FileExplorer"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "File browser with GCVWorker templates"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override {}
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

signals:
    void fileSelected(const QString& path);
    void templateRequested(const QString& templateName);

private slots:
    void onItemDoubleClicked(const class QModelIndex& index);
    void onNewFromTemplate();

private:
    class QFileSystemModel* m_model    = nullptr;
    class QTreeView*        m_tree     = nullptr;
    QString                 m_rootPath;
    PluginContext            m_ctx;
};

} // namespace Helios
