using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using Labs.RemotePlay.Theme;
using Microsoft.Win32;

namespace Labs.RemotePlay;

public partial class ThemeWindow : Window
{
    private readonly Dictionary<string, Border> _swatches = new();

    // Current popup edit state
    private string? _editingKey;
    private double _hue;      // 0..360
    private double _sat;      // 0..1
    private double _val;      // 0..1
    private bool _suppressHexEcho;
    private const double SvWidth = 220;
    private const double SvHeight = 160;
    private const double HueHeight = 160;

    public ThemeWindow()
    {
        InitializeComponent();
        DarkTitleBar.Apply(this);
        BuildPresetsRow();
        BuildColorRows();
        HydrateFromCurrent();
        UpdateImageTileThumbnail();
        RefreshModeUi();
    }

    private readonly Dictionary<string, Button> _presetButtons = new();

    private void BuildPresetsRow()
    {
        foreach (var kv in Presets.All)
        {
            var id = kv.Key;
            var (bg, _, _, _, accent, display) = kv.Value;

            var tile = new Button
            {
                Tag = id,
                Cursor = Cursors.Hand,
                BorderThickness = new Thickness(1),
                BorderBrush = (Brush)FindResource("BorderSoft"),
                Padding = new Thickness(0),
                Margin = new Thickness(0, 0, 10, 0),
                Height = 88,
                Template = BuildPresetTileTemplate(),
            };

            tile.Content = BuildPresetTileContent(id, display, bg, accent);
            tile.Click += (_, _) => SelectPreset(id);

            PresetsRow.Children.Add(tile);
            _presetButtons[id] = tile;
        }
    }

    private static ControlTemplate BuildPresetTileTemplate()
    {
        var xaml = @"
            <ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                             xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'
                             TargetType='Button'>
                <Border x:Name='Bd' CornerRadius='3'
                        Background='{DynamicResource Surface}'
                        BorderBrush='{TemplateBinding BorderBrush}'
                        BorderThickness='{TemplateBinding BorderThickness}'>
                    <ContentPresenter/>
                </Border>
                <ControlTemplate.Triggers>
                    <Trigger Property='IsMouseOver' Value='True'>
                        <Setter TargetName='Bd' Property='BorderBrush' Value='{DynamicResource Accent}'/>
                    </Trigger>
                </ControlTemplate.Triggers>
            </ControlTemplate>";
        using var reader = new System.IO.StringReader(xaml);
        using var xmlr  = System.Xml.XmlReader.Create(reader);
        return (ControlTemplate)System.Windows.Markup.XamlReader.Load(xmlr);
    }

    private FrameworkElement BuildPresetTileContent(string id, string display, string bgHex, string accentHex)
    {
        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        // Preview area (top ~56px)
        var preview = new Border
        {
            CornerRadius = new CornerRadius(2, 2, 0, 0),
            Height = 56,
        };
        Grid.SetRow(preview, 0);

        if (id == "image")
        {
            // Image preview — updated later when user picks one
            var img = new System.Windows.Controls.Image
            {
                Stretch = Stretch.UniformToFill,
                Name = "ImgThumb",
            };
            preview.Child = img;
            preview.Background = (Brush)FindResource("BgSubtle");
            preview.Tag = "ImagePreview";
        }
        else
        {
            ThemeService.TryParseColor(bgHex, out var bgC);
            ThemeService.TryParseColor(accentHex, out var accC);
            preview.Background = new SolidColorBrush(bgC);

            // Small accent dot so user can see the accent at a glance
            var dot = new Ellipse
            {
                Width = 12, Height = 12,
                Fill = new SolidColorBrush(accC),
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(0, 0, 10, 10),
            };
            var inner = new Grid();
            inner.Children.Add(dot);
            preview.Child = inner;
        }
        grid.Children.Add(preview);

        // Label
        var label = new TextBlock
        {
            Text = display,
            FontFamily = (FontFamily)FindResource("FontBody"),
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            Foreground = (Brush)FindResource("Text"),
            HorizontalAlignment = HorizontalAlignment.Center,
            Margin = new Thickness(0, 8, 0, 8),
        };
        Grid.SetRow(label, 1);
        grid.Children.Add(label);

        return grid;
    }

