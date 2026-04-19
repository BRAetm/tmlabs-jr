#pragma once
// LicenseService.h — Helios auth / license system

#include <QObject>
#include <QString>

namespace Helios {

enum class LicenseStatus {
    Unknown,
    Valid,
    Invalid,
    Expired,
    NoSession,
};

struct LicenseInfo {
    LicenseStatus status;
    QString       discordId;
    QString       discordUsername;
    QString       discordAvatar;
    QString       sessionToken;      // Bearer token
    bool          authenticated;
    bool          requiresHWID;
};

class LicenseService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticationChanged)
    Q_PROPERTY(QString discordId  READ discordId       NOTIFY authenticationChanged)

public:
    explicit LicenseService(QObject* parent = nullptr);
    ~LicenseService() override;

    bool    isAuthenticated() const;
    QString discordId()       const;
    QString sessionToken()    const;

    // Auth headers injected into all API calls:
    //   Authorization: Bearer <sessionToken>
    //   X-Helios-API-Version: 4
    //   X-Discord-Username: <username>
    //   X-Discord-GlobalName: <globalName>
    //   X-Discord-Avatar: <avatar>
    //   User-Agent: Helios/2.0
    //   Origin: https://www.inputsense.com
    //   Referer: https://www.inputsense.com/

public slots:
    void performLicenseCheck();
    void refreshSession();

signals:
    void authenticationChanged(bool authenticated);
    void authenticationSucceeded(const LicenseInfo& info);
    void authenticationFailed(const QString& reason);
    void sessionValidated();
    void sessionRefreshed();
    void newSessionToken(const QString& token);

private:
    // session.dat — AES-GCM encrypted, HWID-bound
    // Keys: Helios_Session_AES_2025 / Helios_Session_Encrypt_2025 (legacy)
    // Path: %APPDATA%\HeliosProject\Helios\session.dat
    bool loadSessionFile();
    bool decryptSession(const QByteArray& encrypted, QByteArray& decrypted);

    // API: POST https://helios.inputsense.com/api
    //   /api/scripts/onnx/get-key-v2  — model key download
    bool validateWithServer(const QString& token);

    QString m_sessionToken;
    QString m_discordId;
    bool    m_authenticated = false;
    bool    m_requiresHWID  = false;

    // HWID: HeliosHWID_v3_Stable
    QString computeHWID();
};

} // namespace Helios
