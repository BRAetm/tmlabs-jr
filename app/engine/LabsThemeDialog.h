#pragma once

#include "LabsTheme.h"

#include <QDialog>
#include <QString>
#include <array>

class QLabel;
class QPushButton;
class QSlider;

namespace Labs {

class SettingsManager;

// Live theme editor mirroring LabsSharp's ThemeWindow. Preset row across the
// top, 5 individual color swatches below. Picking a preset seeds the colors;
// each swatch opens a QColorDialog. Changes apply immediately to the whole
// QApplication via setStyleSheet(). On accept, persists to settings.
class LabsThemeDialog : public QDialog {
    Q_OBJECT
public:
    explicit LabsThemeDialog(SettingsManager* settings, QWidget* parent = nullptr);
    ~LabsThemeDialog() override;

    LabsThemeData result() const { return m_theme; }

signals:
    void themeChanged();  // emitted after every live preview, and on accept/reject final state

private slots:
    void onPresetClicked();
    void onSwatchClicked();
    void onResetClicked();
    void onPickImage();
    void onClearImage();
    void onOpacityChanged(int v);
    void onAccept();
    void onReject();

private:
    void applyLive();
    void refreshSwatches();

    SettingsManager* m_settings = nullptr;
    LabsThemeData    m_theme;
    LabsThemeData    m_entryTheme;  // snapshot on open, used on cancel
    bool             m_applied = false;

    std::array<QPushButton*, 4> m_presetBtns { nullptr, nullptr, nullptr, nullptr };
    QPushButton* m_swBg        = nullptr;
    QPushButton* m_swBgSubtle  = nullptr;
    QPushButton* m_swSurface   = nullptr;
    QPushButton* m_swAccent    = nullptr;
    QPushButton* m_swText      = nullptr;

    QPushButton* m_btnPickImage  = nullptr;
    QPushButton* m_btnClearImage = nullptr;
    QSlider*     m_opacitySlider = nullptr;
    QLabel*      m_imagePathLabel = nullptr;
};

} // namespace Labs
