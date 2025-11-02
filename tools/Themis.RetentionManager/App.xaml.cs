using System;
using System.IO;
using System.Net.Http;
using System.Windows;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;
using Themis.RetentionManager.ViewModels;

namespace Themis.RetentionManager
{
    public partial class App : Application
    {
        private ServiceProvider? _serviceProvider;

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            try
            {
                // Build configuration
                var configuration = new ConfigurationBuilder()
                    .SetBasePath(Directory.GetCurrentDirectory())
                    .AddJsonFile("appsettings.json", optional: true, reloadOnChange: true)
                    .Build();

                // Setup DI
                var services = new ServiceCollection();

                // Register configuration
                var serverConfig = configuration.GetSection("ThemisServer").Get<ThemisServerConfig>() 
                    ?? new ThemisServerConfig { BaseUrl = "http://localhost:8765", Timeout = 30 };
                services.AddSingleton(serverConfig);

                // Register API client
                services.AddTransient<ThemisApiClient>(sp =>
                {
                    var config = sp.GetRequiredService<ThemisServerConfig>();
                    var httpClient = new HttpClient
                    {
                        BaseAddress = new Uri(config.BaseUrl),
                        Timeout = TimeSpan.FromSeconds(config.Timeout)
                    };
                    return new ThemisApiClient(httpClient, config);
                });

                // Register ViewModels
                services.AddTransient<MainViewModel>();
                
                // Register Windows
                services.AddTransient<Views.MainWindow>();

                _serviceProvider = services.BuildServiceProvider();

                // Show main window
                var mainWindow = _serviceProvider.GetRequiredService<Views.MainWindow>();
                mainWindow.Show();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Fehler beim Start: {ex.Message}", "Fehler", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown(1);
            }
        }

        protected override void OnExit(ExitEventArgs e)
        {
            _serviceProvider?.Dispose();
            base.OnExit(e);
        }
    }
}
