using System;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Labs.Engine.Scripting;

namespace Labs.RemotePlay.Panels;

public partial class ScriptsBar : UserControl, IDisposable
{
    private ScriptDriver? _driver;

    public ScriptsBar()
    {
        InitializeComponent();
        Refresh();
    }

    public void Bind(ScriptDriver driver)
    {
        _driver = driver;
        _driver.Status += s => Dispatcher.BeginInvoke(() => StatusText.Text = s);
    }

    public void Refresh()
    {
        ScriptCombo.Items.Clear();
        foreach (var s in ScriptDriver.AvailableScripts())
            ScriptCombo.Items.Add(s);
        if (ScriptCombo.Items.Count > 0) ScriptCombo.SelectedIndex = 0;
        StatusText.Text = ScriptCombo.Items.Count == 0 ? "no scripts in /Scripts" : "";
    }

    private void Reload_Click(object sender, RoutedEventArgs e) => Refresh();

    private async void Run_Click(object sender, RoutedEventArgs e)
    {
        if (_driver == null) { StatusText.Text = "not bound"; return; }
        if (ScriptCombo.SelectedItem is not string path) return;
        try
        {
            RunBtn.IsEnabled = false;
            await _driver.StartAsync(path);
            StopBtn.IsEnabled = true;
        }
        catch
        {
            RunBtn.IsEnabled = true;
        }
    }

    private async void Stop_Click(object sender, RoutedEventArgs e)
    {
        if (_driver == null) return;
        await _driver.StopAsync();
        RunBtn.IsEnabled = true;
        StopBtn.IsEnabled = false;
    }

    public void Dispose()
    {
        if (_driver != null) _ = _driver.DisposeAsync();
    }
}
