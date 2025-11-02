using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Themis.KeyRotationDashboard.Models;
using Themis.AdminTools.Shared.ApiClient;

namespace Themis.KeyRotationDashboard.ViewModels
{
    public class MainViewModel : ObservableObject
    {
        private readonly ThemisApiClient _apiClient;
        private ObservableCollection<KeyRotationInfo> _keys = new();
        private string _statusMessage = "Bereit";
        private bool _isLoading = false;
        private string _searchText = string.Empty;
        private int _keyTypeFilterIndex = 0;
        private bool _showExpiredOnly = false;

        public ObservableCollection<KeyRotationInfo> Keys
        {
            get => _keys;
            set => SetProperty(ref _keys, value);
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

        public int KeyTypeFilterIndex
        {
            get => _keyTypeFilterIndex;
            set => SetProperty(ref _keyTypeFilterIndex, value);
        }

        public bool ShowExpiredOnly
        {
            get => _showExpiredOnly;
            set => SetProperty(ref _showExpiredOnly, value);
        }

        public int ExpiredCount => Keys.Count(k => k.Status == "Abgelaufen");

        public RelayCommand RefreshCommand { get; }
        public RelayCommand RotateKeyCommand { get; }
        public RelayCommand RotateLEKCommand { get; }
        public RelayCommand RotateKEKCommand { get; }
        public RelayCommand RotateDEKCommand { get; }

        public MainViewModel(ThemisApiClient apiClient)
        {
            _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
            
            RefreshCommand = new RelayCommand(async () => await ExecuteRefreshAsync());
            RotateKeyCommand = new RelayCommand(ExecuteRotateKey);
            RotateLEKCommand = new RelayCommand(async () => await ExecuteRotateLEKAsync());
            RotateKEKCommand = new RelayCommand(async () => await ExecuteRotateKEKAsync());
            RotateDEKCommand = new RelayCommand(async () => await ExecuteRotateDEKAsync());

            _ = LoadKeysAsync();
        }

        private async Task LoadKeysAsync()
        {
            IsLoading = true;
            StatusMessage = "Lade Schlüssel...";
            
            try
            {
                var response = await _apiClient.Keys.GetKeysAsync();
                
                if (response.Success && response.Data != null)
                {
                    Keys.Clear();
                    
                    foreach (var key in response.Data.Items)
                    {
                        var now = DateTimeOffset.Now;
                        var expiresIn = key.Expires.HasValue ? (key.Expires.Value - DateTime.Now).Days : 999;
                        string status = key.Status == "Expired" ? "Abgelaufen" : 
                                       expiresIn < 30 ? "Läuft ab" : "Aktiv";
                        
                        Keys.Add(new KeyRotationInfo
                        {
                            KeyId = key.Id,
                            KeyType = key.Id.StartsWith("LEK") ? "LEK" : key.Id.StartsWith("KEK") ? "KEK" : "DEK",
                            Version = key.Version,
                            CreatedAt = key.Created,
                            LastRotation = key.Created,
                            NextRotation = key.Expires,
                            Status = status,
                            RotationInterval = "90 Tage"
                        });
                    }
                    
                    OnPropertyChanged(nameof(ExpiredCount));
                    StatusMessage = $"{Keys.Count} Schlüssel geladen";
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
            await LoadKeysAsync();
        }

        private void ExecuteRotateKey()
        {
            StatusMessage = "Bitte wählen Sie einen Schlüssel aus";
        }

        private async Task ExecuteRotateLEKAsync()
        {
            await RotateKeysByTypeAsync("LEK");
        }

        private async Task ExecuteRotateKEKAsync()
        {
            await RotateKeysByTypeAsync("KEK");
        }

        private async Task ExecuteRotateDEKAsync()
        {
            await RotateKeysByTypeAsync("DEK");
        }

        private async Task RotateKeysByTypeAsync(string keyType)
        {
            StatusMessage = $"{keyType} wird rotiert...";
            IsLoading = true;
            
            try
            {
                var keysOfType = Keys.Where(k => k.KeyType == keyType).ToList();
                
                if (!keysOfType.Any())
                {
                    StatusMessage = $"Keine {keyType}-Schlüssel gefunden";
                    return;
                }
                
                foreach (var key in keysOfType)
                {
                    var response = await _apiClient.Keys.RotateKeyAsync(key.KeyId);
                    
                    if (!response.Success)
                    {
                        StatusMessage = $"Fehler bei {key.KeyId}: {response.Error ?? "API noch nicht implementiert (501)"}";
                        return;
                    }
                }
                
                StatusMessage = $"{keyType}-Rotation abgeschlossen";
                await LoadKeysAsync();
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
    }
}