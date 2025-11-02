using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using System;
using System.IO;
using System.Net.Http;
using System.Windows;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;
using Themis.KeyRotationDashboard.ViewModels;
using Themis.KeyRotationDashboard.Views;

namespace Themis.KeyRotationDashboard
{
    public partial class App : Application
    {
        private ServiceProvider? _serviceProvider;

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            var configuration = new ConfigurationBuilder()
                .SetBasePath(Directory.GetCurrentDirectory())
                .AddJsonFile("appsettings.json", optional: false, reloadOnChange: true)
                .Build();

            var services = new ServiceCollection();
            ConfigureServices(services, configuration);
            _serviceProvider = services.BuildServiceProvider();

            var mainWindow = _serviceProvider.GetRequiredService<MainWindow>();
            mainWindow.Show();
        }

        private void ConfigureServices(IServiceCollection services, IConfiguration configuration)
        {
            var themisServerUrl = configuration["ThemisServer:BaseUrl"] ?? "http://localhost:8080";
            var config = new ThemisServerConfig
            {
                BaseUrl = themisServerUrl,
                Timeout = 30,
                ApiKey = configuration["ThemisServer:ApiKey"] ?? string.Empty,
                JwtToken = configuration["ThemisServer:JwtToken"] ?? string.Empty
            };
            
            services.AddSingleton<HttpClient>(sp =>
            {
                var client = new HttpClient();
                return client;
            });
            
            services.AddSingleton(sp =>
            {
                var httpClient = sp.GetRequiredService<HttpClient>();
                return new ThemisApiClient(httpClient, config);
            });

            services.AddTransient<MainViewModel>();
            services.AddTransient<MainWindow>();
        }

        protected override void OnExit(ExitEventArgs e)
        {
            _serviceProvider?.Dispose();
            base.OnExit(e);
        }
    }
}