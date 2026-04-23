#pragma once

#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <memory>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace Labs {

class PluginHost;
class SettingsManager;
class IPlugin;
class IFrameSource;
class IFrameSink;
class IControllerSource;
class IControllerSink;
class LabsBackgroundWidget;
class ControllerMonitorWidget;
class Ps5Discovery;

class LabsMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LabsMainWindow(QWidget* parent = nullptr);
    ~LabsMainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onModeChanged(int index);
    void onPickWindow();
    void onPair();
    void onStart();
    void onStop();
    void onFpsTick();
    void onBrowseScript();
    void onRunScript();
    void onStopScript();
    void onScriptFinished(int exitCode, int exitStatus);
    void onClearLog();
    void onOpenTheme();

private:
    class FanOutFrameSink;

    enum class Mode { Xbox = 0, PS = 1 };

    QWidget* buildTopBar();
    QWidget* buildScriptsRail();
    QWidget* buildCenterStage();
    QWidget* buildDevicesRail();

    void applyMode(Mode mode);
    void stopActiveSource();
    void updateActions();
    void refreshDevicesList();
    void appendLog(const QString& text);

    void applyThemeImage();  // re-reads imagePath from settings and updates root

    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<PluginHost>      m_pluginHost;
    std::unique_ptr<FanOutFrameSink> m_fanOut;

    QMap<QString, IFrameSource*>       m_frameSources;
    QMap<QString, IControllerSink*>    m_ctrlSinks;
    QMap<QString, IPlugin*>            m_pluginsByName;
    IControllerSource*                 m_ctrlSource   = nullptr;

    IFrameSource*    m_activeSource   = nullptr;
    IControllerSink* m_activeCtrlSink = nullptr;

    // Top bar controls.
    QComboBox*   m_modeBox    = nullptr;
    QPushButton* m_btnPick    = nullptr;
    QPushButton* m_btnPair    = nullptr;
    QPushButton* m_btnStart   = nullptr;
    QPushButton* m_btnStop    = nullptr;
    QLabel*      m_statePill  = nullptr;

    // Left rail.
    QComboBox*   m_scriptCombo     = nullptr;
    QPushButton* m_scriptBrowseBtn = nullptr;
    QPushButton* m_scriptRunBtn    = nullptr;
    QPushButton* m_scriptStopBtn   = nullptr;
    QLabel*      m_scriptStatus    = nullptr;
    QProcess*    m_scriptProc      = nullptr;
    QPushButton* m_perfLiteBtn     = nullptr;
    QPushButton* m_perfProBtn      = nullptr;

    // Center stage.
    QLabel*          m_targetLabel = nullptr;
    QLabel*          m_fpsLabel    = nullptr;
    QWidget*         m_stageHost   = nullptr;
    QStackedWidget*  m_stagePages  = nullptr;
    QLabel*          m_tabVideo    = nullptr;
    QLabel*          m_tabMonitor  = nullptr;
    ControllerMonitorWidget* m_monitor = nullptr;
    IControllerSource* m_xinputSource = nullptr;   // always-on feed for the monitor

    // Right rail.
    QLabel*      m_devicesList = nullptr;

    // Log strip.
    QPlainTextEdit* m_log = nullptr;
    LabsBackgroundWidget* m_bgWidget = nullptr;

    QTimer*      m_fpsTimer = nullptr;
    qint64       m_lastFrameCount = 0;

    Ps5Discovery* m_ps5Discovery = nullptr;
};

} // namespace Labs
