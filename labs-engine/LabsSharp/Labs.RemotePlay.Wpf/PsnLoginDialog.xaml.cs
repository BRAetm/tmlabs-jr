using System;
using System.IO;
using System.Windows;
using Labs.Engine.Psn;
using Microsoft.Web.WebView2.Core;

namespace Labs.RemotePlay;

public partial class PsnLoginDialog : Window
{
    public string? AccountIdBase64 { get; private set; }

    public PsnLoginDialog()
    {
        InitializeComponent();
        Loaded += async (_, _) => await InitAsync();
    }

    private async System.Threading.Tasks.Task InitAsync()
    {
        try
        {
            var dataDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "PSRemotePlay", "WebView2");
            Directory.CreateDirectory(dataDir);
            var env = await CoreWebView2Environment.CreateAsync(null, dataDir);
            await Web.EnsureCoreWebView2Async(env);

            Web.CoreWebView2.NavigationStarting += OnNavigationStarting;
            Web.CoreWebView2.Navigate(PsnAuth.AuthorizeUrl);
            StatusText.Text = "Sign in to your PlayStation account to link this device.";
        }
        catch (Exception ex)
        {
            StatusText.Text = "WebView2 failed to initialize: " + ex.Message +
                              "\nInstall the Edge WebView2 Runtime from Microsoft.";
        }
    }

    private async void OnNavigationStarting(object? sender, CoreWebView2NavigationStartingEventArgs e)
    {
        if (!e.Uri.StartsWith(PsnAuth.RedirectPrefix, StringComparison.OrdinalIgnoreCase)) return;

        e.Cancel = true; // don't hit Sony's blank redirect page
        var code = PsnAuth.ExtractCode(e.Uri);
        if (string.IsNullOrEmpty(code))
        {
            StatusText.Text = "Sign-in completed but no code= was in the redirect URL: " + e.Uri;
            return;
        }
        StatusText.Text = "Signing in…";
        try
        {
            AccountIdBase64 = await PsnAuth.ExchangeAsync(code);
            AccountStore.Save(AccountIdBase64);
            DialogResult = true;
            Close();
        }
        catch (Exception ex) { StatusText.Text = "Sign-in failed: " + ex.Message; }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) { DialogResult = false; Close(); }
}
