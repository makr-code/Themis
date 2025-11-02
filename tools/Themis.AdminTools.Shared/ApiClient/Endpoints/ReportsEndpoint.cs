using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class ReportsEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public ReportsEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    public async Task<ApiResponse<ComplianceReportListResponse>> GetReportsAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.GetAsync("/api/reports", cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<ComplianceReportListResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<ComplianceReportListResponse> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<ComplianceReportListResponse> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<ComplianceReportListResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<byte[]>> ExportReportAsync(string reportId, string format = "pdf", CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.GetAsync($"/api/reports/{Uri.EscapeDataString(reportId)}/export?format={Uri.EscapeDataString(format)}", cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadAsByteArrayAsync(cancellationToken);
                return new ApiResponse<byte[]> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<byte[]> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<byte[]> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }
}
