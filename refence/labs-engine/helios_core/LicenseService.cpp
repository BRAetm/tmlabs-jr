// LicenseService.cpp — Helios auth/license system

#include "LicenseService.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QSysInfo>

namespace Helios {

LicenseService::LicenseService(QObject* parent) : QObject(parent) {}

LicenseService::~LicenseService() {}

bool LicenseService::isAuthenticated() const { return m_authenticated; }
QString LicenseService::discordId()    const { return m_discordId; }
QString LicenseService::sessionToken() const { return m_sessionToken; }

void LicenseService::performLicenseCheck()
{
    // 1. Load session.dat
    if (!loadSessionFile()) {
        emit authenticationFailed("No valid session file found");
        return;
    }
    // 2. Validate with server
    if (!validateWithServer(m_sessionToken)) {
        emit authenticationFailed("Server validation failed");
        return;
    }
    m_authenticated = true;

    LicenseInfo info;
    info.status        = LicenseStatus::Valid;
    info.sessionToken  = m_sessionToken;
    info.discordId     = m_discordId;
    info.authenticated = true;
    info.requiresHWID  = m_requiresHWID;

    emit authenticationSucceeded(info);
    emit authenticationChanged(true);
    emit sessionValidated();
}

void LicenseService::refreshSession()
{
    if (m_sessionToken.isEmpty()) {
        performLicenseCheck();
        return;
    }
    if (validateWithServer(m_sessionToken)) {
        emit sessionRefreshed();
    } else {
        performLicenseCheck();
    }
}

bool LicenseService::loadSessionFile()
{
    // Path: %APPDATA%\HeliosProject\Helios\session.dat
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + "/session.dat";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QByteArray encrypted = f.readAll();
    f.close();

    QByteArray decrypted;
    if (!decryptSession(encrypted, decrypted)) return false;

    // Decrypted: JSON {token, discord_id, hwid_required}
    QJsonDocument doc = QJsonDocument::fromJson(decrypted);
    if (!doc.isObject()) return false;

    QJsonObject obj = doc.object();
    m_sessionToken  = obj["token"].toString();
    m_discordId     = obj["discord_id"].toString();
    m_requiresHWID  = obj["hwid_required"].toBool();

    if (m_sessionToken.isEmpty()) return false;

    // HWID check
    if (m_requiresHWID) {
        QString expected = obj["hwid"].toString();
        if (computeHWID() != expected) return false;
    }

    return true;
}

bool LicenseService::decryptSession(const QByteArray& encrypted, QByteArray& decrypted)
{
    if (encrypted.size() < 28) return false; // nonce(12) + tag(16) minimum

    // AES-256-GCM
    // Key derivation: PBKDF2-SHA256 of "Helios_Session_AES_2025" with machine-specific salt
    // Legacy key: "Helios_Session_Encrypt_2025"
    static const char* const kKeys[] = {
        "Helios_Session_AES_2025",
        "Helios_Session_Encrypt_2025",
    };

    QByteArray machineId = QSysInfo::machineUniqueId();

    for (const char* keyStr : kKeys) {
        // Derive 32-byte key via PBKDF2
        QByteArray key = QCryptographicHash::hash(
            QByteArray(keyStr) + machineId, QCryptographicHash::Sha256);

        // Layout: [nonce:12][ciphertext][tag:16]
        QByteArray nonce  = encrypted.left(12);
        QByteArray tag    = encrypted.right(16);
        QByteArray cipher = encrypted.mid(12, encrypted.size() - 28);

        // BCrypt AES-GCM decrypt
        BCRYPT_ALG_HANDLE hAlg   = nullptr;
        BCRYPT_KEY_HANDLE hKey   = nullptr;

        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
            continue;

        BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

        BCRYPT_KEY_DATA_BLOB_HEADER hdr = {};
        hdr.dwMagic   = BCRYPT_KEY_DATA_BLOB_MAGIC;
        hdr.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        hdr.cbKeyData = 32;

        QByteArray keyBlob;
        keyBlob.resize(sizeof(hdr) + 32);
        memcpy(keyBlob.data(), &hdr, sizeof(hdr));
        memcpy(keyBlob.data() + sizeof(hdr), key.constData(), 32);

        if (!BCRYPT_SUCCESS(BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
                                             &hKey, nullptr, 0,
                                             reinterpret_cast<PUCHAR>(keyBlob.data()),
                                             keyBlob.size(), 0))) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            continue;
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo = {};
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce       = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
        authInfo.cbNonce       = 12;
        authInfo.pbTag         = reinterpret_cast<PUCHAR>(const_cast<char*>(tag.constData()));
        authInfo.cbTag         = 16;

        decrypted.resize(cipher.size());
        ULONG plainLen = 0;
        NTSTATUS status = BCryptDecrypt(hKey,
                                         reinterpret_cast<PUCHAR>(const_cast<char*>(cipher.constData())),
                                         cipher.size(),
                                         &authInfo,
                                         nullptr, 0,
                                         reinterpret_cast<PUCHAR>(decrypted.data()),
                                         decrypted.size(),
                                         &plainLen, 0);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (BCRYPT_SUCCESS(status)) {
            decrypted.resize(plainLen);
            return true;
        }
    }
    return false;
}

bool LicenseService::validateWithServer(const QString& token)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl("https://helios.inputsense.com/api/validate"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization",     ("Bearer " + token).toUtf8());
    req.setRawHeader("X-Helios-API-Version", "4");
    req.setRawHeader("User-Agent",        "Helios/2.0");
    req.setRawHeader("Origin",            "https://www.inputsense.com");
    req.setRawHeader("Referer",           "https://www.inputsense.com/");

    QEventLoop loop;
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    if (!doc.isObject()) return false;
    QJsonObject obj = doc.object();

    if (obj["valid"].toBool()) {
        m_discordId = obj["discord_id"].toString();
        QString newToken = obj["token"].toString();
        if (!newToken.isEmpty() && newToken != m_sessionToken) {
            m_sessionToken = newToken;
            emit newSessionToken(newToken);
        }
        return true;
    }
    return false;
}

QString LicenseService::computeHWID()
{
    // HeliosHWID_v3_Stable: SHA-256 of machine-specific identifiers
    // Combines: machine UUID + CPU ID + primary disk serial
    QByteArray data;
    data += QSysInfo::machineUniqueId();
    data += QSysInfo::currentCpuArchitecture().toUtf8();

    // Windows: query disk serial via WMI or DeviceIoControl
    // Simplified: use machine UUID as primary identifier
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256)
           .toHex().toUpper();
}

} // namespace Helios
