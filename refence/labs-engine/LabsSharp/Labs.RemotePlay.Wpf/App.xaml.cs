using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;
using Labs.Engine;

namespace Labs.RemotePlay;

public partial class App : Application
{
    public static string LogPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "PSRemotePlay", "labs.log");

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
        File.AppendAllText(LogPath, $"\n---- start {DateTime.Now:u} ----\n");

        AppDomain.CurrentDomain.UnhandledException += (_, ev) =>
        {
            var ex = ev.ExceptionObject as Exception;
            File.AppendAllText(LogPath, $"[unhandled] {DateTime.Now:u} {ex}\n");
        };
        DispatcherUnhandledException += (_, ev) =>
        {
            File.AppendAllText(LogPath, $"[ui] {DateTime.Now:u} {ev.Exception}\n");
            ev.Handled = true; // don't crash — log and continue
        };
        TaskScheduler.UnobservedTaskException += (_, ev) =>
        {
            File.AppendAllText(LogPath, $"[task] {DateTime.Now:u} {ev.Exception}\n");
            ev.SetObserved();
        };

        try { LabsRuntime.Initialize(); }
        catch (LabsException ex)
        {
            File.AppendAllText(LogPath, $"[init] {ex}\n");
            MessageBox.Show($"Failed to initialize labs engine: {ex.Message}\n\nLog: {LogPath}",
                "PS Remote Play", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }
}
