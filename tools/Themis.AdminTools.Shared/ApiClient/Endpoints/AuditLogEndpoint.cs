using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class AuditLogEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public AuditLogEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    public async Task<ApiResponse<AuditLogResponse>> GetAuditLogsAsync(
        AuditLogFilter filter,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var queryParams = BuildQueryString(filter);
            var response = await _httpClient.GetAsync($"/api/audit{queryParams}", cancellationToken);

            if (response.IsSuccessStatusCode)
            {
                var data = await response.Content.ReadFromJsonAsync<AuditLogResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<AuditLogResponse> { Success = true, Data = data, StatusCode = (int)response.StatusCode };
            }

            var error = await response.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<AuditLogResponse> { Success = false, Error = error, StatusCode = (int)response.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<AuditLogResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<byte[]>> ExportAuditLogsToCsvAsync(
        AuditLogFilter filter,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var queryParams = BuildQueryString(filter);
            var response = await _httpClient.GetAsync($"/api/audit/export/csv{queryParams}", cancellationToken);

            if (response.IsSuccessStatusCode)
            {
                var data = await response.Content.ReadAsByteArrayAsync(cancellationToken);
                return new ApiResponse<byte[]> { Success = true, Data = data, StatusCode = (int)response.StatusCode };
            }

            var error = await response.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<byte[]> { Success = false, Error = error, StatusCode = (int)response.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<byte[]> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    private static string BuildQueryString(AuditLogFilter filter)
    {
        var parameters = new List<string>();

        if (filter.StartDate.HasValue)
            parameters.Add($"start={filter.StartDate.Value:yyyy-MM-ddTHH:mm:ss}");

        if (filter.EndDate.HasValue)
            parameters.Add($"end={filter.EndDate.Value:yyyy-MM-ddTHH:mm:ss}");

        if (!string.IsNullOrEmpty(filter.User))
            parameters.Add($"user={Uri.EscapeDataString(filter.User)}");

        if (!string.IsNullOrEmpty(filter.Action))
            parameters.Add($"action={Uri.EscapeDataString(filter.Action)}");

        if (!string.IsNullOrEmpty(filter.EntityType))
            parameters.Add($"entity_type={Uri.EscapeDataString(filter.EntityType)}");

        if (!string.IsNullOrEmpty(filter.EntityId))
            parameters.Add($"entity_id={Uri.EscapeDataString(filter.EntityId)}");

        if (filter.SuccessOnly.HasValue)
            parameters.Add($"success={filter.SuccessOnly.Value.ToString().ToLower()}");

        parameters.Add($"page={filter.Page}");
        parameters.Add($"page_size={filter.PageSize}");

        return parameters.Count > 0 ? "?" + string.Join("&", parameters) : string.Empty;
    }
}
