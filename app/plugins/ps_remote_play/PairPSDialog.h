#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <atomic>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace Labs {

class SettingsManager;

// Two-step dialog:
//   Step 1: "Sign in with PSN" opens an embedded WebView2 browser; the app
//           intercepts the OAuth redirect, exchanges the code, and saves the
//           PSN account ID automatically — no copy/paste needed.
//   Step 2: Enter console IP + 8-digit PIN, run labs_regist_start
class PairPSDialog : public QDialog {
    Q_OBJECT
public:
    explicit PairPSDialog(SettingsManager* settings, QWidget* parent = nullptr);
    ~PairPSDialog() override;

    // Called from chiaki's regist thread via free-function trampoline.
    void handleRegistEvent(int typeInt, const QByteArray& registKey,
                           const QByteArray& morning, bool isPs5);

    // Public so the C log callback can route raw messages back into the UI.
    void translateRegistMessage(const QString& raw);

private slots:
    void onSignInClicked();
    void onSignOutClicked();
    void onPairClicked();
    void onCancelClicked();
    void onFindConsoleClicked();

private:
    void setAccountLinked(const QString& accountIdBase64, const QString& username);
    void clearAccount();
    void updateLinkedLabel();
    void setBusy(bool busy);
    void reportStatus(const QString& msg, bool ok = false, bool err = false);
    SettingsManager* m_settings = nullptr;

    // Step 1 widgets
    QLabel*      m_linkedLabel = nullptr;
    QPushButton* m_signInBtn   = nullptr;
    QPushButton* m_signOutBtn  = nullptr;

    // Step 2 widgets
    QLineEdit*   m_hostEdit   = nullptr;
    QCheckBox*   m_ps5Check   = nullptr;
    QLineEdit*   m_pinEdit    = nullptr;
    QPushButton* m_findBtn    = nullptr;
    QPushButton* m_pairBtn    = nullptr;
    QPushButton* m_cancelBtn  = nullptr;
    QLabel*      m_status     = nullptr;
    QLabel*      m_progress   = nullptr;

    QString m_accountIdBase64;
    QString m_psnUsername;

    struct Impl;
    Impl* m_impl = nullptr;
    std::atomic<bool> m_registRunning { false };
};

} // namespace Labs
