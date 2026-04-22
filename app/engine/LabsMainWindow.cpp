#include "LabsMainWindow.h"
#include "ControllerMonitorWidget.h"
#include "LabsBackgroundWidget.h"
#include "LabsLogoWidget.h"
#include "LabsTheme.h"
#include "LabsThemeDialog.h"

#include "IPlugin.h"
#include "PluginHost.h"
#include "SettingsManager.h"

#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QEvent>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace Labs {

// Frame-sink fan-out so one source can feed multiple sinks (display + CV).
class LabsMainWindow::FanOutFrameSink : public IFrameSink {
public:
    void addSink(IFrameSink* s) { if (s) m_sinks.push_back(s); }
    void pushFrame(const Frame& frame) override {
        for (auto* s : m_sinks) s->pushFrame(frame);
    }
private:
    std::vector<IFrameSink*> m_sinks;
};

// Simple controller fan-out with one always-on monitor slot and a swappable
// output slot (ViGEm vs PS, depending on mode).
class FanOutCtrlSink : public IControllerSink {
public:
    void setMonitor(IControllerSink* s)  { m_monitor = s; }
    void setOutput(IControllerSink* s)   { m_output  = s; }
    void pushState(const ControllerState& state) override {
        if (m_monitor) m_monitor->pushState(state);
        if (m_output)  m_output ->pushState(state);
    }
private:
    IControllerSink* m_monitor = nullptr;
    IControllerSink* m_output  = nullptr;
};
static FanOutCtrlSink s_ctrlFanOut;

