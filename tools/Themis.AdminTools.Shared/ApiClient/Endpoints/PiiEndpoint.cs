using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class PiiEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public PiiEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    public async Task<ApiResponse<PiiListResponse>> GetPiiMappingsAsync(
        string? originalUuid = null,
        string? pseudonym = null,
        bool? activeOnly = null,
        int page = 1,
        int pageSize = 100,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var qp = new List<string>();
            if (!string.IsNullOrWhiteSpace(originalUuid)) qp.Add($"original_uuid={Uri.EscapeDataString(originalUuid)}");
            if (!string.IsNullOrWhiteSpace(pseudonym)) qp.Add($"pseudonym={Uri.EscapeDataString(pseudonym)}");
            if (activeOnly.HasValue) qp.Add($"active_only={(activeOnly.Value ? "true" : "false")}");
            qp.Add($"page={page}");
            qp.Add($"page_size={pageSize}");
            var url = "/api/pii/mappings" + (qp.Count > 0 ? "?" + string.Join("&", qp) : string.Empty);

            var resp = await _httpClient.GetAsync(url, cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<PiiListResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<PiiListResponse> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<PiiListResponse> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<PiiListResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<byte[]>> ExportPiiCsvAsync(
        string? originalUuid = null,
        string? pseudonym = null,
        bool? activeOnly = null,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var qp = new List<string>();
            if (!string.IsNullOrWhiteSpace(originalUuid)) qp.Add($"original_uuid={Uri.EscapeDataString(originalUuid)}");
            if (!string.IsNullOrWhiteSpace(pseudonym)) qp.Add($"pseudonym={Uri.EscapeDataString(pseudonym)}");
            if (activeOnly.HasValue) qp.Add($"active_only={(activeOnly.Value ? "true" : "false")}");
            var url = "/api/pii/export/csv" + (qp.Count > 0 ? "?" + string.Join("&", qp) : string.Empty);
            var resp = await _httpClient.GetAsync(url, cancellationToken);
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

    public async Task<ApiResponse<bool>> DeletePiiByUuidAsync(
        string uuid,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.PostAsync($"/api/pii/delete/{Uri.EscapeDataString(uuid)}", null, cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                return new ApiResponse<bool> { Success = true, Data = true, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<bool> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<bool> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }
}
