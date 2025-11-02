using System;
using System.Collections.ObjectModel;
using System.Net.Http;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;

namespace Themis.PIIManager.ViewModels
{
    public partial class MainViewModel : ObservableObject
    {
    private readonly ThemisApiClient _api = new(new HttpClient(), new ThemisServerConfig());

        [ObservableProperty]
        private ObservableCollection<PiiMapping> mappings = new();

        [ObservableProperty]
        private string? searchText;

        [ObservableProperty]
        private string? originalUuidFilter;

        [ObservableProperty]
        private string? pseudonymFilter;

        [ObservableProperty]
        private bool activeOnlyFilter = true;

        [ObservableProperty]
        private bool isLoading;

        [ObservableProperty]
        private string statusMessage = "Bereit";

        public MainViewModel()
        {
        }

        [RelayCommand]
        private async Task LoadAsync()
        {
            try
            {
                IsLoading = true;
                StatusMessage = "Lade Mappings...";
                var resp = await _api.GetPiiMappingsAsync(
                    OriginalUuidFilter,
                    PseudonymFilter,
                    ActiveOnlyFilter,
                    page: 1,
                    pageSize: 200);
                Mappings.Clear();
                if (resp.Success && resp.Data != null)
                {
                    foreach (var m in resp.Data.Items)
                        Mappings.Add(m);
                    StatusMessage = $"Geladen: {resp.Data.Total}";
                }
                else
                {
                    StatusMessage = "Fehler beim Laden: " + (resp.Error ?? "unbekannt");
                }
            }
            finally
            {
                IsLoading = false;
            }
        }

        [RelayCommand]
        private async Task ExportAsync()
        {
            try
            {
                IsLoading = true;
                StatusMessage = "Exportiere CSV...";
                var resp = await _api.ExportPiiCsvAsync(OriginalUuidFilter, PseudonymFilter, ActiveOnlyFilter);
                if (resp.Success && resp.Data != null)
                {
                    // TODO: SaveFileDialog integration – for now we just confirm success
                    StatusMessage = $"Export: {resp.Data.Length} Bytes";
                }
                else
                {
                    StatusMessage = "Export fehlgeschlagen: " + (resp.Error ?? "unbekannt");
                }
                StatusMessage = "Export abgeschlossen";
            }
            finally
            {
                IsLoading = false;
            }
        }

        [RelayCommand]
        private void ClearFilters()
        {
            SearchText = OriginalUuidFilter = PseudonymFilter = string.Empty;
            ActiveOnlyFilter = true;
            StatusMessage = "Filter zurückgesetzt";
        }

        [RelayCommand]
        private async Task DeleteByUuidAsync()
        {
            if (string.IsNullOrWhiteSpace(OriginalUuidFilter))
            {
                StatusMessage = "Bitte Original-UUID angeben";
                return;
            }
            try
            {
                IsLoading = true;
                StatusMessage = "Lösche Daten gemäß DSGVO Art.17...";
                var resp = await _api.DeletePiiByUuidAsync(OriginalUuidFilter!);
                StatusMessage = resp.Success ? "Löschung angestoßen" : ("Fehler: " + (resp.Error ?? "unbekannt"));
            }
            finally
            {
                IsLoading = false;
            }
        }
    }
}
