using System.Windows;
using Themis.AuditLogViewer.ViewModels;

namespace Themis.AuditLogViewer.Views;

public partial class MainWindow : Window
{
    private bool _sidebarVisible = true;

    public MainWindow(MainWindowViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }

    private void AboutButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new AboutWindow { Owner = this };
        dlg.ShowDialog();
    }

    private void HamburgerToggle_Click(object sender, RoutedEventArgs e)
    {
        _sidebarVisible = !_sidebarVisible;
        if (SidebarPanel != null) SidebarPanel.Visibility = _sidebarVisible ? Visibility.Visible : Visibility.Collapsed;
        if (SidebarSplitter != null) SidebarSplitter.Visibility = _sidebarVisible ? Visibility.Visible : Visibility.Collapsed;
    }
}
