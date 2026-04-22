#pragma once

#include "LabsCore.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace Labs {

class IPlugin;

class LABSCORE_API PluginHost : public QObject {
    Q_OBJECT
public:
    explicit PluginHost(QObject* parent = nullptr);
    ~PluginHost() override;

    int loadAll(const QString& pluginDir);
    void unloadAll();

    const QVector<IPlugin*>& plugins() const { return m_plugins; }

private:
    QVector<IPlugin*> m_plugins;
};

} // namespace Labs
