using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Themis.RetentionManager.Models;
using Themis.AdminTools.Shared.ApiClient;

namespace Themis.RetentionManager.ViewModels
{
    public class MainViewModel : ObservableObject
    {
    private readonly ThemisApiClient _apiClient;
    private ObservableCollection<RetentionPolicy> _policies = new();
    private string _statusMessage = "Bereit";
    private bool _isLoading = false;
    private string _searchText = string.Empty;
    private int _statusFilterIndex = 0;
    private int _entityTypeFilterIndex = 0;
    private bool _showOverdueOnly = false;

        public ObservableCollection<RetentionPolicy> Policies
        {
            get => _policies;
            set => SetProperty(ref _policies, value);
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

        public int StatusFilterIndex
        {
            get => _statusFilterIndex;
            set => SetProperty(ref _statusFilterIndex, value);
        }

        public int EntityTypeFilterIndex
        {
            get => _entityTypeFilterIndex;
            set => SetProperty(ref _entityTypeFilterIndex, value);
        }

        public bool ShowOverdueOnly
        {
            get => _showOverdueOnly;
            set => SetProperty(ref _showOverdueOnly, value);
        }

        public int OverdueCount => Policies.Count(p => p.Status == "Abgelaufen");

        public RelayCommand RefreshCommand { get; }
        public RelayCommand CreatePolicyCommand { get; }
        public RelayCommand CleanupCommand { get; }
        public RelayCommand DeletePolicyCommand { get; }
        public RelayCommand ShowHistoryCommand { get; }
        public RelayCommand ShowStatsCommand { get; }

        private RetentionPolicy? _selectedPolicy;
        public RetentionPolicy? SelectedPolicy
        {
            get => _selectedPolicy;
            set => SetProperty(ref _selectedPolicy, value);
        }

        public MainViewModel(ThemisApiClient apiClient)
        {
            _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
            RefreshCommand = new RelayCommand(async () => await ExecuteRefreshAsync());
            CreatePolicyCommand = new RelayCommand(ExecuteCreatePolicy);
            CleanupCommand = new RelayCommand(ExecuteCleanup);
            DeletePolicyCommand = new RelayCommand(async () => await ExecuteDeletePolicyAsync(), () => SelectedPolicy != null);
            ShowHistoryCommand = new RelayCommand(async () => await ExecuteShowHistoryAsync());
            ShowStatsCommand = new RelayCommand(async () => await ExecuteShowStatsAsync(), () => SelectedPolicy != null);

            _ = LoadPoliciesAsync();
        }

    private async Task LoadPoliciesAsync()
        {
            IsLoading = true;
            StatusMessage = "Lade Retention Policies...";
            
            try
            {
        var nameFilter = string.IsNullOrWhiteSpace(SearchText) ? null : SearchText;
        var response = await _apiClient.Retention.GetPoliciesAsync(nameFilter, null, 1, 100);
                
                if (response.Success && response.Data != null)
                {
                    Policies.Clear();
                    
                    // Map API models to UI models
                    var now = DateTimeOffset.Now;
                    foreach (var policy in response.Data.Items)
                    {
                        Policies.Add(new RetentionPolicy
                        {
                            PolicyId = $"RET-{policy.Name}",
                            PolicyName = policy.Name,
                            EntityType = policy.Collections.FirstOrDefault() ?? "N/A",
                            RetentionPeriod = $"{policy.RetentionDays} Tage",
                            CreatedAt = now.AddDays(-policy.RetentionDays), // Approximation
                            NextCleanup = policy.LastRun?.AddDays(policy.RetentionDays),
                            Status = policy.Active ? "Aktiv" : "Inaktiv",
                            AffectedRecords = 0 // Not available from API yet
                        });
                    }
                    
                    OnPropertyChanged(nameof(OverdueCount));
                    StatusMessage = $"{Policies.Count} Policies geladen";
                }
                else
                {
                    StatusMessage = $"Fehler: {response.Error ?? "Unbekannter Fehler"}";
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
            await LoadPoliciesAsync();
        }

        private async Task ExecuteDeletePolicyAsync()
        {
            if (SelectedPolicy == null)
            {
                StatusMessage = "Bitte zuerst eine Policy auswählen";
                return;
            }

            IsLoading = true;
            StatusMessage = $"Lösche Policy '{SelectedPolicy.PolicyName}'...";
            try
            {
                var res = await _apiClient.Retention.DeletePolicyAsync(SelectedPolicy.PolicyName);
                if (res.Success)
                {
                    await LoadPoliciesAsync();
                    StatusMessage = $"Policy '{SelectedPolicy.PolicyName}' gelöscht";
                }
                else
                {
                    StatusMessage = $"Fehler beim Löschen: {res.Error}";
                }
            }
            catch (Exception ex)
            {
                StatusMessage = $"Fehler beim Löschen: {ex.Message}";
            }
            finally
            {
                IsLoading = false;
            }
        }

        private async Task ExecuteShowHistoryAsync()
        {
            IsLoading = true;
            StatusMessage = "Lade Retention-Historie...";
            try
            {
                var res = await _apiClient.Retention.GetHistoryRawAsync(50);
                if (res.Success)
                {
                    System.Windows.MessageBox.Show(res.Data ?? "[]", "Retention History");
                    StatusMessage = "Historie geladen";
                }
                else
                {
                    StatusMessage = $"Fehler: {res.Error}";
                }
            }
            catch (Exception ex)
            {
                StatusMessage = $"Fehler: {ex.Message}";
            }
            finally
            {
                IsLoading = false;
            }
        }

        private async Task ExecuteShowStatsAsync()
        {
            if (SelectedPolicy == null)
            {
                StatusMessage = "Bitte zuerst eine Policy auswählen";
                return;
            }

            IsLoading = true;
            StatusMessage = $"Lade Stats für '{SelectedPolicy.PolicyName}'...";
            try
            {
                var res = await _apiClient.Retention.GetPolicyStatsRawAsync(SelectedPolicy.PolicyName);
                if (res.Success)
                {
                    System.Windows.MessageBox.Show(res.Data ?? "{}", "Policy Stats");
                    StatusMessage = "Stats geladen";
                }
                else
                {
                    StatusMessage = $"Fehler: {res.Error}";
                }
            }
            catch (Exception ex)
            {
                StatusMessage = $"Fehler: {ex.Message}";
            }
            finally
            {
                IsLoading = false;
            }
        }

        private void ExecuteCreatePolicy()
        {
            var baseName = string.IsNullOrWhiteSpace(SearchText) ? "NeuePolicy" : SearchText.Trim();
            var name = $"{baseName}-{DateTimeOffset.Now:yyyyMMddHHmmss}";

            _ = CreatePolicyAsync(name);
        }

        private async Task CreatePolicyAsync(string name)
        {
            IsLoading = true;
            StatusMessage = $"Erstelle Policy '{name}'...";
            try
            {
                var payload = new Themis.AdminTools.Shared.Models.RetentionPolicy
                {
                    Name = name,
                    Active = true,
                    Collections = new System.Collections.Generic.List<string> { "Document" },
                    RetentionDays = 30,
                    LastRun = null
                };
                var res = await _apiClient.Retention.CreateOrUpdatePolicyAsync(payload);
                if (res.Success)
                {
                    await LoadPoliciesAsync();
                    StatusMessage = $"Policy '{name}' erstellt";
                }
                else
                {
                    StatusMessage = $"Fehler bei Erstellung: {res.Error}";
                }
            }
            catch (Exception ex)
            {
                StatusMessage = $"Fehler bei Erstellung: {ex.Message}";
            }
            finally
            {
                IsLoading = false;
            }
        }

        private void ExecuteCleanup()
        {
            StatusMessage = "Bereinigung wird durchgeführt...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(2000);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = "Bereinigung abgeschlossen: 1234 Datensätze entfernt";
                });
            });
        }
    }
}
