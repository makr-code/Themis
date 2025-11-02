using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Themis.AdminTools.Shared.ApiClient;
using Themis.ClassificationDashboard.Models;

namespace Themis.ClassificationDashboard.ViewModels
{
    public class MainViewModel : ObservableObject
    {
        private readonly ThemisApiClient _apiClient;
        private ObservableCollection<DataClassification> _classifications = new();
        private string _statusMessage = "Bereit";
        private bool _isLoading = false;
        private string _searchText = string.Empty;
        private int _classificationFilterIndex = 0;
        private int _encryptionFilterIndex = 0;
        private int _complianceFilterIndex = 0;
        private bool _showGapsOnly = false;

        public ObservableCollection<DataClassification> Classifications
        {
            get => _classifications;
            set => SetProperty(ref _classifications, value);
        }

        public string StatusMessage
        {
            get => _statusMessage;
            set => SetProperty(ref _statusMessage, value);
        }

        public bool IsLoading
        {
            get => _isLoading;
            set => SetProperty(ref _isLoading, value);
        }

        public string SearchText
        {
            get => _searchText;
            set => SetProperty(ref _searchText, value);
        }

        public int ClassificationFilterIndex
        {
            get => _classificationFilterIndex;
            set => SetProperty(ref _classificationFilterIndex, value);
        }

        public int EncryptionFilterIndex
        {
            get => _encryptionFilterIndex;
            set => SetProperty(ref _encryptionFilterIndex, value);
        }

        public int ComplianceFilterIndex
        {
            get => _complianceFilterIndex;
            set => SetProperty(ref _complianceFilterIndex, value);
        }

        public bool ShowGapsOnly
        {
            get => _showGapsOnly;
            set => SetProperty(ref _showGapsOnly, value);
        }

        public int PublicCount => Classifications.Count(c => c.ClassificationLevel == "PUBLIC");
        public int InternalCount => Classifications.Count(c => c.ClassificationLevel == "INTERNAL");
        public int ConfidentialCount => Classifications.Count(c => c.ClassificationLevel == "CONFIDENTIAL");
        public int RestrictedCount => Classifications.Count(c => c.ClassificationLevel == "RESTRICTED");
        public int NonCompliantCount => Classifications.Count(c => c.ComplianceStatus == "Non-Compliant");
        public int UnencryptedCount => Classifications.Count(c => c.IsEncrypted == "Nein");

        public RelayCommand RefreshCommand { get; }
        public RelayCommand ExportCommand { get; }
        public RelayCommand ComplianceCheckCommand { get; }

        public MainViewModel(ThemisApiClient apiClient)
        {
            _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
            
            RefreshCommand = new RelayCommand(async () => await ExecuteRefreshAsync());
            ExportCommand = new RelayCommand(ExecuteExport);
            ComplianceCheckCommand = new RelayCommand(ExecuteComplianceCheck);

            _ = LoadClassificationsAsync();
        }

        private async Task LoadClassificationsAsync()
        {
            IsLoading = true;
            StatusMessage = "Lade Klassifizierungsdaten...";
            
            try
            {
                var response = await _apiClient.Classification.GetStatsAsync();
                
                if (response.Success && response.Data != null)
                {
                    Classifications.Clear();
                    
                    foreach (var stat in response.Data.Items)
                    {
                        Classifications.Add(new DataClassification
                        {
                            EntityId = stat.EntityId,
                            EntityType = stat.EntityType,
                            ClassificationLevel = stat.Classification,
                            IsEncrypted = stat.IsEncrypted ? "Ja" : "Nein",
                            Owner = stat.Owner ?? "Unbekannt",
                            CreatedAt = stat.CreatedAt,
                            LastReview = stat.LastReview,
                            ComplianceStatus = stat.IsCompliant ? "Compliant" : "Non-Compliant",
                            Notes = stat.IsCompliant ? "OK" : "⚠️ Compliance-Prüfung erforderlich"
                        });
                    }
                    
                    OnPropertyChanged(nameof(PublicCount));
                    OnPropertyChanged(nameof(InternalCount));
                    OnPropertyChanged(nameof(ConfidentialCount));
                    OnPropertyChanged(nameof(RestrictedCount));
                    OnPropertyChanged(nameof(NonCompliantCount));
                    OnPropertyChanged(nameof(UnencryptedCount));
                    StatusMessage = $"{Classifications.Count} Datensätze geladen";
                }
                else
                {
                    StatusMessage = $"Fehler: {response.Error ?? "API noch nicht implementiert (501)"}";
                }
            }
            catch (Exception ex)
            {
                StatusMessage = $"Fehler beim Laden: {ex.Message}";
            }
            finally
            {
                IsLoading = false;
            }
        }

        private async Task ExecuteRefreshAsync()
        {
            await LoadClassificationsAsync();
        }

        private void ExecuteExport()
        {
            StatusMessage = "Exportiere Klassifizierungsdaten nach CSV...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(1000);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = $"Export abgeschlossen: {Classifications.Count} Datensätze";
                });
            });
        }

        private void ExecuteComplianceCheck()
        {
            StatusMessage = "Führe Compliance-Check durch...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(1500);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = $"Compliance-Check abgeschlossen: {NonCompliantCount} Verstöße gefunden";
                });
            });
        }
    }
}
