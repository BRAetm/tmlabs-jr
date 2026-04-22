#include "PairPSDialog.h"
#include "PsnLoginDialog.h"
#include "SettingsManager.h"

#include <QByteArray>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>

#include <cstring>

extern "C" {
#include <labs/regist.h>
#include <labs/log.h>
}

namespace Labs {

// ── PSN OAuth constants (same as chiaki-ng psn-account-id.py) ────────────────
static const QString kClientId     = QStringLiteral("ba495a24-818c-472b-b12d-ff231c1b5745");
static const QString kClientSecret = QStringLiteral("mvaiZkRsAsI1IBkY");
static const QString kRedirectUri  = QStringLiteral("https://remoteplay.dl.playstation.net/remoteplay/redirect");
static const QString kTokenUrl     = QStringLiteral("https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token");

static QString buildAuthUrl()
{
    return QStringLiteral(
        "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"
        "?service_entity=urn:service-entity:psn"
        "&response_type=code"
        "&client_id=ba495a24-818c-472b-b12d-ff231c1b5745"
        "&redirect_uri=https%3A%2F%2Fremoteplay.dl.playstation.net%2Fremoteplay%2Fredirect"
        "&scope=psn:clientapp"
        "&request_locale=en_US"
        "&ui=pr"
        "&service_logo=ps"
        "&layout_type=popup"
        "&smcid=remoteplay"
        "&prompt=always"
        "&PlatformPrivacyWs1=minimal");
}

// ── native impl ──────────────────────────────────────────────────────────────

struct PairPSDialog::Impl {
    LabsLog    log {};
    LabsRegist regist {};
    bool       registInited = false;
    QByteArray hostUtf8;
    QByteArray psnBytes;
    PairPSDialog* owner = nullptr;

    static void logCb(LabsLogLevel, const char* msg, void* user) {
        auto* impl = static_cast<Impl*>(user);
        qInfo().noquote() << "[regist]" << msg;
        if (impl && impl->owner) {
            const QString m = QString::fromUtf8(msg);
            QMetaObject::invokeMethod(impl->owner, [impl, m]() {
                impl->owner->reportStatus(m);
            }, Qt::QueuedConnection);
        }
    }
};

// ── constructor ──────────────────────────────────────────────────────────────

PairPSDialog::PairPSDialog(SettingsManager* settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_impl(new Impl())
{
    m_impl->owner = this;
    setWindowTitle(QStringLiteral("Pair PlayStation"));
    setModal(true);
    setMinimumWidth(460);

    // ── Step 1 — PSN account ──────────────────────────────────────────────────
    auto* step1Label = new QLabel(QStringLiteral("<b>STEP 1 — Link PlayStation Account</b>"), this);

    // Load saved account ID from settings
    if (m_settings)
        m_accountIdBase64 = m_settings->value(QStringLiteral("ps/psnAccountId")).toString();

    // Linked status (text set after widget exists)
    m_linkedLabel = new QLabel(this);
    m_linkedLabel->setWordWrap(true);

    auto* signInBtn = new QPushButton(QStringLiteral("Sign in with PSN  →"), this);
    signInBtn->setToolTip(QStringLiteral("Opens an embedded browser — sign in and the app handles the rest"));

    auto* step1Layout = new QVBoxLayout();
    step1Layout->setSpacing(6);
    step1Layout->addWidget(step1Label);
    step1Layout->addWidget(m_linkedLabel);
    step1Layout->addWidget(signInBtn);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: #444;"));

    // ── Step 2 — Console pairing ──────────────────────────────────────────────
    auto* step2Label = new QLabel(QStringLiteral("<b>STEP 2 — Pair Console</b>"), this);

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(QStringLiteral("192.168.1.42"));
    if (m_settings)
        m_hostEdit->setText(m_settings->value(QStringLiteral("ps/hostIp")).toString());

    m_ps5Check = new QCheckBox(QStringLiteral("PS5  (uncheck for PS4)"), this);
    m_ps5Check->setChecked(m_settings ? m_settings->value(QStringLiteral("ps/isPs5"), true).toBool() : true);

    m_pinEdit = new QLineEdit(this);
    m_pinEdit->setPlaceholderText(QStringLiteral("8-digit PIN shown on console"));
    m_pinEdit->setMaxLength(8);
    m_pinEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{1,8}")), this));

    auto* consoleHint = new QLabel(
        QStringLiteral("On the console:  Settings → Remote Play → Link Device  — then enter the PIN shown."), this);
    consoleHint->setWordWrap(true);
    consoleHint->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));

