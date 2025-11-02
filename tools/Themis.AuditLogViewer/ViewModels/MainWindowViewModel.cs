using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;

namespace Themis.AuditLogViewer.ViewModels;

public class MainWindowViewModel : INotifyPropertyChanged
{
    private readonly ThemisApiClient _apiClient;
    
    private DateTime _startDate = DateTime.Today.AddDays(-7);
    private DateTime _endDate = DateTime.Today.AddDays(1);
    private string _userFilter = string.Empty;
    private string _actionFilter = string.Empty;
    private string _entityTypeFilter = string.Empty;
    private string _searchText = string.Empty;
    private bool _isLoading;
    private string _statusMessage = "Bereit";
    private int _totalCount;
    private int _currentPage = 1;
    private bool _successOnlyFilter;
    private string _sortColumn = "Timestamp";
    private bool _sortAscending = false;
    
    private ObservableCollection<AuditLogEntry> _allLogs = new();
    private ICollectionView? _logsView;

    public MainWindowViewModel(ThemisApiClient apiClient)
    {
        _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
        
        AuditLogs = new ObservableCollection<AuditLogEntry>();
        _logsView = CollectionViewSource.GetDefaultView(AuditLogs);
        _logsView.Filter = FilterLogs;
        
        LoadCommand = new AsyncRelayCommand(LoadAuditLogsAsync);
        ExportCommand = new AsyncRelayCommand(ExportToCsvAsync);
        NextPageCommand = new AsyncRelayCommand(NextPageAsync, () => HasMorePages);
        PreviousPageCommand = new AsyncRelayCommand(PreviousPageAsync, () => CurrentPage > 1);
        ClearFiltersCommand = new RelayCommand(ClearFilters);
        SortCommand = new RelayCommand<string>(ApplySort);
        SearchCommand = new RelayCommand(ApplySearch);
    }

    public ObservableCollection<AuditLogEntry> AuditLogs { get; }

    public DateTime StartDate
    {
        get => _startDate;
        set => SetProperty(ref _startDate, value);
    }

    public DateTime EndDate
    {
        get => _endDate;
        set => SetProperty(ref _endDate, value);
    }

    public string UserFilter
    {
        get => _userFilter;
        set => SetProperty(ref _userFilter, value);
    }

    public string ActionFilter
    {
        get => _actionFilter;
        set => SetProperty(ref _actionFilter, value);
    }

