#include "LabsThemeDialog.h"
#include "SettingsManager.h"

#include <QApplication>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace Labs {

static QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

static void paintSwatch(QPushButton* b, const QColor& c)
{
    b->setFixedHeight(32);
    b->setStyleSheet(QString(
        "QPushButton { background:%1; border:1px solid rgba(255,255,255,0.08);"
        " border-radius:4px; color:#F1F5F9; font-family:'Cascadia Mono';"
        " font-size:11px; text-align:left; padding:6px 10px; }"
        "QPushButton:hover { border-color:rgba(255,255,255,0.3); }"
    ).arg(hex(c)));
    b->setText(hex(c).toUpper());
}

static QPushButton* makePresetButton(QWidget* parent, const LabsPreset& p)
{
    auto* b = new QPushButton(parent);
    b->setCheckable(true);
    b->setFixedHeight(60);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("presetKey", QString::fromUtf8(p.key));
    b->setStyleSheet(QString(
        "QPushButton { background:%1; color:%3; border:1px solid %4;"
        " border-radius:6px; font-family:'Segoe UI Variable Display';"
        " font-size:11px; font-weight:600; padding:4px; }"
        "QPushButton:hover { border-color:%2; }"
        "QPushButton:checked { border-color:%2; border-width:2px; }"
    ).arg(p.bg).arg(p.accent).arg(p.text).arg(p.bgSubtle));
    b->setText(QString::fromUtf8(p.display));
    return b;
}

// ── ctor/dtor ──────────────────────────────────────────────────────────────

LabsThemeDialog::LabsThemeDialog(SettingsManager* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(QStringLiteral("theme"));
    setModal(true);
    setMinimumWidth(460);

    m_theme      = labsThemeLoad(settings);
    m_entryTheme = m_theme;

    auto* title = new QLabel(QStringLiteral("THEME"), this);
    title->setStyleSheet(QStringLiteral(
        "color:#64748B; font-family:'Cascadia Mono'; font-size:10px; letter-spacing:1.5px;"));

    auto* hint = new QLabel(QStringLiteral("pick a preset · edit any slot"), this);
    hint->setStyleSheet(QStringLiteral("color:#94A3B8; font-size:11px;"));

    // Preset row.
    auto* presetRow = new QHBoxLayout();
    presetRow->setSpacing(8);
    const auto& presets = labsPresets();
    for (int i = 0; i < int(presets.size()); ++i) {
        m_presetBtns[i] = makePresetButton(this, presets[i]);
        connect(m_presetBtns[i], &QPushButton::clicked, this, &LabsThemeDialog::onPresetClicked);
        presetRow->addWidget(m_presetBtns[i]);
    }

    // Individual swatches.
    m_swBg       = new QPushButton(this);
    m_swBgSubtle = new QPushButton(this);
    m_swSurface  = new QPushButton(this);
    m_swAccent   = new QPushButton(this);
    m_swText     = new QPushButton(this);
    m_swBg      ->setProperty("slot", QStringLiteral("bg"));
    m_swBgSubtle->setProperty("slot", QStringLiteral("bgSubtle"));
    m_swSurface ->setProperty("slot", QStringLiteral("surface"));
    m_swAccent  ->setProperty("slot", QStringLiteral("accent"));
    m_swText    ->setProperty("slot", QStringLiteral("text"));
    for (auto* b : { m_swBg, m_swBgSubtle, m_swSurface, m_swAccent, m_swText })
        connect(b, &QPushButton::clicked, this, &LabsThemeDialog::onSwatchClicked);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(8);
    form->addRow(QStringLiteral("Background"),     m_swBg);
    form->addRow(QStringLiteral("Subtle / panels"), m_swBgSubtle);
    form->addRow(QStringLiteral("Surface / cards"), m_swSurface);
    form->addRow(QStringLiteral("Accent"),         m_swAccent);
    form->addRow(QStringLiteral("Text"),           m_swText);

    // Background image section.
    auto* imgSection = new QLabel(QStringLiteral("BACKGROUND IMAGE"), this);
    imgSection->setObjectName(QStringLiteral("eyebrow"));

    m_imagePathLabel = new QLabel(this);
    m_imagePathLabel->setWordWrap(true);
    m_imagePathLabel->setObjectName(QStringLiteral("pathBlue"));

    m_btnPickImage  = new QPushButton(QStringLiteral("choose image…"), this);
    m_btnClearImage = new QPushButton(QStringLiteral("clear"),          this);
    connect(m_btnPickImage,  &QPushButton::clicked, this, &LabsThemeDialog::onPickImage);
    connect(m_btnClearImage, &QPushButton::clicked, this, &LabsThemeDialog::onClearImage);

    auto* imgBtnRow = new QHBoxLayout();
    imgBtnRow->addWidget(m_btnPickImage);
    imgBtnRow->addWidget(m_btnClearImage);
    imgBtnRow->addStretch();

    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(20, 100);
    m_opacitySlider->setValue(int(m_theme.imageOpacity * 100));
    connect(m_opacitySlider, &QSlider::valueChanged, this, &LabsThemeDialog::onOpacityChanged);

    auto* opacityLabel = new QLabel(QStringLiteral("opacity"), this);

    // Buttons.
    auto* reset = new QPushButton(QStringLiteral("reset"), this);
    auto* cancel = new QPushButton(QStringLiteral("cancel"), this);
    auto* save   = new QPushButton(QStringLiteral("save"), this);
    save->setProperty("accent", true);
    connect(reset,  &QPushButton::clicked, this, &LabsThemeDialog::onResetClicked);
    connect(cancel, &QPushButton::clicked, this, &LabsThemeDialog::onReject);
    connect(save,   &QPushButton::clicked, this, &LabsThemeDialog::onAccept);

    auto* btnRow = new QHBoxLayout();
    btnRow->addWidget(reset);
    btnRow->addStretch();
    btnRow->addWidget(cancel);
    btnRow->addWidget(save);

    auto* opacityRow = new QHBoxLayout();
    opacityRow->addWidget(opacityLabel);
    opacityRow->addWidget(m_opacitySlider, 1);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 20);
    root->setSpacing(14);
    root->addWidget(title);
    root->addWidget(hint);
    root->addSpacing(4);
    root->addLayout(presetRow);
    root->addSpacing(10);
    root->addLayout(form);
    root->addSpacing(8);
    root->addWidget(imgSection);
    root->addWidget(m_imagePathLabel);
    root->addLayout(imgBtnRow);
    root->addLayout(opacityRow);
    root->addStretch();
    root->addLayout(btnRow);

    refreshSwatches();
}