// Small helpers for consistent labels. All style comes from LabsTheme.cpp's
// central stylesheet via object names — nothing is hardcoded here so the
// theme dialog re-tints them when the user picks a different accent.
static QLabel* eyebrowLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text.toUpper(), parent);
    l->setObjectName(QStringLiteral("eyebrow"));
    return l;
}
static QLabel* sectionLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("sectionTitle"));
    return l;
}
// Reads LabsSharp's paired-host store at %AppData%/PSRemotePlay/ and copies the
// registration credentials into our settings (base64 strings in the same form
// PSRemotePlayPlugin::start() expects). No-op if our settings already have a
// regist key, or if LabsSharp's files aren't present.
static void importLabsSharpPairing(SettingsManager* s)
{
    if (!s) return;
    if (!s->value(QStringLiteral("ps/registKey")).toByteArray().isEmpty()) return;

    const QString appData = QString::fromLocal8Bit(qgetenv("APPDATA"));
    if (appData.isEmpty()) return;
    const QString base        = appData + QStringLiteral("/PSRemotePlay");
    const QString hostsPath   = base + QStringLiteral("/hosts.json");
    const QString accountPath = base + QStringLiteral("/account.txt");

    QFile hostsFile(hostsPath);
    if (!hostsFile.exists() || !hostsFile.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(hostsFile.readAll());
    hostsFile.close();
    if (!doc.isObject()) return;
    const QJsonObject hosts = doc.object();
    if (hosts.isEmpty()) return;

    // Take the first paired host.
    const QJsonObject host = hosts.begin().value().toObject();
    const QString registKey = host.value(QStringLiteral("RpRegistKey")).toString();
    const QString rpKey     = host.value(QStringLiteral("RpKey")).toString();
    const QString apName    = host.value(QStringLiteral("ApName")).toString();
    if (registKey.isEmpty() || rpKey.isEmpty()) return;

    s->setValue(QStringLiteral("ps/registKey"), registKey.toLatin1());
    s->setValue(QStringLiteral("ps/morning"),   rpKey.toLatin1());
    s->setValue(QStringLiteral("ps/isPs5"),     apName.startsWith(QStringLiteral("PS5"), Qt::CaseInsensitive));

    // Account ID is in a sibling text file.
    QFile accFile(accountPath);
    if (accFile.open(QIODevice::ReadOnly)) {
        const QString acc = QString::fromUtf8(accFile.readAll()).trimmed();
        accFile.close();
        if (!acc.isEmpty())
            s->setValue(QStringLiteral("ps/psnAccountId"), acc);
    }
    s->sync();
}

static QFrame* hSeparator(QWidget* parent)
{
    auto* s = new QFrame(parent);
    s->setObjectName(QStringLiteral("hrSep"));
    s->setFrameShape(QFrame::HLine);
    s->setFixedHeight(1);
    return s;
}

// ── ctor ────────────────────────────────────────────────────────────────────

LabsMainWindow::LabsMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_settings(std::make_unique<SettingsManager>())
    , m_pluginHost(std::make_unique<PluginHost>(this))
{
    setWindowTitle(QStringLiteral("Labs Engine — PS Remote Play"));
    importLabsSharpPairing(m_settings.get());
    const QByteArray geom  = m_settings->value(QStringLiteral("window/geometry")).toByteArray();
    const QByteArray state = m_settings->value(QStringLiteral("window/state")).toByteArray();
    if (!geom.isEmpty())  restoreGeometry(geom);
    else                  resize(1320, 720);
    if (!state.isEmpty()) restoreState(state);

    m_fpsTimer = new QTimer(this);
    m_fpsTimer->setInterval(1000);
    connect(m_fpsTimer, &QTimer::timeout, this, &LabsMainWindow::onFpsTick);

    // Build the three-column body: top bar above a H-split of rails + stage,
    // with a log strip at the bottom.
    QWidget* top   = buildTopBar();
    QWidget* left  = buildScriptsRail();
    QWidget* stage = buildCenterStage();
    QWidget* right = buildDevicesRail();

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(left);
    body->addWidget(stage, 1);
    body->addWidget(right);

    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("logStrip"));
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(90);

    auto* logRow = new QHBoxLayout();
    logRow->setContentsMargins(14, 0, 10, 0);
    logRow->setSpacing(8);
    auto* logLabel = new QLabel(QStringLiteral("log"), this);
    logLabel->setObjectName(QStringLiteral("logLabel"));
    auto* clearBtn = new QPushButton(QStringLiteral("clear"), this);
    connect(clearBtn, &QPushButton::clicked, this, &LabsMainWindow::onClearLog);
    logRow->addWidget(logLabel);
    logRow->addWidget(m_log, 1);
    logRow->addWidget(clearBtn);

    auto* root = new QVBoxLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(top);
    root->addLayout(body, 1);
    root->addLayout(logRow);

    m_bgWidget = new LabsBackgroundWidget(this);
    m_bgWidget->setLayout(root);
    setCentralWidget(m_bgWidget);

    // Load plugins + wire.
    const QString pluginDir = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
    const int n = m_pluginHost->loadAll(pluginDir);
    PluginContext ctx;
    ctx.settings = m_settings.get();
    m_fanOut = std::make_unique<FanOutFrameSink>();

    IControllerSource* cvCtrlSource = nullptr;
    IControllerSource* xinputCtrlSource = nullptr;

    for (IPlugin* p : m_pluginHost->plugins()) {
        try { p->initialize(ctx); }
        catch (const std::exception& e) { appendLog(QStringLiteral("plugin %1 init threw: %2").arg(p->name(), e.what())); continue; }
        catch (...)                     { appendLog(QStringLiteral("plugin %1 init threw unknown").arg(p->name())); continue; }
        m_pluginsByName.insert(p->name(), p);
        if (auto* sp = dynamic_cast<IFrameSourcePlugin*>(p); sp)
            m_frameSources.insert(p->name(), sp->frameSource());
        if (auto* kp = dynamic_cast<IFrameSinkPlugin*>(p); kp)
            m_fanOut->addSink(kp->frameSink());
        if (auto* sp = dynamic_cast<IControllerSourcePlugin*>(p); sp) {
            if      (p->name() == QStringLiteral("CV Python")) cvCtrlSource    = sp->controllerSource();
            else if (p->name() == QStringLiteral("XInput"))    xinputCtrlSource = sp->controllerSource();
        }
        if (p->name() == QStringLiteral("XInput")) {
            if (auto* sp = dynamic_cast<IControllerSourcePlugin*>(p))
                m_xinputSource = sp->controllerSource();
        }
        if (auto* kp = dynamic_cast<IControllerSinkPlugin*>(p); kp)
            m_ctrlSinks.insert(p->name(), kp->controllerSink());

        // Mount the first UIPlugin into the center stage.
        if (auto* ui = dynamic_cast<IUIPlugin*>(p); ui && m_stageHost && m_stageHost->layout()->count() == 0) {
            QWidget* w = ui->createWidget(m_stageHost);
            m_stageHost->layout()->addWidget(w);
        }
    }

    m_ctrlSource = cvCtrlSource ? cvCtrlSource : xinputCtrlSource;

    // Single controller source pipe: primary source → fan-out → [monitor, mode output].
    s_ctrlFanOut.setMonitor(m_monitor);
    if (m_ctrlSource) {
        m_ctrlSource->setSink(&s_ctrlFanOut);
        m_ctrlSource->start();
    }

    const int savedMode = m_settings->value(QStringLiteral("session/mode"), 0).toInt();
    m_modeBox->setCurrentIndex(qBound(0, savedMode, 1));
    applyMode(static_cast<Mode>(m_modeBox->currentIndex()));

    connect(m_modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LabsMainWindow::onModeChanged);

    refreshDevicesList();
    appendLog(QStringLiteral("plugins: %1  |  sources: %2  |  ctrl sinks: %3")
              .arg(n).arg(m_frameSources.size()).arg(m_ctrlSinks.size()));
    applyThemeImage();
    updateActions();
}

