using System.Windows;
using Themis.KeyRotationDashboard.ViewModels;

namespace Themis.KeyRotationDashboard.Views
{
    public partial class MainWindow : Window
    {
        private bool _sidebarVisible = true;

        public MainWindow(MainViewModel viewModel)
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
            SidebarPanel.Visibility = _sidebarVisible ? Visibility.Visible : Visibility.Collapsed;
            SidebarSplitter.Visibility = _sidebarVisible ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}