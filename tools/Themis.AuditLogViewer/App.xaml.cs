using System.Windows;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;
using Themis.AuditLogViewer.ViewModels;
using Themis.AuditLogViewer.Views;

namespace Themis.AuditLogViewer;

public partial class App : Application
{
    private ServiceProvider? _serviceProvider;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // Configuration
        var configuration = new ConfigurationBuilder()
            .SetBasePath(AppDomain.CurrentDomain.BaseDirectory)
            .AddJsonFile("appsettings.json", optional: false, reloadOnChange: true)
            .Build();

        // Dependency Injection
        var services = new ServiceCollection();
        
        // Configuration
        var serverConfig = configuration.GetSection("ThemisServer").Get<ThemisServerConfig>() 
            ?? new ThemisServerConfig();
        services.AddSingleton(serverConfig);

        // HTTP Client
        services.AddHttpClient<ThemisApiClient>();

        // ViewModels
        services.AddTransient<MainWindowViewModel>();

        // Views
        services.AddTransient<MainWindow>();

        _serviceProvider = services.BuildServiceProvider();

        // Show main window
        var mainWindow = _serviceProvider.GetRequiredService<MainWindow>();
        mainWindow.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _serviceProvider?.Dispose();
        base.OnExit(e);
    }
}