    auto* form = new QFormLayout();
    form->setSpacing(8);
    form->addRow(QStringLiteral("Console IP:"), m_hostEdit);
    form->addRow(QString(), m_ps5Check);
    form->addRow(QStringLiteral("PIN:"), m_pinEdit);

    m_pairBtn   = new QPushButton(QStringLiteral("Pair"), this);
    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(m_pairBtn);
    btnRow->addWidget(m_cancelBtn);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #888;"));

    auto* step2Layout = new QVBoxLayout();
    step2Layout->setSpacing(6);
    step2Layout->addWidget(step2Label);
    step2Layout->addLayout(form);
    step2Layout->addWidget(consoleHint);

    // ── Root ──────────────────────────────────────────────────────────────────
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(16, 16, 16, 16);
    root->addLayout(step1Layout);
    root->addWidget(divider);
    root->addLayout(step2Layout);
    root->addWidget(m_status);
    root->addLayout(btnRow);

    updateLinkedLabel();   // now safe — m_linkedLabel is constructed

    connect(signInBtn,  &QPushButton::clicked, this, &PairPSDialog::onSignInClicked);
    connect(m_pairBtn,  &QPushButton::clicked, this, &PairPSDialog::onPairClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PairPSDialog::onCancelClicked);
}

PairPSDialog::~PairPSDialog()
{
    if (m_impl) {
        if (m_impl->registInited) {
            labs_regist_stop(&m_impl->regist);
            labs_regist_fini(&m_impl->regist);
        }
        delete m_impl;
    }
}

// ── Step 1: OAuth ─────────────────────────────────────────────────────────────

