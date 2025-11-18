using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using Themis.AqlQueryBuilder.ViewModels;

namespace Themis.AqlQueryBuilder;

/// <summary>
/// Interaction logic for MainWindow.xaml
/// </summary>
public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        
        // Create ViewModel with dependency injection
        DataContext = new MainViewModel(
            App.Services.Resolve<Services.IAqlQueryService>(),
            App.Services.Resolve<Services.ISchemaService>(),
            App.Services.Resolve<Services.IQueryHistoryService>()
        );
    }
}