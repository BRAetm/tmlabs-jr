using System;
using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using Labs.Engine.Discovery;
using Labs.Engine.Psn;
using Labs.Engine.Registration;

namespace Labs.RemotePlay;

public partial class ConsolePickerWindow : Window
{
    private readonly ObservableCollection<DiscoveredHost> _hosts = new();
    private readonly HostDiscovery _discovery = new();
    private DiscoveredHost? _selected;

    public ConsolePickerWindow()
    {
        InitializeComponent();
        HostList.ItemsSource = _hosts;
        _discovery.HostFound += OnHostFound;
        Loaded += async (_, _) =>
        {
            RefreshAccountStatus();
            StatusBox.Text = "Searching for consoles…";
            _ = _discovery.StartAsync();
            await System.Threading.Tasks.Task.CompletedTask;
        };
        Closed += (_, _) => _discovery.Dispose();
    }

    private void OnHostFound(DiscoveredHost host)
    {
        Dispatcher.Invoke(() =>
        {
            _hosts.Add(host);
            StatusBox.Text = $"Found {_hosts.Count} console(s). Select one to continue.";
        });
    }

    private void HostList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _selected = HostList.SelectedItem as DiscoveredHost;
        if (_selected == null)
        {
            SelectedName.Text = "No console selected";
            SelectedStatus.Text = "Pick a console from the list or wait for discovery.";
            RegisterButton.IsEnabled = false;
            ConnectButton.IsEnabled = false;
            return;
        }
        SelectedName.Text = _selected.Name;
        SelectedStatus.Text = $"{_selected.Address} — status: {_selected.Status}";
        RegisterButton.IsEnabled = true;
        ConnectButton.IsEnabled = RegistrationStore.IsRegistered(_selected.HostId);
    }

    private void RegisterButton_Click(object sender, RoutedEventArgs e)
    {
        if (_selected == null) return;
        if (!RequirePsn()) return;
        var dlg = new RegisterDialog(_selected) { Owner = this };
        if (dlg.ShowDialog() == true)
            ConnectButton.IsEnabled = true;
    }

    private void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        if (_selected == null) return;
        if (!RegistrationStore.IsRegistered(_selected.HostId))
        {
            if (!RequirePsn()) return;
            var dlg = new RegisterDialog(_selected) { Owner = this };
            if (dlg.ShowDialog() != true) return;
        }
        var stream = new StreamWindow(_selected) { Owner = this };
        stream.Show();
    }

    private void PsnSignIn_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new PsnLoginDialog { Owner = this };
        if (dlg.ShowDialog() == true) RefreshAccountStatus();
    }

    private void SettingsButton_Click(object sender, RoutedEventArgs e)
    {
        new SettingsWindow { Owner = this }.ShowDialog();
    }

    private bool RequirePsn()
    {
        if (AccountStore.HasAccount) return true;
        MessageBox.Show(this, "Sign in with PSN first (button on the sidebar).",
            "PS Remote Play", MessageBoxButton.OK, MessageBoxImage.Information);
        return false;
    }

    private void RefreshAccountStatus()
    {
        AccountStatus.Text = AccountStore.HasAccount ? "Signed in to PSN ✓" : "Not signed in";
        PsnSignInButton.Content = AccountStore.HasAccount ? "Re-sign in to PSN" : "Sign in with PSN";
    }
}
