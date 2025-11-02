using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class KeysEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public KeysEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    public async Task<ApiResponse<KeyListResponse>> GetKeysAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.GetAsync("/api/keys", cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<KeyListResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<KeyListResponse> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<KeyListResponse> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<KeyListResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<RotationResult>> RotateKeyAsync(string keyId, CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.PostAsync($"/api/keys/{Uri.EscapeDataString(keyId)}/rotate", null, cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<RotationResult>(_jsonOptions, cancellationToken);
                return new ApiResponse<RotationResult> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<RotationResult> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<RotationResult> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }
}