void LabsMainWindow::applyThemeImage()
{
    if (!m_bgWidget || !m_settings) return;
    const LabsThemeData t = labsThemeLoad(m_settings.get());
    m_bgWidget->setImage(t.imagePath);
    m_bgWidget->setOpacity(t.imageOpacity);
}

LabsMainWindow::~LabsMainWindow()
{
    stopActiveSource();
    if (m_ctrlSource) m_ctrlSource->stop();
    if (m_scriptProc && m_scriptProc->state() != QProcess::NotRunning) {
        m_scriptProc->terminate();
        m_scriptProc->waitForFinished(1000);
    }
}

void LabsMainWindow::closeEvent(QCloseEvent* event)
{
    if (m_settings) {
        m_settings->setValue(QStringLiteral("window/geometry"), saveGeometry());
        m_settings->setValue(QStringLiteral("window/state"),    saveState());
        m_settings->setValue(QStringLiteral("session/mode"),    m_modeBox->currentIndex());
        if (m_scriptCombo) {
            m_settings->setValue(QStringLiteral("cv/scriptPath"), m_scriptCombo->currentText());
        }
        m_settings->sync();
    }
    QMainWindow::closeEvent(event);
}

// ── top bar ─────────────────────────────────────────────────────────────────

QWidget* LabsMainWindow::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(56);

    auto* logo = new LabsLogoWidget(bar);

    auto* wordmark = new QLabel(QStringLiteral("labs engine"), bar);
    wordmark->setObjectName(QStringLiteral("wordmark"));

    auto* version = new QLabel(QStringLiteral("v1.0 · creator studio"), bar);
    version->setObjectName(QStringLiteral("versionTag"));

    auto* titleColumn = new QVBoxLayout();
    titleColumn->setContentsMargins(0, 0, 0, 0);
    titleColumn->setSpacing(2);
    titleColumn->addWidget(wordmark);
    titleColumn->addWidget(version);

    auto* modeLabel = new QLabel(QStringLiteral("mode"), bar);
    modeLabel->setObjectName(QStringLiteral("modeLabel"));

    m_modeBox = new QComboBox(bar);
    m_modeBox->addItem(QStringLiteral("Xbox (WGC)"));
    m_modeBox->addItem(QStringLiteral("PS Remote Play"));
    m_modeBox->setMinimumWidth(170);

    m_btnPick  = new QPushButton(QStringLiteral("pick window"), bar);
    m_btnPair  = new QPushButton(QStringLiteral("pair…"),       bar);
    auto* btnTheme = new QPushButton(QStringLiteral("theme…"),  bar);
    m_btnStart = new QPushButton(QStringLiteral("start"),       bar);
    m_btnStop  = new QPushButton(QStringLiteral("stop"),        bar);
    m_btnStart->setProperty("accent", true);

    connect(m_btnPick,  &QPushButton::clicked, this, &LabsMainWindow::onPickWindow);
    connect(m_btnPair,  &QPushButton::clicked, this, &LabsMainWindow::onPair);
    connect(btnTheme,   &QPushButton::clicked, this, &LabsMainWindow::onOpenTheme);
    connect(m_btnStart, &QPushButton::clicked, this, &LabsMainWindow::onStart);
    connect(m_btnStop,  &QPushButton::clicked, this, &LabsMainWindow::onStop);

    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(20, 0, 18, 0);
    row->setSpacing(12);
    row->addWidget(logo);
    row->addSpacing(6);
    row->addLayout(titleColumn);
    row->addStretch();
    row->addWidget(modeLabel);
    row->addWidget(m_modeBox);
    row->addWidget(m_btnPick);
    row->addWidget(m_btnPair);
    row->addWidget(btnTheme);
    row->addWidget(m_btnStart);
    row->addWidget(m_btnStop);

    return bar;
}

