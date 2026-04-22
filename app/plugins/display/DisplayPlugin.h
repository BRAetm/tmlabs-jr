#pragma once

#include "IPlugin.h"

#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QWidget>

namespace Labs {

class DisplaySurface : public QWidget {
    Q_OBJECT
public:
    explicit DisplaySurface(QWidget* parent = nullptr);
    void setImage(const QImage& img);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
    QMutex m_mx;
};

class DisplayPlugin : public QObject, public IUIPlugin, public IFrameSinkPlugin, public IFrameSink {
    Q_OBJECT
public:
    DisplayPlugin();
    ~DisplayPlugin() override;

    QString name()        const override { return QStringLiteral("Display"); }
    QString author()      const override { return QStringLiteral("Labs"); }
    QString description() const override { return QStringLiteral("Renders captured frames"); }
    QString version()     const override { return QStringLiteral("0.1.0"); }

    void initialize(const PluginContext& ctx) override;
    void shutdown() override;

    QObject* qobject() override { return this; }
    QWidget* createWidget(QWidget* parent) override;

    // IFrameSinkPlugin
    IFrameSink* frameSink() override { return this; }

    // IFrameSink
    void pushFrame(const Frame& frame) override;

private:
    QPointer<DisplaySurface> m_surface;
};

} // namespace Labs
