#include "PsnLoginDialog.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace Labs {

static const QString kClientId     = QStringLiteral("ba495a24-818c-472b-b12d-ff231c1b5745");
static const QString kClientSecret = QStringLiteral("mvaiZkRsAsI1IBkY");
static const QString kRedirectPrefix =
    QStringLiteral("https://remoteplay.dl.playstation.net/remoteplay/redirect");
static const QString kTokenUrl     =
    QStringLiteral("https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token");

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

PsnLoginDialog::PsnLoginDialog(QWidget* parent)
    : QDialog(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Link PlayStation Account"));
    setModal(true);
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    auto* step1 = new QLabel(
        QStringLiteral("<b>Step 1</b> — Click the button to open Sony's sign-in page in your browser."), this);
    step1->setWordWrap(true);
    root->addWidget(step1);

    m_openBtn = new QPushButton(QStringLiteral("Open Sony sign-in page  ↗"), this);
    connect(m_openBtn, &QPushButton::clicked, this, &PsnLoginDialog::onOpenBrowser);
    root->addWidget(m_openBtn);

    auto* step2 = new QLabel(
        QStringLiteral("<b>Step 2</b> — After signing in, your browser will show a page that "
                       "\"can't be reached\". Copy the full URL from the address bar and paste it below."),
        this);
    step2->setWordWrap(true);
    root->addWidget(step2);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(
        QStringLiteral("https://remoteplay.dl.playstation.net/remoteplay/redirect?code=…"));
    root->addWidget(m_urlEdit);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setVisible(false);
    root->addWidget(m_status);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_confirmBtn = new QPushButton(QStringLiteral("Get Account ID"), this);
    connect(m_confirmBtn, &QPushButton::clicked, this, &PsnLoginDialog::onConfirm);
    btnRow->addWidget(m_confirmBtn);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);
}

void PsnLoginDialog::onOpenBrowser()
{
    QDesktopServices::openUrl(QUrl(buildAuthUrl()));
    setStatus(QStringLiteral("Browser opened — sign in, then paste the redirect URL above."));
}

void PsnLoginDialog::onConfirm()
{
    const QString pasted = m_urlEdit->text().trimmed();
    if (pasted.isEmpty()) {
        setStatus(QStringLiteral("Paste the redirect URL first."), true);
        return;
    }

    const QString code = QUrlQuery(QUrl(pasted)).queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        setStatus(QStringLiteral("No code= found in that URL — make sure you copied the full address bar URL."), true);
        return;
    }

    m_confirmBtn->setEnabled(false);
    setStatus(QStringLiteral("Exchanging code…"));

    const QByteArray creds = (kClientId + QStringLiteral(":") + kClientSecret).toUtf8().toBase64();

    QNetworkRequest req{QUrl{kTokenUrl}};
    req.setRawHeader("Authorization", "Basic " + creds);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"),   QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code"),         code);
    body.addQueryItem(QStringLiteral("redirect_uri"), kRedirectPrefix);

    QNetworkReply* reply = m_nam->post(req, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onTokenReply(reply); });
}

void PsnLoginDialog::onTokenReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_confirmBtn->setEnabled(true);
        setStatus(QStringLiteral("Token exchange failed: ") + reply->errorString(), true);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QString token = doc.object().value(QStringLiteral("access_token")).toString();
    if (token.isEmpty()) {
        m_confirmBtn->setEnabled(true);
        setStatus(QStringLiteral("Sony response missing access_token."), true);
        return;
    }

    setStatus(QStringLiteral("Getting account info…"));

    const QByteArray creds = (kClientId + QStringLiteral(":") + kClientSecret).toUtf8().toBase64();
    QNetworkRequest req{QUrl{kTokenUrl + QStringLiteral("/") +
                             QString::fromUtf8(QUrl::toPercentEncoding(token))}};
    req.setRawHeader("Authorization", "Basic " + creds);

    QNetworkReply* infoReply = m_nam->get(req);
    connect(infoReply, &QNetworkReply::finished, this, [this, infoReply]() { onInfoReply(infoReply); });
}

void PsnLoginDialog::onInfoReply(QNetworkReply* reply)
{
    reply->deleteLater();
    m_confirmBtn->setEnabled(true);

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QStringLiteral("Account info failed: ") + reply->errorString(), true);
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QString userIdStr = root.value(QStringLiteral("user_id")).toString();
    if (userIdStr.isEmpty()) {
        setStatus(QStringLiteral("PSN response missing user_id."), true);
        return;
    }

    bool ok = false;
    const quint64 userId = userIdStr.toULongLong(&ok);
    if (!ok) {
        setStatus(QStringLiteral("Unexpected user_id: ") + userIdStr, true);
        return;
    }

    // Sony's token-info response usually includes online_id (the human-readable
    // PSN username). Save whatever Sony sent — fall back to empty if absent.
    m_psnUsername = root.value(QStringLiteral("online_id")).toString();

    QByteArray bytes(8, 0);
    for (int i = 0; i < 8; ++i)
        bytes[i] = static_cast<char>((userId >> (8 * i)) & 0xFF);

    m_accountId = QString::fromLatin1(bytes.toBase64());
    emit accountIdReady(m_accountId, m_psnUsername);
    accept();
}

void PsnLoginDialog::setStatus(const QString& msg, bool err)
{
    m_status->setStyleSheet(err
        ? QStringLiteral("color: #d55;")
        : QStringLiteral("color: #888;"));
    m_status->setText(msg);
    m_status->setVisible(!msg.isEmpty());
}

} // namespace Labs
