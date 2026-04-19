#pragma once
// OutputPanel.h — Log/output display plugin

#include "../helios_core/IPlugin.h"
#include <QObject>
#include <QString>

namespace Helios {

class OutputPanelPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit OutputPanelPlugin(QObject* parent = nullptr);
    ~OutputPanelPlugin() override = default;

    QString  name()        const override { return "OutputPanel"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "Log output display"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override {}
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

public slots:
    void appendLine(const QString& line);
    void appendError(const QString& line);
    void clear();

private:
    class QTextEdit* m_output = nullptr;
    PluginContext m_ctx;
};

} // namespace Helios
