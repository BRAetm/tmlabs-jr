#pragma once

#include "FrameTypes.h"

#include <QObject>
#include <Windows.h>
#include <atomic>
#include <memory>

namespace Labs {

class SettingsManager;

class WgcSource : public QObject, public IFrameSource {
    Q_OBJECT
public:
    explicit WgcSource(QObject* parent = nullptr);
    ~WgcSource() override;

    void setSettings(SettingsManager* settings);
    void setTargetWindow(HWND hwnd, const QString& label = {});

    // Show the native Windows Graphics Capture picker dialog.
    // Parent HWND is used so the picker parents to the Labs main window.
    // Returns true if user selected a target. Does NOT start capture.
    bool pickTarget(HWND parent);

    void setSink(IFrameSink* sink) override { m_sink = sink; }
    bool start() override;
    void stop()  override;
    bool isRunning()   const override { return m_running.load(); }
    qint64 frameCount() const override { return m_frameCount.load(); }
    QString targetLabel() const override { return m_targetLabel; }
    bool showPicker(quintptr parentWindow) override { return pickTarget(reinterpret_cast<HWND>(parentWindow)); }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    IFrameSink*       m_sink     = nullptr;
    SettingsManager*  m_settings = nullptr;
    HWND              m_target   = nullptr;
    QString           m_targetLabel;
    std::atomic<bool> m_running     { false };
    std::atomic<qint64> m_frameCount { 0 };
};

} // namespace Labs
