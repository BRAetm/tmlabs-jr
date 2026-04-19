using System.Windows;

namespace Labs.RemotePlay;

public partial class NewSessionDialog : Window
{
    public enum SessionKind { None, PsRemotePlay, XboxRemotePlay, CloudGaming, CaptureCard }
    public SessionKind Chosen { get; private set; } = SessionKind.None;

    public NewSessionDialog() { InitializeComponent(); }

    private void PsRp_Click(object sender, RoutedEventArgs e)
    {
        Chosen = SessionKind.PsRemotePlay;
        DialogResult = true;
        Close();
    }

    private void CaptureCard_Click(object sender, RoutedEventArgs e)
    {
        Chosen = SessionKind.CaptureCard;
        DialogResult = true;
        Close();
    }

    private void XboxRp_Click(object sender, RoutedEventArgs e)
    {
        Chosen = SessionKind.XboxRemotePlay;
        DialogResult = true;
        Close();
    }

    private void Cloud_Click(object sender, RoutedEventArgs e)
    {
        Chosen = SessionKind.CloudGaming;
        DialogResult = true;
        Close();
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) { DialogResult = false; Close(); }
}
