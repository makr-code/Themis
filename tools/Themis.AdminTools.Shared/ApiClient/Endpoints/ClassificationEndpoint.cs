using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class ClassificationEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public ClassificationEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    public async Task<ApiResponse<ClassificationStatsResponse>> GetStatsAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var resp = await _httpClient.GetAsync("/api/classification/stats", cancellationToken);
            if (resp.IsSuccessStatusCode)
            {
                var data = await resp.Content.ReadFromJsonAsync<ClassificationStatsResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<ClassificationStatsResponse> { Success = true, Data = data, StatusCode = (int)resp.StatusCode };
            }
            var err = await resp.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<ClassificationStatsResponse> { Success = false, Error = err, StatusCode = (int)resp.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<ClassificationStatsResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }
}
