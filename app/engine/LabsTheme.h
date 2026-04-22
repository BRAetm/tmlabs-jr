#pragma once

#include <QColor>
#include <QMap>
#include <QString>
#include <array>

namespace Labs {

class SettingsManager;

// Five user-editable color slots — same as LabsSharp's ThemeService, plus
// optional background image.
struct LabsThemeData {
    QString preset   { "deepblue" };
    QColor  bg       { "#070A14" };
    QColor  bgSubtle { "#0B1020" };
    QColor  surface  { "#11182C" };
    QColor  accent   { "#3B82F6" };
    QColor  text     { "#F1F5F9" };

    QString imagePath;          // empty → no background image
    qreal   imageOpacity = 1.0;

    bool imageActive() const { return !imagePath.isEmpty(); }
};

struct LabsPreset {
    const char* key;
    const char* display;
    const char* bg;
    const char* bgSubtle;
    const char* surface;
    const char* accent;
    const char* text;
};

inline const std::array<LabsPreset, 4>& labsPresets()
{
    static const std::array<LabsPreset, 4> P = {{
        { "deepblue", "deep blue", "#070A14", "#0B1020", "#11182C", "#3B82F6", "#F1F5F9" },
        { "midnight", "midnight",  "#0E0E1A", "#15162B", "#1B1C36", "#7C3AED", "#F5F3FF" },
        { "black",    "pure black","#000000", "#0A0A0A", "#141414", "#EAEAEA", "#FAFAFA" },
        { "paper",    "paper",     "#F5F3EE", "#ECE8DF", "#FFFFFF", "#1F3A5F", "#1C1F24" },
    }};
    return P;
}

inline LabsThemeData labsThemeFromPreset(const QString& key)
{
    LabsThemeData t;
    for (const auto& p : labsPresets()) {
        if (key == QString::fromUtf8(p.key)) {
            t.preset = key;
            t.bg = QColor(p.bg); t.bgSubtle = QColor(p.bgSubtle);
            t.surface = QColor(p.surface); t.accent = QColor(p.accent); t.text = QColor(p.text);
            return t;
        }
    }
    return t;
}

// Load/save to SettingsManager under [theme].
void labsThemeSave(const LabsThemeData& t, SettingsManager* settings);
LabsThemeData labsThemeLoad(SettingsManager* settings);

// Generate a full Qt stylesheet substituting the theme's 5 colors. Accent is
// also lightened/darkened algorithmically for hover + border states.
QString labsStylesheet(const LabsThemeData& t);

} // namespace Labs
