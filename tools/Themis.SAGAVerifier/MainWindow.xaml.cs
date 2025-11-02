using System;
using System.Windows;
using Themis.SAGAVerifier.ViewModels;

namespace Themis.SAGAVerifier
{
    public partial class MainWindow : Window
    {
        private bool _sidebarVisible = true;

        public MainWindow(MainViewModel viewModel)
        {
            InitializeComponent();
            DataContext = viewModel;
            Loaded += async (s, e) =>
            {
                try
                {
                    await viewModel.LoadBatchesCommand.ExecuteAsync(null);
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Error loading batches: {ex.Message}\n\n{ex.StackTrace}", 
                        "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            };
        }

        private void AboutButton_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new AboutWindow { Owner = this };
            dlg.ShowDialog();
        }

        private void HamburgerToggle_Click(object sender, RoutedEventArgs e)
        {
            // Toggle sidebar visibility
            _sidebarVisible = !_sidebarVisible;
            if (SidebarPanel != null) SidebarPanel.Visibility = _sidebarVisible ? Visibility.Visible : Visibility.Collapsed;
            if (SidebarSplitter != null) SidebarSplitter.Visibility = _sidebarVisible ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}
