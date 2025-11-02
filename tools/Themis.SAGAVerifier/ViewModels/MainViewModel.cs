using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Data;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;

namespace Themis.SAGAVerifier.ViewModels;

public partial class MainViewModel : ObservableObject
{
    private readonly ThemisApiClient _apiClient;
    
    private ICollectionView? _batchesView;
    private ICollectionView? _stepsView;

    [ObservableProperty]
    private ObservableCollection<SAGABatchInfo> _batches = new();

    [ObservableProperty]
    private SAGABatchInfo? _selectedBatch;

    [ObservableProperty]
    private SAGABatchDetail? _batchDetail;

    [ObservableProperty]
    private SAGAVerificationResult? _verificationResult;

    [ObservableProperty]
    private ObservableCollection<SAGAStep> _steps = new();

    [ObservableProperty]
    private bool _isLoading;

    [ObservableProperty]
    private string? _errorMessage;

    [ObservableProperty]
    private string _statusMessage = "Ready";
    
    [ObservableProperty]
    private string _batchSearchText = string.Empty;
    
    [ObservableProperty]
    private string _stepSearchText = string.Empty;
    
    [ObservableProperty]
    private string _batchSortColumn = "Timestamp";
    
    [ObservableProperty]
    private bool _batchSortAscending = false;
    
    [ObservableProperty]
    private string _stepSortColumn = "Timestamp";
    
    [ObservableProperty]
    private bool _stepSortAscending = true;

    public MainViewModel(ThemisApiClient apiClient)
    {
        _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
    }
    
    partial void OnBatchesChanged(ObservableCollection<SAGABatchInfo> value)
    {
        _batchesView = CollectionViewSource.GetDefaultView(value);
        _batchesView.Filter = FilterBatches;
    }
    
    partial void OnStepsChanged(ObservableCollection<SAGAStep> value)
    {
        _stepsView = CollectionViewSource.GetDefaultView(value);
        _stepsView.Filter = FilterSteps;
    }
    
    partial void OnBatchSearchTextChanged(string value)
    {
        _batchesView?.Refresh();
        UpdateBatchStatusMessage();
    }
    
    partial void OnStepSearchTextChanged(string value)
    {
        _stepsView?.Refresh();
        UpdateStepStatusMessage();
    }

