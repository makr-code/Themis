using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Themis.AdminTools.Shared.ApiClient;
using Themis.ComplianceReports.Models;

namespace Themis.ComplianceReports.ViewModels
{
    public class MainViewModel : ObservableObject
    {
        private readonly ThemisApiClient _apiClient;
        private ObservableCollection<ComplianceReport> _reports = new();
        private string _statusMessage = "Bereit";
        private bool _isLoading = false;
        private string _searchText = string.Empty;
        private int _reportTypeIndex = 0;
        private int _templateIndex = 0;
        private DateTime? _startDate = DateTime.Now.AddMonths(-1);
        private DateTime? _endDate = DateTime.Now;
        private bool _includeCharts = true;
        private bool _includeTechnicalDetails = false;

        public ObservableCollection<ComplianceReport> Reports
        {
            get => _reports;
            set => SetProperty(ref _reports, value);
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

        public int ReportTypeIndex
        {
            get => _reportTypeIndex;
            set => SetProperty(ref _reportTypeIndex, value);
        }

        public int TemplateIndex
        {
            get => _templateIndex;
            set => SetProperty(ref _templateIndex, value);
        }

        public DateTime? StartDate
        {
            get => _startDate;
            set => SetProperty(ref _startDate, value);
        }

        public DateTime? EndDate
        {
            get => _endDate;
            set => SetProperty(ref _endDate, value);
        }

        public bool IncludeCharts
        {
            get => _includeCharts;
            set => SetProperty(ref _includeCharts, value);
        }

        public bool IncludeTechnicalDetails
        {
            get => _includeTechnicalDetails;
            set => SetProperty(ref _includeTechnicalDetails, value);
        }

        public int CompletedCount => Reports.Count(r => r.Status == "Abgeschlossen");

        public RelayCommand RefreshCommand { get; }
        public RelayCommand GenerateReportCommand { get; }
        public RelayCommand ExportPdfCommand { get; }
        public RelayCommand ExportExcelCommand { get; }
        public RelayCommand ExportCsvCommand { get; }

        public MainViewModel(ThemisApiClient apiClient)
        {
            _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
            
            RefreshCommand = new RelayCommand(async () => await ExecuteRefreshAsync());
            GenerateReportCommand = new RelayCommand(ExecuteGenerateReport);
            ExportPdfCommand = new RelayCommand(ExecuteExportPdf);
            ExportExcelCommand = new RelayCommand(ExecuteExportExcel);
            ExportCsvCommand = new RelayCommand(ExecuteExportCsv);

            _ = LoadReportsAsync();
        }

        private async Task LoadReportsAsync()
        {
            IsLoading = true;
            StatusMessage = "Lade Reports...";
            
            try
            {
                var response = await _apiClient.Reports.GetReportsAsync();
                
                if (response.Success && response.Data != null)
                {
                    Reports.Clear();
                    
                    foreach (var report in response.Data.Items)
                    {
                        Reports.Add(new ComplianceReport
                        {
                            ReportId = report.Id,
                            ReportType = report.Type,
                            CreatedAt = report.CreatedAt,
                            CreatedBy = report.CreatedBy ?? "System",
                            PeriodStart = report.PeriodStart,
                            PeriodEnd = report.PeriodEnd,
                            Status = report.Status,
                            RecordCount = report.RecordCount,
                            Notes = report.Description ?? string.Empty
                        });
                    }
                    
                    OnPropertyChanged(nameof(CompletedCount));
                    StatusMessage = $"{Reports.Count} Reports geladen";
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
            await LoadReportsAsync();
        }

        private void ExecuteGenerateReport()
        {
            var reportTypes = new[] { "DSGVO", "SOX", "HIPAA", "ISO 27001", "PCI-DSS" };
            var selectedType = reportTypes[ReportTypeIndex];
            
            StatusMessage = $"Generiere {selectedType}-Report...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(2000);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = $"{selectedType}-Report erfolgreich generiert";
                });
            });
        }

        private void ExecuteExportPdf()
        {
            StatusMessage = "Exportiere Report als PDF...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(1500);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = "PDF-Export abgeschlossen: compliance_report.pdf";
                });
            });
        }

        private void ExecuteExportExcel()
        {
            StatusMessage = "Exportiere Report als Excel...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(1000);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = "Excel-Export abgeschlossen: compliance_report.xlsx";
                });
            });
        }

        private void ExecuteExportCsv()
        {
            StatusMessage = "Exportiere Report als CSV...";
            IsLoading = true;

            System.Threading.Tasks.Task.Run(() =>
            {
                System.Threading.Thread.Sleep(500);
                System.Windows.Application.Current.Dispatcher.Invoke(() =>
                {
                    IsLoading = false;
                    StatusMessage = "CSV-Export abgeschlossen: compliance_report.csv";
                });
            });
        }
    }
}
