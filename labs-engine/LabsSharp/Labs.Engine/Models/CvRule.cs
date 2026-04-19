using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Labs.Engine.Models;

/// <summary>Type of CV detection for a rule.</summary>
public enum CvRuleType
{
    ColorRange,
    TemplateMatch,
    ContourDetect,
    PixelColor,
}

/// <summary>What happens when a rule triggers.</summary>
public enum CvActionType
{
    PressButton,
    HoldButton,
    ReleaseButton,
    MoveStick,
    Combo,
}

/// <summary>A single CV detection rule with normalized region, detection params, and gamepad output.</summary>
public class CvRule : INotifyPropertyChanged
{
    private string  _name          = string.Empty;
    private double  _regionX;
    private double  _regionY;
    private double  _regionW       = 1.0;
    private double  _regionH       = 1.0;
    private CvRuleType _type       = CvRuleType.ColorRange;
    private int     _lowerH;
    private int     _lowerS;
    private int     _lowerV;
    private int     _upperH        = 179;
    private int     _upperS        = 255;
    private int     _upperV        = 255;
    private int     _minPixels     = 100;
    private string  _templatePath  = string.Empty;
    private double  _threshold     = 0.8;
    private int     _buttonIndex;
    private int     _holdMs        = 100;
    private CvActionType _actionType = CvActionType.PressButton;

    // Contour detection
    private int     _minContourW   = 5;
    private int     _maxContourW   = 200;
    private int     _minContourH   = 5;
    private int     _maxContourH   = 200;
    private int     _minContourArea = 50;

    // Pixel color check
    private int     _pixelX;
    private int     _pixelY;
    private int     _colorTolerance = 30;

    // Stick output
    private int     _stickIndex;  // 0=LX, 1=LY, 2=RX, 3=RY
    private double  _stickValue;  // -1.0 to 1.0

    // Combo (semicolon-separated button indices, e.g. "0;100;2;50" = btn0 hold 100ms, btn2 hold 50ms)
    private string  _comboSequence = string.Empty;

    // Cooldown between triggers
    private int     _cooldownMs    = 200;

    /// <summary>Display name of the rule.</summary>
    public string Name
    {
        get => _name;
        set { _name = value; OnPropertyChanged(); }
    }

    /// <summary>Normalized X origin of the detection region (0.0–1.0).</summary>
    public double RegionX
    {
        get => _regionX;
        set { _regionX = value; OnPropertyChanged(); }
    }

    /// <summary>Normalized Y origin of the detection region (0.0–1.0).</summary>
    public double RegionY
    {
        get => _regionY;
        set { _regionY = value; OnPropertyChanged(); }
    }

    /// <summary>Normalized width of the detection region (0.0–1.0).</summary>
    public double RegionW
    {
        get => _regionW;
        set { _regionW = value; OnPropertyChanged(); }
    }

    /// <summary>Normalized height of the detection region (0.0–1.0).</summary>
    public double RegionH
    {
        get => _regionH;
        set { _regionH = value; OnPropertyChanged(); }
    }

    /// <summary>Detection type — color range or template match.</summary>
    public CvRuleType Type
    {
        get => _type;
        set { _type = value; OnPropertyChanged(); }
    }

    /// <summary>Lower hue bound for HSV color range (0–179).</summary>
    public int LowerH
    {
        get => _lowerH;
        set { _lowerH = value; OnPropertyChanged(); }
    }

    /// <summary>Lower saturation bound for HSV color range (0–255).</summary>
    public int LowerS
    {
        get => _lowerS;
        set { _lowerS = value; OnPropertyChanged(); }
    }

    /// <summary>Lower value bound for HSV color range (0–255).</summary>
    public int LowerV
    {
        get => _lowerV;
        set { _lowerV = value; OnPropertyChanged(); }
    }

    /// <summary>Upper hue bound for HSV color range (0–179).</summary>
    public int UpperH
    {
        get => _upperH;
        set { _upperH = value; OnPropertyChanged(); }
    }

