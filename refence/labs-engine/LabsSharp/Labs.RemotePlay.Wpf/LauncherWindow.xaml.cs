using System;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace Labs.RemotePlay;

public partial class LauncherWindow : Window
{
    public LauncherWindow() { InitializeComponent(); }

    private void ToolList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (HeroEngine == null || HeroZen == null || HeroCv == null) return;
        HeroEngine.Visibility = Visibility.Collapsed;
        HeroZen.Visibility = Visibility.Collapsed;
        HeroCv.Visibility = Visibility.Collapsed;
        switch (ToolList.SelectedIndex)
        {
            case 0: HeroEngine.Visibility = Visibility.Visible; break;
            case 1: HeroZen.Visibility = Visibility.Visible; break;
            case 2: HeroCv.Visibility = Visibility.Visible; break;
        }
    }

    private void LaunchEngine_Click(object sender, RoutedEventArgs e)
    {
        var main = new MainWindow();
        Application.Current.MainWindow = main;
        main.Show();
        Close();
    }

    private void LaunchCvRemote_Click(object sender, RoutedEventArgs e) => LaunchCv("remote");

    private void LaunchCvCapture_Click(object sender, RoutedEventArgs e) => LaunchCv("capture");

    private void LaunchCv(string sessionKind)
    {
        var (exe, args) = ResolveLabsLabeler();
        if (exe == null)
        {
            if (CvStatusLine2 != null)
            {
                CvStatusLine2.Text = "labs-labeler not found (expected C:\\labs-labeler)";
            }
            return;
        }
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = args,
                UseShellExecute = false,
                CreateNoWindow = false,
                WorkingDirectory = Path.GetDirectoryName(exe) ?? Environment.CurrentDirectory,
            };
            psi.EnvironmentVariables["LABSLABEL_SESSION"] = sessionKind;
            Process.Start(psi);
            if (CvStatusLine2 != null)
            {
                CvStatusLine2.Text = $"started · {sessionKind} session";
            }
        }
        catch (Exception ex)
        {
            if (CvStatusLine2 != null)
            {
                CvStatusLine2.Text = "launch failed: " + ex.Message;
            }
        }
    }

    private static (string? exe, string args) ResolveLabsLabeler()
    {
        // Prefer the venv python; fall back to system python if only that exists.
        var venvPy = @"C:\labs-labeler\.venv\Scripts\python.exe";
        if (File.Exists(venvPy))
        {
            return (venvPy, "-c \"from labslabel.app import main; main()\"");
        }
        var sysPy = "python";
        var repoCheck = @"C:\labs-labeler\src\labslabel\app.py";
        if (File.Exists(repoCheck))
        {
            return (sysPy, "-c \"from labslabel.app import main; main()\"");
        }
        return (null, "");
    }

    private void Exit_Click(object sender, RoutedEventArgs e) => Close();
}