// ── left rail (scripts) ─────────────────────────────────────────────────────

QWidget* LabsMainWindow::buildScriptsRail()
{
    auto* rail = new QWidget(this);
    rail->setObjectName(QStringLiteral("leftRail"));
    rail->setFixedWidth(270);

    auto* col = new QVBoxLayout(rail);
    col->setContentsMargins(22, 22, 22, 22);
    col->setSpacing(0);

    col->addWidget(sectionLabel(QStringLiteral("scripts"), rail));
    col->addSpacing(14);

    auto* eb = eyebrowLabel(QStringLiteral("active script"), rail);
    col->addWidget(eb);
    col->addSpacing(4);

    m_scriptCombo = new QComboBox(rail);
    m_scriptCombo->setEditable(true);
    m_scriptCombo->setInsertPolicy(QComboBox::NoInsert);
    {
        QString saved = m_settings ? m_settings->value(QStringLiteral("cv/scriptPath")).toString() : QString();
        if (saved.isEmpty() || !QFileInfo::exists(saved)) {
            const QString candidate = QDir::cleanPath(
                QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../../../labs-engine/scripts/SecretK.py"));
            saved = QFileInfo::exists(candidate) ? candidate : QString();
        }
        if (!saved.isEmpty()) {
            m_scriptCombo->addItem(saved);
            m_scriptCombo->setCurrentText(saved);
        }
    }
    connect(m_scriptCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { updateActions(); });
    col->addWidget(m_scriptCombo);

    col->addSpacing(10);
    m_scriptBrowseBtn = new QPushButton(QStringLiteral("browse…"), rail);
    connect(m_scriptBrowseBtn, &QPushButton::clicked, this, &LabsMainWindow::onBrowseScript);
    col->addWidget(m_scriptBrowseBtn);

    col->addSpacing(8);
    auto* scriptBtnRow = new QHBoxLayout();
    scriptBtnRow->setSpacing(8);
    m_scriptRunBtn  = new QPushButton(QStringLiteral("run"), rail);
    m_scriptStopBtn = new QPushButton(QStringLiteral("stop"), rail);
    m_scriptStopBtn->setEnabled(false);
    m_scriptRunBtn ->setProperty("accent", true);
    connect(m_scriptRunBtn,  &QPushButton::clicked, this, &LabsMainWindow::onRunScript);
    connect(m_scriptStopBtn, &QPushButton::clicked, this, &LabsMainWindow::onStopScript);
    scriptBtnRow->addWidget(m_scriptRunBtn);
    scriptBtnRow->addWidget(m_scriptStopBtn);
    col->addLayout(scriptBtnRow);

    col->addSpacing(16);
    col->addWidget(eyebrowLabel(QStringLiteral("status"), rail));
    col->addSpacing(4);
    m_scriptStatus = new QLabel(QStringLiteral("idle"), rail);
    m_scriptStatus->setObjectName(QStringLiteral("statusMono"));
    col->addWidget(m_scriptStatus);

    col->addStretch();
    col->addWidget(hSeparator(rail));
    col->addSpacing(10);
    col->addWidget(eyebrowLabel(QStringLiteral("settings ini"), rail));
    col->addSpacing(4);
    auto* settingsPath = new QLabel(m_settings ? m_settings->filePath() : QString(), rail);
    settingsPath->setObjectName(QStringLiteral("pathBlue"));
    settingsPath->setWordWrap(true);
    col->addWidget(settingsPath);

    return rail;
}

// ── center stage ────────────────────────────────────────────────────────────

QWidget* LabsMainWindow::buildCenterStage()
{
    auto* stage = new QWidget(this);
    stage->setObjectName(QStringLiteral("stage"));

    // Tab strip.
    auto* tabs = new QWidget(stage);
    tabs->setObjectName(QStringLiteral("stageTabs"));
    tabs->setFixedHeight(38);

    m_tabVideo = new QLabel(QStringLiteral("video display"), tabs);
    m_tabVideo->setObjectName(QStringLiteral("tabActive"));
    m_tabVideo->setCursor(Qt::PointingHandCursor);
    m_tabVideo->installEventFilter(this);

    m_tabMonitor = new QLabel(QStringLiteral("controller monitor"), tabs);
    m_tabMonitor->setObjectName(QStringLiteral("tabInactive"));
    m_tabMonitor->setCursor(Qt::PointingHandCursor);
    m_tabMonitor->installEventFilter(this);

    m_targetLabel = new QLabel(QStringLiteral(""), tabs);
    m_targetLabel->setObjectName(QStringLiteral("targetText"));

    m_fpsLabel = new QLabel(QStringLiteral("— fps"), tabs);
    m_fpsLabel->setObjectName(QStringLiteral("fpsPill"));

    auto* tabsRow = new QHBoxLayout(tabs);
    tabsRow->setContentsMargins(0, 0, 10, 0);
    tabsRow->setSpacing(0);
    tabsRow->addWidget(m_tabVideo);
    tabsRow->addWidget(m_tabMonitor);
    tabsRow->addStretch();
    tabsRow->addWidget(m_targetLabel);
    tabsRow->addWidget(m_fpsLabel);

    m_stagePages = new QStackedWidget(stage);
    m_stagePages->setObjectName(QStringLiteral("stage"));

    // Page 0: video display — IUIPlugin widget mounts here later.
    m_stageHost = new QWidget(m_stagePages);
    m_stageHost->setObjectName(QStringLiteral("stage"));
    auto* hostLayout = new QVBoxLayout(m_stageHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    m_stagePages->addWidget(m_stageHost);

    // Page 1: controller monitor.
    m_monitor = new ControllerMonitorWidget(m_stagePages);
    m_stagePages->addWidget(m_monitor);

    auto* col = new QVBoxLayout(stage);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(tabs);
    col->addWidget(m_stagePages, 1);

    return stage;
}

bool LabsMainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonRelease) {
        if (obj == m_tabVideo && m_stagePages) {
            m_stagePages->setCurrentIndex(0);
            m_tabVideo  ->setObjectName(QStringLiteral("tabActive"));
            m_tabMonitor->setObjectName(QStringLiteral("tabInactive"));
            m_tabVideo  ->style()->polish(m_tabVideo);
            m_tabMonitor->style()->polish(m_tabMonitor);
            return true;
        }
        if (obj == m_tabMonitor && m_stagePages) {
            m_stagePages->setCurrentIndex(1);
            m_tabMonitor->setObjectName(QStringLiteral("tabActive"));
            m_tabVideo  ->setObjectName(QStringLiteral("tabInactive"));
            m_tabMonitor->style()->polish(m_tabMonitor);
            m_tabVideo  ->style()->polish(m_tabVideo);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

// ── right rail (devices) ────────────────────────────────────────────────────

QWidget* LabsMainWindow::buildDevicesRail()
{
    auto* rail = new QWidget(this);
    rail->setObjectName(QStringLiteral("rightRail"));
    rail->setFixedWidth(240);

    auto* col = new QVBoxLayout(rail);
    col->setContentsMargins(22, 22, 22, 22);
    col->setSpacing(0);

    col->addWidget(sectionLabel(QStringLiteral("devices"), rail));
    col->addSpacing(14);

    m_devicesList = new QLabel(QStringLiteral("…"), rail);
    m_devicesList->setObjectName(QStringLiteral("deviceList"));
    m_devicesList->setWordWrap(true);
    m_devicesList->setAlignment(Qt::AlignTop);
    col->addWidget(m_devicesList);
    col->addStretch();

    return rail;
}

void LabsMainWindow::refreshDevicesList()
{
    if (!m_devicesList) return;
    QStringList lines;
    for (auto it = m_pluginsByName.constBegin(); it != m_pluginsByName.constEnd(); ++it) {
        lines << QStringLiteral("• ") + it.key();
    }
    m_devicesList->setText(lines.join(QStringLiteral("\n")));
}

// ── mode wiring ─────────────────────────────────────────────────────────────

void LabsMainWindow::applyMode(Mode mode)
{
    stopActiveSource();

    const QString sourceName = (mode == Mode::Xbox)
        ? QStringLiteral("WGC Capture")
        : QStringLiteral("PS Remote Play");

    const QString ctrlSinkName = (mode == Mode::Xbox)
        ? QStringLiteral("ViGEm Output")
        : QStringLiteral("PS Remote Play");

    m_activeSource   = m_frameSources.value(sourceName,  nullptr);
    m_activeCtrlSink = m_ctrlSinks   .value(ctrlSinkName, nullptr);

    if (m_activeSource) m_activeSource->setSink(m_fanOut.get());

    // Swap the output half of the fan-out; monitor half stays permanently
    // connected so the pad visualization keeps updating regardless of mode.
    s_ctrlFanOut.setOutput(m_activeCtrlSink);

    // In PS mode, skip XInput slot 0 (the real pad) so SecretK's virtual X360
    // on slot 1+ is what we forward to the PS5. In Xbox mode, use all slots.
    if (IPlugin* xi = m_pluginsByName.value(QStringLiteral("XInput"), nullptr)) {
        const int skipMask = (mode == Mode::PS) ? 0x01 : 0x00;
        QMetaObject::invokeMethod(xi->qobject(), "setSkipMask", Qt::DirectConnection,
                                  Q_ARG(int, skipMask));
    }

    updateActions();
}

void LabsMainWindow::stopActiveSource()
{
    if (m_activeSource) m_activeSource->stop();
    m_fpsTimer->stop();
    if (m_fpsLabel) m_fpsLabel->setText(QStringLiteral("— fps"));
}

void LabsMainWindow::onModeChanged(int index)
{
    applyMode(static_cast<Mode>(qBound(0, index, 1)));
    appendLog(QStringLiteral("mode: %1").arg(m_modeBox->currentText()));
}

// ── actions ─────────────────────────────────────────────────────────────────

void LabsMainWindow::onPickWindow()
{
    if (!m_activeSource) return;
    const quintptr hwnd = static_cast<quintptr>(winId());
    if (m_activeSource->showPicker(hwnd)) {
        appendLog(QStringLiteral("target → %1").arg(m_activeSource->targetLabel()));
    }
    updateActions();
}

void LabsMainWindow::onPair()
{
    const Mode mode = static_cast<Mode>(m_modeBox->currentIndex());
    const QString name = (mode == Mode::PS) ? QStringLiteral("PS Remote Play") : QString();
    if (name.isEmpty()) return;
    IPlugin* p = m_pluginsByName.value(name, nullptr);
    if (auto* pr = dynamic_cast<IPairablePlugin*>(p)) {
        if (pr->pair(this)) appendLog(QStringLiteral("paired"));
    }
}

void LabsMainWindow::onStart()
{
    if (m_settings && m_scriptCombo) {
        m_settings->setValue(QStringLiteral("cv/scriptPath"), m_scriptCombo->currentText());
    }
    if (!m_activeSource) { appendLog(QStringLiteral("no source for this mode")); return; }
    if (m_activeSource->start()) {
        m_lastFrameCount = m_activeSource->frameCount();
        m_fpsTimer->start();
        const QString label = m_activeSource->targetLabel();
        if (m_targetLabel) m_targetLabel->setText(label);
        m_scriptStatus->setText(QStringLiteral("running"));
        appendLog(QStringLiteral("capture started: %1").arg(label.isEmpty() ? QStringLiteral("(no target)") : label));
    } else {
        const Mode mode = static_cast<Mode>(m_modeBox->currentIndex());
        appendLog(mode == Mode::PS
            ? QStringLiteral("PS start failed — check [ps] settings (pair first)")
            : QStringLiteral("start failed — pick a window first"));
    }
    updateActions();
}

void LabsMainWindow::onStop()
{
    stopActiveSource();
    if (m_targetLabel) m_targetLabel->setText(QString());
    m_scriptStatus->setText(QStringLiteral("idle"));
    appendLog(QStringLiteral("stopped"));
    updateActions();
}

void LabsMainWindow::onFpsTick()
{
    if (!m_activeSource || !m_fpsLabel) return;
    const qint64 now = m_activeSource->frameCount();
    const qint64 delta = now - m_lastFrameCount;
    m_lastFrameCount = now;
    m_fpsLabel->setText(QStringLiteral("%1 fps").arg(delta));
}

void LabsMainWindow::onBrowseScript()
{
    const QString start = m_scriptCombo ? m_scriptCombo->currentText() : QString();
    const QString picked = QFileDialog::getOpenFileName(this,
        QStringLiteral("Pick CV script"),
        start.isEmpty() ? QDir::homePath() : QFileInfo(start).absolutePath(),
        QStringLiteral("Python (*.py);;All files (*.*)"));
    if (picked.isEmpty()) return;
    if (m_scriptCombo) {
        int idx = m_scriptCombo->findText(picked);
        if (idx < 0) { m_scriptCombo->addItem(picked); idx = m_scriptCombo->count() - 1; }
        m_scriptCombo->setCurrentIndex(idx);
    }
    if (m_settings) m_settings->setValue(QStringLiteral("cv/scriptPath"), picked);
    appendLog(QStringLiteral("script → %1").arg(picked));
}

void LabsMainWindow::onRunScript()
{
    const QString path = m_scriptCombo ? m_scriptCombo->currentText().trimmed() : QString();
    if (path.isEmpty()) {
        appendLog(QStringLiteral("select a script first"));
        return;
    }
    if (m_scriptProc && m_scriptProc->state() != QProcess::NotRunning) return;

    if (!m_scriptProc) {
        m_scriptProc = new QProcess(this);
        m_scriptProc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_scriptProc, &QProcess::readyRead, this, [this]() {
            if (m_log) m_log->appendPlainText(QString::fromLocal8Bit(m_scriptProc->readAll()).trimmed());
        });
        connect(m_scriptProc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) { onScriptFinished(code, 0); });
    }

    const QFileInfo fi(path);
    m_scriptProc->setWorkingDirectory(fi.absolutePath());
    m_scriptProc->start(QStringLiteral("python"), { path });

    if (!m_scriptProc->waitForStarted(3000)) {
        appendLog(QStringLiteral("failed to start python — is it on PATH?"));
        return;
    }

    m_scriptStatus->setText(QStringLiteral("running"));
    appendLog(QStringLiteral("script started: %1").arg(fi.fileName()));
    updateActions();
    if (m_settings) m_settings->setValue(QStringLiteral("cv/scriptPath"), path);
}

void LabsMainWindow::onStopScript()
{
    if (!m_scriptProc || m_scriptProc->state() == QProcess::NotRunning) return;
    m_scriptProc->terminate();
    if (!m_scriptProc->waitForFinished(2000))
        m_scriptProc->kill();
}

void LabsMainWindow::onScriptFinished(int exitCode, int /*exitStatus*/)
{
    m_scriptStatus->setText(exitCode == 0
        ? QStringLiteral("exited")
        : QStringLiteral("exited (%1)").arg(exitCode));
    appendLog(QStringLiteral("script finished (exit %1)").arg(exitCode));
    updateActions();
}

void LabsMainWindow::onClearLog()
{
    if (m_log) m_log->clear();
}

void LabsMainWindow::onOpenTheme()
{
    LabsThemeDialog dlg(m_settings.get(), this);
    connect(&dlg, &LabsThemeDialog::themeChanged,
            this, &LabsMainWindow::applyThemeImage);
    dlg.exec();
    applyThemeImage();  // final state on close (handles cancel revert too)
}

void LabsMainWindow::updateActions()
{
    const bool haveSource = (m_activeSource != nullptr);
    const bool running = haveSource && m_activeSource->isRunning();
    const Mode mode = static_cast<Mode>(m_modeBox ? m_modeBox->currentIndex() : 0);
    const bool scriptRunning = m_scriptProc && m_scriptProc->state() != QProcess::NotRunning;

    if (m_btnPick)      m_btnPick ->setEnabled(haveSource && !running && mode == Mode::Xbox);
    if (m_btnPair)      m_btnPair ->setEnabled(!running && mode == Mode::PS);
    if (m_btnStart)     m_btnStart->setEnabled(haveSource && !running);
    if (m_btnStop)      m_btnStop ->setEnabled(haveSource &&  running);
    if (m_modeBox)      m_modeBox ->setEnabled(!running);
    const bool haveScript = m_scriptCombo && !m_scriptCombo->currentText().trimmed().isEmpty();
    if (m_scriptRunBtn)  m_scriptRunBtn ->setEnabled(haveSource && haveScript && !scriptRunning);
    if (m_scriptStopBtn) m_scriptStopBtn->setEnabled(scriptRunning);
}

void LabsMainWindow::appendLog(const QString& text)
{
    if (m_log) m_log->appendPlainText(text);
}

} // namespace Labs