    [RelayCommand]
    private async Task LoadBatchesAsync()
    {
        try
        {
            IsLoading = true;
            ErrorMessage = null;
            StatusMessage = "Loading SAGA batches...";

            var response = await _apiClient.GetSAGABatchesAsync();

            if (response.Success && response.Data != null)
            {
                Batches = new ObservableCollection<SAGABatchInfo>(response.Data.Batches);
                StatusMessage = $"Loaded {Batches.Count} batch(es)";
                UpdateBatchStatusMessage();
            }
            else
            {
                ErrorMessage = $"Failed to load batches: {response.Error}";
                StatusMessage = "Error loading batches";
            }
        }
        catch (Exception ex)
        {
            ErrorMessage = $"Error: {ex.Message}";
            StatusMessage = "Error";
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task LoadBatchDetailAsync()
    {
        if (SelectedBatch == null) return;

        try
        {
            IsLoading = true;
            ErrorMessage = null;
            StatusMessage = $"Loading batch detail for {SelectedBatch.BatchIdShort}...";

            var response = await _apiClient.GetSAGABatchDetailAsync(SelectedBatch.BatchId);

            if (response.Success && response.Data != null)
            {
                BatchDetail = response.Data;
                Steps = new ObservableCollection<SAGAStep>(response.Data.Steps);
                StatusMessage = $"Loaded {Steps.Count} SAGA step(s)";
                UpdateStepStatusMessage();
            }
            else
            {
                ErrorMessage = $"Failed to load batch detail: {response.Error}";
                StatusMessage = "Error loading detail";
                BatchDetail = null;
                Steps.Clear();
            }
        }
        catch (Exception ex)
        {
            ErrorMessage = $"Error: {ex.Message}";
            StatusMessage = "Error";
            BatchDetail = null;
            Steps.Clear();
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task VerifyBatchAsync()
    {
        if (SelectedBatch == null) return;

        try
        {
            IsLoading = true;
            ErrorMessage = null;
            StatusMessage = $"Verifying batch {SelectedBatch.BatchIdShort}...";

            var response = await _apiClient.VerifySAGABatchAsync(SelectedBatch.BatchId);

            if (response.Success && response.Data != null)
            {
                VerificationResult = response.Data;
                StatusMessage = response.Data.Verified 
                    ? "✓ Batch verified successfully" 
                    : "✗ Batch verification failed";

                var resultMessage = response.Data.Verified
                    ? "Batch signature and hash verified successfully."
                    : $"Verification failed: {response.Data.DetailedStatus}";

                MessageBox.Show(resultMessage, 
                    "Verification Result", 
                    MessageBoxButton.OK,
                    response.Data.Verified ? MessageBoxImage.Information : MessageBoxImage.Warning);
            }
            else
            {
                ErrorMessage = $"Failed to verify batch: {response.Error}";
                StatusMessage = "Error verifying batch";
                VerificationResult = null;
            }
        }
        catch (Exception ex)
        {
            ErrorMessage = $"Error: {ex.Message}";
            StatusMessage = "Error";
            VerificationResult = null;
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task FlushBatchAsync()
    {
        try
        {
            IsLoading = true;
            ErrorMessage = null;
            StatusMessage = "Flushing current SAGA batch...";

            var response = await _apiClient.FlushCurrentSAGABatchAsync();

            if (response.Success && response.Data != null)
            {
                MessageBox.Show(response.Data.Message, "Flush Result", MessageBoxButton.OK, MessageBoxImage.Information);
                StatusMessage = "Batch flushed successfully";
                
                // Reload batches
                await LoadBatchesAsync();
            }
            else
            {
                ErrorMessage = $"Failed to flush batch: {response.Error}";
                StatusMessage = "Error flushing batch";
            }
        }
        catch (Exception ex)
        {
            ErrorMessage = $"Error: {ex.Message}";
            StatusMessage = "Error";
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private void ExportSteps()
    {
        if (Steps.Count == 0)
        {
            MessageBox.Show("No SAGA steps to export.", "Export", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        try
        {
            var dialog = new Microsoft.Win32.SaveFileDialog
            {
                FileName = $"saga_steps_{SelectedBatch?.BatchIdShort}_{DateTime.Now:yyyyMMdd_HHmmss}.csv",
                DefaultExt = ".csv",
                Filter = "CSV Files (*.csv)|*.csv|All Files (*.*)|*.*"
            };

            if (dialog.ShowDialog() == true)
            {
                var csv = "Timestamp,SAGA ID,Step Name,Status,Correlation ID,Metadata\n";
                foreach (var step in Steps)
                {
                    csv += $"\"{step.Timestamp}\",\"{step.SagaId}\",\"{step.StepName}\",\"{step.Status}\",\"{step.CorrelationId}\",\"{step.Metadata}\"\n";
                }

                File.WriteAllText(dialog.FileName, csv);
                MessageBox.Show($"Exported {Steps.Count} SAGA steps to:\n{dialog.FileName}", 
                    "Export Successful", MessageBoxButton.OK, MessageBoxImage.Information);
                StatusMessage = $"Exported {Steps.Count} steps";
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Export failed: {ex.Message}", "Export Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    partial void OnSelectedBatchChanged(SAGABatchInfo? value)
    {
        if (value != null)
        {
            // Auto-load detail when batch is selected
            _ = LoadBatchDetailAsync();
        }
        else
        {
            BatchDetail = null;
            Steps.Clear();
            VerificationResult = null;
        }
    }
    
    // Filter methods
    private bool FilterBatches(object obj)
    {
        if (obj is not SAGABatchInfo batch)
            return false;

        if (string.IsNullOrWhiteSpace(BatchSearchText))
            return true;

        var search = BatchSearchText.ToLowerInvariant();
        return batch.BatchId.ToLowerInvariant().Contains(search) ||
               batch.Hash?.ToLowerInvariant().Contains(search) == true ||
               batch.Signature?.ToLowerInvariant().Contains(search) == true ||
               batch.TimestampFormatted.ToLowerInvariant().Contains(search);
    }
    
    private bool FilterSteps(object obj)
    {
        if (obj is not SAGAStep step)
            return false;

        if (string.IsNullOrWhiteSpace(StepSearchText))
            return true;

        var search = StepSearchText.ToLowerInvariant();
        return step.SagaId?.ToLowerInvariant().Contains(search) == true ||
               step.StepName?.ToLowerInvariant().Contains(search) == true ||
               step.Status?.ToLowerInvariant().Contains(search) == true ||
               step.CorrelationId?.ToLowerInvariant().Contains(search) == true ||
               step.Metadata?.ToLowerInvariant().Contains(search) == true;
    }
    
    // Sort methods
    [RelayCommand]
    private void SortBatches(string? columnName)
    {
        if (string.IsNullOrEmpty(columnName))
            return;

        if (BatchSortColumn == columnName)
        {
            BatchSortAscending = !BatchSortAscending;
        }
        else
        {
            BatchSortColumn = columnName;
            BatchSortAscending = true;
        }

        _batchesView?.SortDescriptions.Clear();
        _batchesView?.SortDescriptions.Add(new SortDescription(BatchSortColumn,
            BatchSortAscending ? ListSortDirection.Ascending : ListSortDirection.Descending));
        
        UpdateBatchStatusMessage();
    }
    
    [RelayCommand]
    private void SortSteps(string? columnName)
    {
        if (string.IsNullOrEmpty(columnName))
            return;

        if (StepSortColumn == columnName)
        {
            StepSortAscending = !StepSortAscending;
        }
        else
        {
            StepSortColumn = columnName;
            StepSortAscending = true;
        }

        _stepsView?.SortDescriptions.Clear();
        _stepsView?.SortDescriptions.Add(new SortDescription(StepSortColumn,
            StepSortAscending ? ListSortDirection.Ascending : ListSortDirection.Descending));
        
        UpdateStepStatusMessage();
    }
    
    // Status message updates
    private void UpdateBatchStatusMessage()
    {
        if (_batchesView == null) return;
        
        var filtered = Batches.Count(b => _batchesView.Filter == null || _batchesView.Filter(b));
        var total = Batches.Count;
        
        if (filtered != total)
        {
            StatusMessage = $"{filtered} of {total} batches shown";
        }
        else if (total > 0)
        {
            StatusMessage = $"{total} batch(es) loaded";
        }
    }
    
    private void UpdateStepStatusMessage()
    {
        if (_stepsView == null) return;
        
        var filtered = Steps.Count(s => _stepsView.Filter == null || _stepsView.Filter(s));
        var total = Steps.Count;
        
        if (filtered != total)
        {
            StatusMessage = $"{filtered} of {total} steps shown";
        }
        else if (total > 0)
        {
            StatusMessage = $"{total} SAGA step(s) loaded";
        }
    }
}
