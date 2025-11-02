using System.Windows;
using Themis.PIIManager.ViewModels;

namespace Themis.PIIManager.Views
{
    public partial class MainWindow : Window
    {
        private bool _sidebarVisible = true;

        public MainWindow()
        {
            InitializeComponent();
            DataContext = new MainViewModel();
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