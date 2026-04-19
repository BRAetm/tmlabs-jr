// OutputPanel.cpp — Log/output display plugin

#include "OutputPanel.h"
#include "../helios_core/LoggingService.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QScrollBar>

namespace Helios {

OutputPanelPlugin::OutputPanelPlugin(QObject* parent) : QObject(parent) {}

void OutputPanelPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
    if (m_ctx.logging) {
        connect(m_ctx.logging, &LoggingService::logMessage,
                this, &OutputPanelPlugin::appendLine);
        connect(m_ctx.logging, &LoggingService::logError,
                this, &OutputPanelPlugin::appendError);
    }
}

QWidget* OutputPanelPlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);

    auto* bar    = new QHBoxLayout;
    auto* btnClear = new QPushButton("Clear", w);
    bar->addStretch();
    bar->addWidget(btnClear);

    m_output = new QTextEdit(w);
    m_output->setReadOnly(true);
    m_output->setFontFamily("Consolas");
    m_output->setFontPointSize(9);
    m_output->document()->setMaximumBlockCount(5000);

    lay->addLayout(bar);
    lay->addWidget(m_output);

    connect(btnClear, &QPushButton::clicked, this, &OutputPanelPlugin::clear);
    return w;
}

void OutputPanelPlugin::appendLine(const QString& line)
{
    if (!m_output) return;
    m_output->append(line);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void OutputPanelPlugin::appendError(const QString& line)
{
    if (!m_output) return;
    m_output->append(QString("<span style='color:#ff6b6b'>%1</span>").arg(line.toHtmlEscaped()));
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void OutputPanelPlugin::clear()
{
    if (m_output) m_output->clear();
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::OutputPanelPlugin();
}
