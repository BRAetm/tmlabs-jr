#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;

namespace Labs {

// Opens the Sony OAuth URL in the user's default browser, then accepts the
// redirect URL pasted back by the user, exchanges the code, derives the PSN
// account ID, and emits accountIdReady().
class PsnLoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit PsnLoginDialog(QWidget* parent = nullptr);
    ~PsnLoginDialog() override = default;

    QString accountIdBase64() const { return m_accountId; }
    QString psnUsername()    const { return m_psnUsername; }

signals:
    void accountIdReady(const QString& accountIdBase64, const QString& username);

private slots:
    void onOpenBrowser();
    void onConfirm();
    void onTokenReply(QNetworkReply* reply);
    void onInfoReply(QNetworkReply* reply);

private:
    void setStatus(const QString& msg, bool err = false);

    QLabel*               m_hint     = nullptr;
    QLineEdit*            m_urlEdit  = nullptr;
    QPushButton*          m_openBtn  = nullptr;
    QPushButton*          m_confirmBtn = nullptr;
    QLabel*               m_status   = nullptr;
    QNetworkAccessManager* m_nam     = nullptr;
    QString               m_accountId;
    QString               m_psnUsername;
};

} // namespace Labs
