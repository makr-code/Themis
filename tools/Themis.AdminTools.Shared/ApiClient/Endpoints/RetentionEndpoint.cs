using System.Net.Http;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class RetentionEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public RetentionEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    // Backward-compatible default call
    public Task<ApiResponse<RetentionPolicyListResponse>> GetPoliciesAsync(CancellationToken cancellationToken = default)
        => GetPoliciesAsync(null, null, 1, 100, cancellationToken);

    // New overload with filters & pagination
    public async Task<ApiResponse<RetentionPolicyListResponse>> GetPoliciesAsync(
        string? name,
        string? classification,
        int page,
        int pageSize,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var qs = BuildQueryString(name, classification, page, pageSize);
            var resp = await _httpClient.GetAsync($"/api/retention/policies{qs}", cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<RetentionPolicyListResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<RetentionPolicyListResponse> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<RetentionPolicyListResponse> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<RetentionPolicyListResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    private static string BuildQueryString(string? name, string? classification, int page, int pageSize)
    {
        var sb = new StringBuilder();
        void add(string key, string? value)
        {
            if (!string.IsNullOrWhiteSpace(value))
            {
                sb.Append(sb.Length == 0 ? '?' : '&');
                sb.Append(Uri.EscapeDataString(key));
                sb.Append('=');
                sb.Append(Uri.EscapeDataString(value));
            }
        }
        add("name", name);
        add("classification", classification);
        if (page > 1)
        {
            add("page", page.ToString());
        }
        if (pageSize > 0 && pageSize != 100)
        {
            add("pageSize", pageSize.ToString());
        }
        return sb.ToString();
    }

    public async Task<ApiResponse<RetentionPolicy>> CreateOrUpdatePolicyAsync(
        RetentionPolicy policy,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.PostAsJsonAsync("/api/retention/policies", policy, _jsonOptions, cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<RetentionPolicy>(_jsonOptions, cancellationToken);
                return new ApiResponse<RetentionPolicy> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<RetentionPolicy> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<RetentionPolicy> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<bool>> DeletePolicyAsync(string name, CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.DeleteAsync($"/api/retention/policies/{Uri.EscapeDataString(name)}", cancellationToken);
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

    public async Task<ApiResponse<string>> GetHistoryRawAsync(int? limit = null, CancellationToken cancellationToken = default)
    {
        try
        {
            var qs = limit.HasValue && limit.Value > 0 ? $"?limit={limit.Value}" : string.Empty;
            var resp = await _httpClient.GetAsync($"/api/retention/history{qs}", cancellationToken);
            var text = await resp.Content.ReadAsStringAsync(cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                return new ApiResponse<string> { Success = true, Data = text, StatusCode = (int)resp.StatusCode };
            }
            return new ApiResponse<string> { Success = false, Error = text, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<string> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<string>> GetPolicyStatsRawAsync(string name, CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.GetAsync($"/api/retention/policies/{Uri.EscapeDataString(name)}/stats", cancellationToken);
            var text = await resp.Content.ReadAsStringAsync(cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                return new ApiResponse<string> { Success = true, Data = text, StatusCode = (int)resp.StatusCode };
            }
            return new ApiResponse<string> { Success = false, Error = text, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<string> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }
}