    /// <summary>Upper saturation bound for HSV color range (0–255).</summary>
    public int UpperS
    {
        get => _upperS;
        set { _upperS = value; OnPropertyChanged(); }
    }

    /// <summary>Upper value bound for HSV color range (0–255).</summary>
    public int UpperV
    {
        get => _upperV;
        set { _upperV = value; OnPropertyChanged(); }
    }

    /// <summary>Minimum matching pixels to trigger the rule (color range mode).</summary>
    public int MinPixels
    {
        get => _minPixels;
        set { _minPixels = value; OnPropertyChanged(); }
    }

    /// <summary>Path to the template image file (template match mode).</summary>
    public string TemplatePath
    {
        get => _templatePath;
        set { _templatePath = value; OnPropertyChanged(); }
    }

    /// <summary>Match confidence threshold 0.0–1.0 (template match mode).</summary>
    public double Threshold
    {
        get => _threshold;
        set { _threshold = value; OnPropertyChanged(); }
    }

    /// <summary>Web Gamepad API button index to press when the rule triggers (0–16).</summary>
    public int ButtonIndex
    {
        get => _buttonIndex;
        set { _buttonIndex = value; OnPropertyChanged(); }
    }

    /// <summary>Duration in milliseconds to hold the button when triggered.</summary>
    public int HoldMs
    {
        get => _holdMs;
        set { _holdMs = value; OnPropertyChanged(); }
    }

    /// <summary>What action to perform when the rule triggers.</summary>
    public CvActionType ActionType
    {
        get => _actionType;
        set { _actionType = value; OnPropertyChanged(); }
    }

    /// <summary>Minimum contour width (ContourDetect mode).</summary>
    public int MinContourW { get => _minContourW; set { _minContourW = value; OnPropertyChanged(); } }
    /// <summary>Maximum contour width (ContourDetect mode).</summary>
    public int MaxContourW { get => _maxContourW; set { _maxContourW = value; OnPropertyChanged(); } }
    /// <summary>Minimum contour height (ContourDetect mode).</summary>
    public int MinContourH { get => _minContourH; set { _minContourH = value; OnPropertyChanged(); } }
    /// <summary>Maximum contour height (ContourDetect mode).</summary>
    public int MaxContourH { get => _maxContourH; set { _maxContourH = value; OnPropertyChanged(); } }
    /// <summary>Minimum contour area in pixels (ContourDetect mode).</summary>
    public int MinContourArea { get => _minContourArea; set { _minContourArea = value; OnPropertyChanged(); } }

    /// <summary>Pixel X coordinate to check (PixelColor mode, absolute).</summary>
    public int PixelX { get => _pixelX; set { _pixelX = value; OnPropertyChanged(); } }
    /// <summary>Pixel Y coordinate to check (PixelColor mode, absolute).</summary>
    public int PixelY { get => _pixelY; set { _pixelY = value; OnPropertyChanged(); } }
    /// <summary>Color tolerance for pixel matching (PixelColor mode).</summary>
    public int ColorTolerance { get => _colorTolerance; set { _colorTolerance = value; OnPropertyChanged(); } }

    /// <summary>Stick axis index: 0=LX, 1=LY, 2=RX, 3=RY (MoveStick action).</summary>
    public int StickIndex { get => _stickIndex; set { _stickIndex = value; OnPropertyChanged(); } }
    /// <summary>Stick value -1.0 to 1.0 (MoveStick action).</summary>
    public double StickValue { get => _stickValue; set { _stickValue = value; OnPropertyChanged(); } }

    /// <summary>Combo sequence: "btn,holdMs;btn,holdMs;..." (Combo action).</summary>
    public string ComboSequence { get => _comboSequence; set { _comboSequence = value; OnPropertyChanged(); } }

    /// <summary>Cooldown between triggers in milliseconds.</summary>
    public int CooldownMs { get => _cooldownMs; set { _cooldownMs = value; OnPropertyChanged(); } }

    // ---------------------------------------------------------------------------
    // INotifyPropertyChanged
    // ---------------------------------------------------------------------------

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
