using System.Windows;

namespace Labs.RemotePlay;

public partial class SettingsWindow : Window
{
    public SettingsWindow() { InitializeComponent(); }
    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