LabsThemeDialog::~LabsThemeDialog() = default;

// ── actions ─────────────────────────────────────────────────────────────────

void LabsThemeDialog::onPresetClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const QString key = btn->property("presetKey").toString();
    m_theme = labsThemeFromPreset(key);
    refreshSwatches();
    applyLive();
}

void LabsThemeDialog::onSwatchClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const QString slot = btn->property("slot").toString();

    QColor& target = (slot == QStringLiteral("bg"))       ? m_theme.bg
                   : (slot == QStringLiteral("bgSubtle")) ? m_theme.bgSubtle
                   : (slot == QStringLiteral("surface"))  ? m_theme.surface
                   : (slot == QStringLiteral("accent"))   ? m_theme.accent
                   : m_theme.text;

    const QColor picked = QColorDialog::getColor(target, this,
        QStringLiteral("Choose colour"),
        QColorDialog::DontUseNativeDialog);
    if (!picked.isValid()) return;

    target = picked;
    m_theme.preset = QStringLiteral("custom");
    refreshSwatches();
    applyLive();
}

void LabsThemeDialog::onResetClicked()
{
    m_theme = labsThemeFromPreset(QStringLiteral("deepblue"));
    refreshSwatches();
    applyLive();
}

void LabsThemeDialog::onPickImage()
{
    const QString start = m_theme.imagePath.isEmpty()
        ? QDir::homePath()
        : QFileInfo(m_theme.imagePath).absolutePath();
    const QString picked = QFileDialog::getOpenFileName(this,
        QStringLiteral("Choose background image"),
        start,
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*.*)"));
    if (picked.isEmpty()) return;
    m_theme.imagePath = picked;
    // Persist so the main window's applyThemeImage() reads the new path on next
    // themeChanged() emit.
    labsThemeSave(m_theme, m_settings);
    refreshSwatches();
    applyLive();
}

void LabsThemeDialog::onClearImage()
{
    m_theme.imagePath.clear();
    labsThemeSave(m_theme, m_settings);
    refreshSwatches();
    applyLive();
}

void LabsThemeDialog::onOpacityChanged(int v)
{
    m_theme.imageOpacity = qBound(0.2, v / 100.0, 1.0);
    labsThemeSave(m_theme, m_settings);
    applyLive();
}

void LabsThemeDialog::onAccept()
{
    labsThemeSave(m_theme, m_settings);
    accept();
}

void LabsThemeDialog::onReject()
{
    // Revert to entry state + reapply.
    m_theme = m_entryTheme;
    applyLive();
    reject();
}

// ── helpers ─────────────────────────────────────────────────────────────────

void LabsThemeDialog::applyLive()
{
    qApp->setStyleSheet(labsStylesheet(m_theme));
    m_applied = true;
    emit themeChanged();
}

void LabsThemeDialog::refreshSwatches()
{
    paintSwatch(m_swBg,       m_theme.bg);
    paintSwatch(m_swBgSubtle, m_theme.bgSubtle);
    paintSwatch(m_swSurface,  m_theme.surface);
    paintSwatch(m_swAccent,   m_theme.accent);
    paintSwatch(m_swText,     m_theme.text);

    if (m_imagePathLabel) {
        m_imagePathLabel->setText(m_theme.imagePath.isEmpty()
            ? QStringLiteral("(none)")
            : QFileInfo(m_theme.imagePath).fileName());
    }
    if (m_btnClearImage) m_btnClearImage->setEnabled(!m_theme.imagePath.isEmpty());
    if (m_opacitySlider) m_opacitySlider->setEnabled(!m_theme.imagePath.isEmpty());

    const auto& presets = labsPresets();
    for (int i = 0; i < int(presets.size()); ++i) {
        if (m_presetBtns[i])
            m_presetBtns[i]->setChecked(QString::fromUtf8(presets[i].key) == m_theme.preset);
    }
}

} // namespace Labs