    private void UpdateImageTileThumbnail()
    {
        if (!_presetButtons.TryGetValue("image", out var tile)) return;
        if (tile.Content is not Grid grid) return;
        if (grid.Children[0] is not Border preview) return;
        if (preview.Tag as string != "ImagePreview") return;

        var path = ThemeService.Current.BackgroundImagePath;
        if (preview.Child is System.Windows.Controls.Image img)
        {
            if (!string.IsNullOrWhiteSpace(path) && System.IO.File.Exists(path))
            {
                try
                {
                    var bmp = new System.Windows.Media.Imaging.BitmapImage();
                    bmp.BeginInit();
                    bmp.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad;
                    bmp.DecodePixelWidth = 200;
                    bmp.UriSource = new Uri(path);
                    bmp.EndInit();
                    bmp.Freeze();
                    img.Source = bmp;
                }
                catch { img.Source = null; }
            }
            else
            {
                img.Source = null;
            }
        }
    }

    private void RemoveTheme_Click(object sender, RoutedEventArgs e) => SelectPreset("custom");

    private void SelectPreset(string id)
    {
        ThemeService.Current.PresetName = id;
        if (id != "custom")
        {
            // Clear individual non-accent overrides so the preset wins; leave Accent alone.
            ThemeService.Current.Bg = null;
            ThemeService.Current.BgSubtle = null;
            ThemeService.Current.Surface = null;
            ThemeService.Current.Text = null;
        }

        // "image" preset requires a picked image — prompt if none set.
        if (id == "image" && string.IsNullOrWhiteSpace(ThemeService.Current.BackgroundImagePath))
        {
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Title = "choose background image",
                Filter = "images|*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.gif|all|*.*",
            };
            if (dlg.ShowDialog(this) == true)
            {
                ThemeService.Current.BackgroundImagePath = dlg.FileName;
                ImagePathBox.Text = dlg.FileName;
            }
            else
            {
                // User cancelled — fall back to deep blue instead of staying on empty image preset.
                ThemeService.Current.PresetName = "deepblue";
            }
        }
        // Non-image presets should not carry a background image.
        if (ThemeService.Current.PresetName != "image")
            ThemeService.Current.BackgroundImagePath = null;

