using System;
using System.Threading;
using System.Windows;
using Labs.Engine.Discovery;
using Labs.Engine.Psn;
using Labs.Engine.Registration;

namespace Labs.RemotePlay;

public partial class RegisterDialog : Window
{
    private readonly DiscoveredHost _host;

    public RegisterDialog(DiscoveredHost host)
    {
        InitializeComponent();
        _host = host;
        HostHeader.Text = $"Register with {host.Name}";
    }

    private async void Ok_Click(object sender, RoutedEventArgs e)
    {
        ErrorText.Text = "";
        var accountId = AccountStore.Load();
        if (string.IsNullOrEmpty(accountId)) { ErrorText.Text = "Sign in with PSN first."; return; }
        var pin = PinBox.Text.Trim();
        if (pin.Length != 8 || !int.TryParse(pin, out _)) { ErrorText.Text = "PIN must be 8 digits."; return; }

        OkButton.IsEnabled = false;
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
            var creds = await RegistrationService.RegisterAsync(_host, accountId, pin, cts.Token);
            RegistrationStore.Save(_host.HostId, creds);
            DialogResult = true;
            Close();
        }
        catch (Exception ex)
        {
            ErrorText.Text = ex.Message;
            OkButton.IsEnabled = true;
        }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) { DialogResult = false; Close(); }
}