void PairPSDialog::onSignInClicked()
{
    auto* dlg = new PsnLoginDialog(this);
    connect(dlg, &PsnLoginDialog::accountIdReady, this, &PairPSDialog::setAccountLinked);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

void PairPSDialog::setAccountLinked(const QString& accountIdBase64)
{
    m_accountIdBase64 = accountIdBase64;
    if (m_settings) {
        m_settings->setValue(QStringLiteral("ps/psnAccountId"), accountIdBase64);
        m_settings->sync();
    }
    updateLinkedLabel();
    reportStatus(QStringLiteral("Account linked. Now enter your console IP and PIN below."), true);
}

void PairPSDialog::updateLinkedLabel()
{
    if (m_accountIdBase64.isEmpty()) {
        m_linkedLabel->setText(QStringLiteral("<span style='color:#888'>Not linked — sign in below.</span>"));
    } else {
        m_linkedLabel->setText(
            QStringLiteral("<span style='color:#3a7'>✓ Account linked</span>  "
                           "<span style='color:#666; font-size:11px'>") +
            m_accountIdBase64 + QStringLiteral("</span>"));
    }
}

// ── Step 2: Pairing ───────────────────────────────────────────────────────────

void PairPSDialog::onPairClicked()
{
    if (m_accountIdBase64.isEmpty()) {
        reportStatus(QStringLiteral("Complete Step 1 first — sign in with PSN to link your account."), false, true);
        return;
    }

    const QString host   = m_hostEdit->text().trimmed();
    const QString pinStr = m_pinEdit->text().trimmed();

    if (host.isEmpty()) { reportStatus(QStringLiteral("Enter the console's IP address."), false, true); return; }

    m_impl->psnBytes = QByteArray::fromBase64(m_accountIdBase64.toLatin1());
    if (m_impl->psnBytes.size() != LABS_PSN_ACCOUNT_ID_SIZE) {
        reportStatus(QStringLiteral("Saved account ID is corrupt — re-link your PSN account."), false, true);
        return;
    }

    bool pinOk = false;
    const uint32_t pin = pinStr.toUInt(&pinOk);
    if (!pinOk || pinStr.size() != 8) {
        reportStatus(QStringLiteral("PIN must be exactly 8 digits shown on the console."), false, true);
        return;
    }

    setBusy(true);
    reportStatus(QStringLiteral("Pairing… keep the PIN screen visible on the console."));

    labs_log_init(&m_impl->log, LABS_LOG_ALL, &Impl::logCb, m_impl);
    m_impl->hostUtf8 = host.toUtf8();

    LabsRegistInfo info {};
    info.target         = m_ps5Check->isChecked() ? LABS_TARGET_PS5_1 : LABS_TARGET_PS4_10;
    info.host           = m_impl->hostUtf8.constData();
    info.broadcast      = false;
    info.psn_online_id  = nullptr;
    info.holepunch_info = nullptr;
    std::memcpy(info.psn_account_id, m_impl->psnBytes.constData(), LABS_PSN_ACCOUNT_ID_SIZE);
    info.pin        = pin;
    info.console_pin = 0;

    m_registRunning.store(true);

    static auto trampoline = [](LabsRegistEvent* event, void* user) {
        auto* self = static_cast<PairPSDialog*>(user);
        if (!self || !event) return;
        QByteArray registKey, morning;
        bool isPs5 = true;
        if (event->type == LABS_REGIST_EVENT_TYPE_FINISHED_SUCCESS && event->registered_host) {
            const LabsRegisteredHost* h = event->registered_host;
            registKey = QByteArray(h->rp_regist_key, LABS_SESSION_AUTH_SIZE);
            morning   = QByteArray(reinterpret_cast<const char*>(h->rp_key), 0x10);
            isPs5     = (h->target == LABS_TARGET_PS5_1) || (h->target == LABS_TARGET_PS5_UNKNOWN);
        }
        self->handleRegistEvent(static_cast<int>(event->type), registKey, morning, isPs5);
    };

    const LabsErrorCode rc = labs_regist_start(&m_impl->regist, &m_impl->log, &info,
        static_cast<LabsRegistCb>(+trampoline), this);
    if (rc != LABS_ERR_SUCCESS) {
        m_registRunning.store(false);
        reportStatus(QStringLiteral("labs_regist_start failed (%1)").arg(static_cast<int>(rc)), false, true);
        setBusy(false);
        return;
    }
    m_impl->registInited = true;
}

void PairPSDialog::onCancelClicked()
{
    if (m_registRunning.load() && m_impl->registInited)
        labs_regist_stop(&m_impl->regist);
    reject();
}

// ── regist callback ───────────────────────────────────────────────────────────

void PairPSDialog::handleRegistEvent(int typeInt, const QByteArray& registKey,
                                     const QByteArray& morning, bool isPs5)
{
    const auto type = static_cast<LabsRegistEventType>(typeInt);
    const QString host = m_hostEdit->text().trimmed();

    if (type == LABS_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
        QMetaObject::invokeMethod(this, [this, registKey, morning, isPs5, host]() {
            if (m_settings) {
                m_settings->setValue(QStringLiteral("ps/hostIp"),    host);
                m_settings->setValue(QStringLiteral("ps/isPs5"),     isPs5);
                m_settings->setValue(QStringLiteral("ps/registKey"), registKey.toBase64());
                m_settings->setValue(QStringLiteral("ps/morning"),   morning.toBase64());
                m_settings->sync();
            }
            m_registRunning.store(false);
            setBusy(false);
            reportStatus(QStringLiteral("Paired successfully. Close this dialog and click Start."), true);
            m_pairBtn->setText(QStringLiteral("Done"));
            disconnect(m_pairBtn, &QPushButton::clicked, this, &PairPSDialog::onPairClicked);
            connect(m_pairBtn,    &QPushButton::clicked, this, &QDialog::accept);
        }, Qt::QueuedConnection);

    } else if (type == LABS_REGIST_EVENT_TYPE_FINISHED_FAILED) {
        QMetaObject::invokeMethod(this, [this]() {
            m_registRunning.store(false);
            setBusy(false);
            reportStatus(QStringLiteral("Pairing failed — check IP, account link, and PIN."), false, true);
        }, Qt::QueuedConnection);

    } else if (type == LABS_REGIST_EVENT_TYPE_FINISHED_CANCELED) {
        QMetaObject::invokeMethod(this, [this]() {
            m_registRunning.store(false);
            setBusy(false);
            reportStatus(QStringLiteral("Pairing canceled."), false, true);
        }, Qt::QueuedConnection);
    }
}

// ── helpers ───────────────────────────────────────────────────────────────────

void PairPSDialog::setBusy(bool busy)
{
    m_hostEdit->setEnabled(!busy);
    m_ps5Check->setEnabled(!busy);
    m_pinEdit->setEnabled(!busy);
    m_pairBtn->setEnabled(!busy);
}

void PairPSDialog::reportStatus(const QString& msg, bool ok, bool err)
{
    QString color = ok ? QStringLiteral("#3a7") : (err ? QStringLiteral("#d55") : QStringLiteral("#888"));
    m_status->setStyleSheet(QStringLiteral("color: ") + color + QStringLiteral(";"));
    m_status->setText(msg);
}

} // namespace Labs