    public string EntityTypeFilter
    {
        get => _entityTypeFilter;
        set => SetProperty(ref _entityTypeFilter, value);
    }

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                ApplySearch();
            }
        }
    }

    public string SortColumn
    {
        get => _sortColumn;
        set => SetProperty(ref _sortColumn, value);
    }

    public bool SortAscending
    {
        get => _sortAscending;
        set => SetProperty(ref _sortAscending, value);
    }

    public bool SuccessOnlyFilter
    {
        get => _successOnlyFilter;
        set => SetProperty(ref _successOnlyFilter, value);
    }

    public bool IsLoading
    {
        get => _isLoading;
        set => SetProperty(ref _isLoading, value);
    }

    public string StatusMessage
    {
        get => _statusMessage;
        set => SetProperty(ref _statusMessage, value);
    }

    public int TotalCount
    {
        get => _totalCount;
        set => SetProperty(ref _totalCount, value);
    }

    public int CurrentPage
    {
        get => _currentPage;
        set
        {
            SetProperty(ref _currentPage, value);
            ((AsyncRelayCommand)NextPageCommand).NotifyCanExecuteChanged();
            ((AsyncRelayCommand)PreviousPageCommand).NotifyCanExecuteChanged();
        }
    }

    public bool HasMorePages => CurrentPage * 100 < TotalCount;

    public ICommand LoadCommand { get; }
    public ICommand ExportCommand { get; }
    public ICommand NextPageCommand { get; }
    public ICommand PreviousPageCommand { get; }
    public ICommand ClearFiltersCommand { get; }
    public ICommand SortCommand { get; }
    public ICommand SearchCommand { get; }

    private bool FilterLogs(object obj)
    {
        if (obj is not AuditLogEntry log)
            return false;

        // Suchtext-Filter (alle Spalten durchsuchen)
        if (!string.IsNullOrWhiteSpace(SearchText))
        {
            var search = SearchText.ToLowerInvariant();
            var matches = log.User?.ToLowerInvariant().Contains(search) == true ||
                         log.Action?.ToLowerInvariant().Contains(search) == true ||
                         log.EntityType?.ToLowerInvariant().Contains(search) == true ||
                         log.EntityId?.ToLowerInvariant().Contains(search) == true ||
                         log.OldValue?.ToLowerInvariant().Contains(search) == true ||
                         log.NewValue?.ToLowerInvariant().Contains(search) == true ||
                         log.IpAddress?.ToLowerInvariant().Contains(search) == true ||
                         log.SessionId?.ToLowerInvariant().Contains(search) == true ||
                         log.ErrorMessage?.ToLowerInvariant().Contains(search) == true;
            
            if (!matches)
                return false;
        }

        return true;
    }

    private void ApplySearch()
    {
        _logsView?.Refresh();
        UpdateStatusMessage();
    }

    private void ApplySort(string? columnName)
    {
        if (string.IsNullOrEmpty(columnName))
            return;

        // Toggle sort direction if same column
        if (SortColumn == columnName)
        {
            SortAscending = !SortAscending;
        }
        else
        {
            SortColumn = columnName;
            SortAscending = true;
        }

        // Apply sorting
        _logsView?.SortDescriptions.Clear();
        _logsView?.SortDescriptions.Add(new SortDescription(SortColumn, 
            SortAscending ? ListSortDirection.Ascending : ListSortDirection.Descending));
        
        UpdateStatusMessage();
    }

    private void UpdateStatusMessage()
    {
        var filtered = AuditLogs.Count(log => _logsView?.Filter == null || _logsView.Filter(log));
        var total = AuditLogs.Count;
        
        if (filtered != total)
        {
            StatusMessage = $"{filtered} von {total} Einträgen angezeigt (Gesamt im System: {TotalCount})";
        }
        else
        {
            StatusMessage = $"{total} Einträge geladen (Gesamt: {TotalCount})";
        }
    }

    private async Task LoadAuditLogsAsync()
    {
        IsLoading = true;
        StatusMessage = "Lade Audit-Logs...";

        try
        {
            var filter = new AuditLogFilter
            {
                StartDate = StartDate,
                EndDate = EndDate,
                User = string.IsNullOrWhiteSpace(UserFilter) ? null : UserFilter,
                Action = string.IsNullOrWhiteSpace(ActionFilter) ? null : ActionFilter,
                EntityType = string.IsNullOrWhiteSpace(EntityTypeFilter) ? null : EntityTypeFilter,
                SuccessOnly = SuccessOnlyFilter ? true : null,
                Page = CurrentPage,
                PageSize = 100
            };

            var response = await _apiClient.GetAuditLogsAsync(filter);

            if (response.Success && response.Data != null)
            {
                AuditLogs.Clear();
                foreach (var entry in response.Data.Entries)
                {
                    AuditLogs.Add(entry);
                }

                TotalCount = response.Data.TotalCount;
                StatusMessage = $"{response.Data.Entries.Count} Einträge geladen (Gesamt: {TotalCount})";
                ((AsyncRelayCommand)NextPageCommand).NotifyCanExecuteChanged();
                
                // Apply current sort
                if (!string.IsNullOrEmpty(SortColumn))
                {
                    ApplySort(SortColumn);
                }
                
                // Apply search filter
                _logsView?.Refresh();
                UpdateStatusMessage();
            }
            else
            {
                StatusMessage = $"Fehler: {response.Error}";
                MessageBox.Show($"Fehler beim Laden der Audit-Logs:\n{response.Error}", 
                    "Fehler", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
        catch (Exception ex)
        {
            StatusMessage = $"Fehler: {ex.Message}";
            MessageBox.Show($"Unerwarteter Fehler:\n{ex.Message}", 
                "Fehler", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            IsLoading = false;
        }
    }

    private async Task ExportToCsvAsync()
    {
        var dialog = new SaveFileDialog
        {
            Filter = "CSV-Dateien (*.csv)|*.csv",
            FileName = $"audit_log_{DateTime.Now:yyyyMMdd_HHmmss}.csv"
        };

        if (dialog.ShowDialog() != true)
            return;

        IsLoading = true;
        StatusMessage = "Exportiere Daten...";

        try
        {
            var filter = new AuditLogFilter
            {
                StartDate = StartDate,
                EndDate = EndDate,
                User = string.IsNullOrWhiteSpace(UserFilter) ? null : UserFilter,
                Action = string.IsNullOrWhiteSpace(ActionFilter) ? null : ActionFilter,
                EntityType = string.IsNullOrWhiteSpace(EntityTypeFilter) ? null : EntityTypeFilter,
                SuccessOnly = SuccessOnlyFilter ? true : null,
                Page = 1,
                PageSize = 10000
            };

            var response = await _apiClient.ExportAuditLogsToCsvAsync(filter);

            if (response.Success && response.Data != null)
            {
                await File.WriteAllBytesAsync(dialog.FileName, response.Data);
                StatusMessage = $"Export erfolgreich: {dialog.FileName}";
                MessageBox.Show($"Daten erfolgreich exportiert:\n{dialog.FileName}", 
                    "Export erfolgreich", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            else
            {
                StatusMessage = $"Export fehlgeschlagen: {response.Error}";
                MessageBox.Show($"Fehler beim Export:\n{response.Error}", 
                    "Fehler", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
        catch (Exception ex)
        {
            StatusMessage = $"Export-Fehler: {ex.Message}";
            MessageBox.Show($"Fehler beim Export:\n{ex.Message}", 
                "Fehler", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            IsLoading = false;
        }
    }

    private async Task NextPageAsync()
    {
        CurrentPage++;
        await LoadAuditLogsAsync();
    }

    private async Task PreviousPageAsync()
    {
        if (CurrentPage > 1)
        {
            CurrentPage--;
            await LoadAuditLogsAsync();
        }
    }

    private void ClearFilters()
    {
        StartDate = DateTime.Today.AddDays(-7);
        EndDate = DateTime.Today.AddDays(1);
        UserFilter = string.Empty;
        ActionFilter = string.Empty;
        EntityTypeFilter = string.Empty;
        SearchText = string.Empty;
        SuccessOnlyFilter = false;
        CurrentPage = 1;
        SortColumn = "Timestamp";
        SortAscending = false;
        
        _logsView?.SortDescriptions.Clear();
        _logsView?.Refresh();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
            return false;

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }
}