        ThemeService.Apply();
        RefreshSwatchesFromResources();
        RefreshModeUi();
        UpdateImageTileThumbnail();
    }

    private void RefreshModeUi()
    {
        bool custom = ThemeService.Current.PresetName == "custom";
        bool isImage = ThemeService.Current.PresetName == "image";

        ColorsHint.Text = custom
            ? "no theme · edit any color"
            : "accent only · remove theme to unlock all colors";
        ThemeHint.Text = custom
            ? "no theme — all colors editable below"
            : "pick a theme · accent stays editable";
        RemoveThemeBtn.Visibility = custom ? Visibility.Collapsed : Visibility.Visible;
        ImageControls.Visibility  = isImage ? Visibility.Visible : Visibility.Collapsed;

        // Highlight active preset tile
        foreach (var kv in _presetButtons)
        {
            var active = kv.Key == ThemeService.Current.PresetName;
            kv.Value.BorderBrush = active
                ? (Brush)FindResource("Accent")
                : (Brush)FindResource("BorderSoft");
            kv.Value.BorderThickness = new Thickness(active ? 2 : 1);
        }

        // Color rows: Accent always editable; others editable only in "custom" mode
        foreach (Grid row in ColorRows.Children)
        {
            foreach (var child in row.Children)
            {
                if (child is Button btn && btn.Tag is string key)
                {
                    bool enabled = custom || key == "Accent";
                    btn.IsEnabled = enabled;
                    btn.Opacity = enabled ? 1.0 : 0.45;
                }
            }
        }
    }

    private void RefreshSwatchesFromResources()
    {
        foreach (var key in ThemeService.EditableBrushKeys)
        {
            var hex = ThemeService.CurrentHex(key);
            if (_swatches.TryGetValue(key, out var sw)
                && ThemeService.TryParseColor(hex, out var c))
                sw.Background = new SolidColorBrush(c);
        }
        RefreshSwatchHexCaptions();
    }

    private void BuildColorRows()
    {
        foreach (var key in ThemeService.EditableBrushKeys)
        {
            var row = new Grid { Margin = new Thickness(0, 0, 0, 10) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var label = new TextBlock
            {
                Text = key,
                VerticalAlignment = VerticalAlignment.Center,
                FontFamily = (FontFamily)FindResource("FontMono"),
                FontSize = 12,
                Foreground = (Brush)FindResource("TextMuted"),
            };
            Grid.SetColumn(label, 0);
            row.Children.Add(label);

            // Full-width clickable swatch showing the current color + hex caption
            var btn = new Button
            {
                Cursor = Cursors.Hand,
                Tag = key,
                BorderThickness = new Thickness(1),
                BorderBrush = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#2E3F6A")),
                Padding = new Thickness(12, 10, 12, 10),
                HorizontalContentAlignment = HorizontalAlignment.Left,
            };
            btn.Template = BuildSwatchTemplate();
            btn.Click += Swatch_Click;

            var swatchFill = new Border
            {
                Background = (Brush)(Application.Current.Resources[key] ?? Brushes.Black),
                Width = 28,
                Height = 28,
                CornerRadius = new CornerRadius(2),
            };
            var hexText = new TextBlock
            {
                Text = ThemeService.CurrentHex(key).ToUpperInvariant(),
                FontFamily = (FontFamily)FindResource("FontMono"),
                FontSize = 13,
                Foreground = (Brush)FindResource("Text"),
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(12, 0, 0, 0),
            };

            var stack = new StackPanel { Orientation = Orientation.Horizontal };
            stack.Children.Add(swatchFill);
            stack.Children.Add(hexText);

            btn.Content = stack;
            // Store the swatchFill + hexText so we can update them later
            _swatches[key] = swatchFill;
            btn.Resources["HexText"] = hexText;

            Grid.SetColumn(btn, 1);
            row.Children.Add(btn);

            ColorRows.Children.Add(row);
        }
    }

    private static ControlTemplate BuildSwatchTemplate()
    {
        // A simple control template so we get our own padding + hover behavior
        var xaml = @"
            <ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                             xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'
                             TargetType='Button'>
                <Border x:Name='Bd' CornerRadius='3'
                        Background='{DynamicResource BgSubtle}'
                        BorderBrush='{TemplateBinding BorderBrush}'
                        BorderThickness='{TemplateBinding BorderThickness}'
                        Padding='{TemplateBinding Padding}'>
                    <ContentPresenter HorizontalAlignment='{TemplateBinding HorizontalContentAlignment}'
                                      VerticalAlignment='Center'/>
                </Border>
                <ControlTemplate.Triggers>
                    <Trigger Property='IsMouseOver' Value='True'>
                        <Setter TargetName='Bd' Property='BorderBrush' Value='#3B82F6'/>
                    </Trigger>
                </ControlTemplate.Triggers>
            </ControlTemplate>";
        using var reader = new System.IO.StringReader(xaml);
        using var xmlr  = System.Xml.XmlReader.Create(reader);
        return (ControlTemplate)System.Windows.Markup.XamlReader.Load(xmlr);
    }

    private void HydrateFromCurrent()
    {
        ImagePathBox.Text = ThemeService.Current.BackgroundImagePath ?? "";
        OpacitySlider.Value = ThemeService.Current.BackgroundImageOpacity;
        foreach (ComboBoxItem item in StretchCombo.Items)
        {
            if ((string)item.Content == ThemeService.Current.BackgroundImageStretch)
            {
                StretchCombo.SelectedItem = item;
                break;
            }
        }
        if (StretchCombo.SelectedItem is null) StretchCombo.SelectedIndex = 0;
    }

    // ============= Swatch click opens popup ======================================
    private void Swatch_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button b || b.Tag is not string key) return;
        _editingKey = key;

        var hex = ThemeService.CurrentHex(key);
        if (ThemeService.TryParseColor(hex, out var c))
        {
            var hsv = RgbToHsv(c.R, c.G, c.B);
            _hue = hsv.H; _sat = hsv.S; _val = hsv.V;
        }

        RefreshPopupFromHsv(writeHex: true);
        ColorPop.PlacementTarget = b;
        ColorPop.IsOpen = true;
    }

    // ============= SV region mouse =====================================
    private void Sv_Down(object sender, MouseButtonEventArgs e)
    {
        if (sender is UIElement el) { el.CaptureMouse(); Sv_UpdateFrom(e.GetPosition(el)); }
    }
    private void Sv_Move(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed) return;
        if (sender is UIElement el) Sv_UpdateFrom(e.GetPosition(el));
    }
    private void Sv_Up(object sender, MouseButtonEventArgs e)
    {
        if (sender is UIElement el) el.ReleaseMouseCapture();
    }
    private void Sv_UpdateFrom(Point p)
    {
        _sat = Math.Clamp(p.X / SvWidth, 0, 1);
        _val = Math.Clamp(1 - (p.Y / SvHeight), 0, 1);
        RefreshPopupFromHsv(writeHex: true);
    }

    // ============= Hue slider mouse =====================================
    private void Hue_Down(object sender, MouseButtonEventArgs e)
    {
        if (sender is UIElement el) { el.CaptureMouse(); Hue_UpdateFrom(e.GetPosition(el)); }
    }
    private void Hue_Move(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed) return;
        if (sender is UIElement el) Hue_UpdateFrom(e.GetPosition(el));
    }
    private void Hue_Up(object sender, MouseButtonEventArgs e)
    {
        if (sender is UIElement el) el.ReleaseMouseCapture();
    }
    private void Hue_UpdateFrom(Point p)
    {
        _hue = Math.Clamp((p.Y / HueHeight) * 360, 0, 360);
        RefreshPopupFromHsv(writeHex: true);
    }

    // ============= Hex text input =====================================
    private void HexField_Changed(object sender, TextChangedEventArgs e)
    {
        if (_suppressHexEcho) return;
        if (!ThemeService.TryParseColor(HexField.Text, out var c)) return;
        var hsv = RgbToHsv(c.R, c.G, c.B);
        _hue = hsv.H; _sat = hsv.S; _val = hsv.V;
        RefreshPopupFromHsv(writeHex: false);
    }

    private void Quick_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button b || b.Tag is not string hex) return;
        if (!ThemeService.TryParseColor(hex, out var c)) return;
        var hsv = RgbToHsv(c.R, c.G, c.B);
        _hue = hsv.H; _sat = hsv.S; _val = hsv.V;
        RefreshPopupFromHsv(writeHex: true);
    }

    // ============= Shared: recompute color, update popup UI + theme ====
    private void RefreshPopupFromHsv(bool writeHex)
    {
        var color = HsvToRgb(_hue, _sat, _val);
        var hex   = $"#{color.R:X2}{color.G:X2}{color.B:X2}";

        // Popup previews
        PreviewBrush.Color = color;
        var pureHue = HsvToRgb(_hue, 1, 1);
        HueStop.Color = pureHue;

        Canvas.SetLeft(SvThumb, _sat * SvWidth - 7);
        Canvas.SetTop(SvThumb,  (1 - _val) * SvHeight - 7);
        Canvas.SetTop(HueThumb, (_hue / 360) * HueHeight - 1);

        if (writeHex)
        {
            _suppressHexEcho = true;
            HexField.Text = hex;
            _suppressHexEcho = false;
        }

        // Apply to theme live
        if (_editingKey is not null)
        {
            // Editing a non-Accent color while a preset is active implicitly switches to custom.
            if (_editingKey != "Accent" && ThemeService.Current.PresetName != "custom")
            {
                // Seed the custom theme with the currently-rendered preset colors so changing
                // one field doesn't wipe the others.
                foreach (var k in ThemeService.EditableBrushKeys)
                    SetThemeField(k, ThemeService.CurrentHex(k));
                ThemeService.Current.PresetName = "custom";
                RefreshModeUi();
            }

            SetThemeField(_editingKey, hex);
            ThemeService.Apply();
            if (_swatches.TryGetValue(_editingKey, out var sw)) sw.Background = new SolidColorBrush(color);
            foreach (Grid row in ColorRows.Children)
            foreach (var child in row.Children)
                if (child is Button btn && btn.Tag is string k && k == _editingKey
                    && btn.Resources["HexText"] is TextBlock tb)
                    tb.Text = hex.ToUpperInvariant();
        }
    }

    private static void SetThemeField(string key, string hex)
    {
        switch (key)
        {
            case "Bg":       ThemeService.Current.Bg       = hex; break;
            case "BgSubtle": ThemeService.Current.BgSubtle = hex; break;
            case "Surface":  ThemeService.Current.Surface  = hex; break;
            case "Accent":   ThemeService.Current.Accent   = hex; break;
            case "Text":     ThemeService.Current.Text     = hex; break;
        }
    }

    // ============= HSV/RGB conversions ===================================
    private static (double H, double S, double V) RgbToHsv(byte r, byte g, byte b)
    {
        double rd = r / 255.0, gd = g / 255.0, bd = b / 255.0;
        double max = Math.Max(rd, Math.Max(gd, bd));
        double min = Math.Min(rd, Math.Min(gd, bd));
        double v = max;
        double d = max - min;
        double s = max == 0 ? 0 : d / max;
        double h;
        if (d == 0) h = 0;
        else if (max == rd) h = ((gd - bd) / d + (gd < bd ? 6 : 0)) * 60;
        else if (max == gd) h = ((bd - rd) / d + 2) * 60;
        else               h = ((rd - gd) / d + 4) * 60;
        return (h, s, v);
    }

    private static Color HsvToRgb(double h, double s, double v)
    {
        h = ((h % 360) + 360) % 360;
        double c = v * s;
        double x = c * (1 - Math.Abs((h / 60) % 2 - 1));
        double m = v - c;
        double r, g, b;
        if (h < 60)       { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else              { r = c; g = 0; b = x; }
        return Color.FromRgb((byte)Math.Round((r + m) * 255),
                              (byte)Math.Round((g + m) * 255),
                              (byte)Math.Round((b + m) * 255));
    }

    // ============= Presets ===================================
    private void ApplyPreset(Dictionary<string, string> preset)
    {
        foreach (var kv in preset)
        {
            SetThemeField(kv.Key, kv.Value);
            if (_swatches.TryGetValue(kv.Key, out var sw)
                && ThemeService.TryParseColor(kv.Value, out var c))
                sw.Background = new SolidColorBrush(c);
        }
        ThemeService.Apply();
        RefreshSwatchHexCaptions();
    }

    private void RefreshSwatchHexCaptions()
    {
        foreach (var key in ThemeService.EditableBrushKeys)
        {
            foreach (Grid row in ColorRows.Children)
            foreach (var child in row.Children)
                if (child is Button btn && btn.Tag is string k && k == key
                    && btn.Resources["HexText"] is TextBlock tb)
                    tb.Text = ThemeService.CurrentHex(k).ToUpperInvariant();
        }
    }


    // ============= Background image ===================================
    private void ChooseImage_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "choose background image",
            Filter = "images|*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.gif|all|*.*",
        };
        if (dlg.ShowDialog(this) != true) return;
        ThemeService.Current.BackgroundImagePath = dlg.FileName;
        ImagePathBox.Text = dlg.FileName;
        ThemeService.Current.PresetName = "image";
        ThemeService.Apply();
        UpdateImageTileThumbnail();
        RefreshModeUi();
    }

    private void Stretch_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (StretchCombo.SelectedItem is ComboBoxItem item)
        {
            ThemeService.Current.BackgroundImageStretch = (string)item.Content;
            ThemeService.Apply();
        }
    }

    private void Opacity_Changed(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        ThemeService.Current.BackgroundImageOpacity = e.NewValue;
        // Mutate the existing ImageBrush opacity directly — avoids the full disk reload
        // round-trip that BuildBackgroundImageBrush() does, giving smooth live preview.
        if (Application.Current?.MainWindow?.Background is System.Windows.Media.ImageBrush ib && !ib.IsFrozen)
            ib.Opacity = e.NewValue;
        else
            ThemeService.Apply();
    }

    private void Reset_Click(object sender, RoutedEventArgs e)
    {
        ThemeService.ResetToDefaults();
        RefreshSwatchesFromResources();
        RefreshModeUi();
        ImagePathBox.Text = "";
        OpacitySlider.Value = 1;
        StretchCombo.SelectedIndex = 0;
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        ThemeService.Save();
        Close();
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}

/// <summary>Converts a "#RRGGBB" string Tag to a Color for the quick-swatch template binding.</summary>
public sealed class HexToColorConverter : IValueConverter
{
    public static readonly HexToColorConverter Instance = new();
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is string hex && ThemeService.TryParseColor(hex, out var c) ? c : Colors.Black;
    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) => throw new NotSupportedException();
}
