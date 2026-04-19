using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Labs.RemotePlay.Theme;

public sealed class LabsTheme
{
    /// <summary>Preset identifier: "deepblue" / "midnight" / "black" / "paper" / "custom".
    /// Presets define Bg/BgSubtle/Surface/Text. Accent is always user-overridable on top.
    /// When "custom", all 5 fields come from this object.</summary>
    public string  PresetName { get; set; } = "deepblue";

    public string? Bg         { get; set; }
    public string? BgSubtle   { get; set; }
    public string? Surface    { get; set; }
    public string? Accent     { get; set; }
    public string? Text       { get; set; }
    public string? BackgroundImagePath { get; set; }
    public double  BackgroundImageOpacity { get; set; } = 1.0;
    public string  BackgroundImageStretch { get; set; } = "UniformToFill";
}

public static class Presets
{
    // Color presets (base palette). "image" is a virtual preset that reuses deepblue
    // colors but enables the user's background photo — lives in the same selector row.
    public static readonly Dictionary<string, (string Bg, string BgSubtle, string Surface, string Text, string Accent, string DisplayName)> All =
        new()
        {
            ["deepblue"] = ("#070A14", "#0B1020", "#11182C", "#F1F5F9", "#3B82F6", "deep blue"),
            ["midnight"] = ("#0E0E1A", "#15162B", "#1B1C36", "#F5F3FF", "#7C3AED", "midnight"),
            ["black"]    = ("#000000", "#0A0A0A", "#141414", "#FAFAFA", "#EAEAEA", "pure black"),
            ["paper"]    = ("#F5F3EE", "#ECE8DF", "#FFFFFF", "#1C1F24", "#1F3A5F", "paper"),
            ["image"]    = ("#070A14", "#0B1020", "#11182C", "#F1F5F9", "#3B82F6", "custom image"),
        };

    public static bool IsThemeActive(string presetName) => presetName != "custom";
}

public static class ThemeService
{
    public static readonly IReadOnlyList<string> EditableBrushKeys =
        new[] { "Bg", "BgSubtle", "Surface", "Accent", "Text" };

    // Ship defaults — matches the deep-blue palette in App.xaml.
    public static readonly IReadOnlyDictionary<string, string> Defaults =
        new Dictionary<string, string>
        {
            ["Bg"]       = "#070A14",
            ["BgSubtle"] = "#0B1020",
            ["Surface"]  = "#11182C",
            ["Accent"]   = "#3B82F6",
            ["Text"]     = "#F1F5F9",
        };

    public static LabsTheme Current { get; private set; } = new();

    /// <summary>Fired after a theme is loaded/reset so windows can refresh background images.</summary>
    public static event Action? ThemeApplied;

    private static string ThemePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "LabsEngine", "theme.json");

    public static void Load()
    {
        try
        {
            if (File.Exists(ThemePath))
            {
                var json = File.ReadAllText(ThemePath);
                Current = JsonSerializer.Deserialize<LabsTheme>(json) ?? new();
            }
        }
        catch { Current = new(); }
        Apply();
    }

    public static void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ThemePath)!);
            var json = JsonSerializer.Serialize(Current, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(ThemePath, json);
        }
        catch { /* best-effort persistence */ }
    }

    public static void Apply()
    {
        // Resolve base palette — if a named preset is active, its colors win over Current's
        // Bg/BgSubtle/Surface/Text. Accent is always a user override on top of the preset.
        string bg, bgSubtle, surface, text, accent;
        if (Current.PresetName != "custom"
            && Presets.All.TryGetValue(Current.PresetName, out var p))
        {
            bg = p.Bg; bgSubtle = p.BgSubtle; surface = p.Surface; text = p.Text;
            accent = Current.Accent ?? p.Accent;

            // In image mode: thin out the side-rail and panel backgrounds so the user's
            // image bleeds through everywhere (including the scripts and titan rails),
            // not just the empty middle stage.
            if (Current.PresetName == "image")
            {
                bgSubtle = WithAlpha(bgSubtle, 0xC8);  // ~78% — rails (readable but themed)
                surface  = WithAlpha(surface,  0xDC);  // ~86% — panels / cards
            }
        }
        else
        {
            bg       = Current.Bg       ?? Defaults["Bg"];
            bgSubtle = Current.BgSubtle ?? Defaults["BgSubtle"];
            surface  = Current.Surface  ?? Defaults["Surface"];
            text     = Current.Text     ?? Defaults["Text"];
            accent   = Current.Accent   ?? Defaults["Accent"];
        }
        ApplyBrush("Bg",       bg);
        ApplyBrush("BgSubtle", bgSubtle);
        ApplyBrush("Surface",  surface);
        ApplyBrush("Accent",   accent);
        ApplyBrush("Text",     text);
        ThemeApplied?.Invoke();
    }

    // Returns the hex with the alpha byte prepended: "#RRGGBB" + aa → "#AARRGGBB".
    private static string WithAlpha(string hex, byte alpha)
    {
        if (string.IsNullOrEmpty(hex) || hex[0] != '#') return hex;
        var core = hex.Length == 7 ? hex[1..] : hex[3..];
        return $"#{alpha:X2}{core}";
    }

    public static void ResetToDefaults()
    {
        Current = new LabsTheme();
        Apply();
        Save();
    }

    public static bool TryParseColor(string? hex, out Color color)
    {
        color = default;
        if (string.IsNullOrWhiteSpace(hex)) return false;
        try { color = (Color)ColorConverter.ConvertFromString(hex); return true; }
        catch { return false; }
    }

    public static string CurrentHex(string key)
    {
        var brush = Application.Current?.Resources[key] as SolidColorBrush;
        if (brush is null) return Defaults.GetValueOrDefault(key, "#000000");
        var c = brush.Color;
        return $"#{c.R:X2}{c.G:X2}{c.B:X2}";
    }

    /// <summary>Builds an ImageBrush from the saved path, or null if no image is set / path missing.</summary>
    public static ImageBrush? BuildBackgroundImageBrush()
    {
        var path = Current.BackgroundImagePath;
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return null;
        try
        {
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.CacheOption = BitmapCacheOption.OnLoad;
            bmp.UriSource = new Uri(path, UriKind.Absolute);
            bmp.EndInit();
            bmp.Freeze();

            return new ImageBrush(bmp)
            {
                Stretch = Enum.TryParse<Stretch>(Current.BackgroundImageStretch, out var s) ? s : Stretch.UniformToFill,
                Opacity = Math.Clamp(Current.BackgroundImageOpacity, 0.05, 1.0),
            };
        }
        catch { return null; }
    }

    private static void ApplyBrush(string key, string? hex)
    {
        var target = hex ?? (Defaults.TryGetValue(key, out var d) ? d : null);
        if (!TryParseColor(target, out var c)) return;
        if (Application.Current?.Resources[key] is not SolidColorBrush existing) return;

        if (existing.IsFrozen)
        {
            // StaticResource references already hold the frozen brush — to update everywhere
            // in one shot we replace the resource AND any controls using DynamicResource will
            // refresh. Controls with StaticResource bindings keep the old brush until restart.
            Application.Current.Resources[key] = new SolidColorBrush(c);
        }
        else
        {
            existing.Color = c;
        }
    }
}
