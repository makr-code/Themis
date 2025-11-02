using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient.Endpoints;

public class SagaEndpoint
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions;

    public SagaEndpoint(HttpClient httpClient, JsonSerializerOptions jsonOptions)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _jsonOptions = jsonOptions ?? throw new ArgumentNullException(nameof(jsonOptions));
    }

    public async Task<ApiResponse<SAGABatchListResponse>> GetBatchesAsync(
        CancellationToken cancellationToken = default)
    {
        try
        {
            var response = await _httpClient.GetAsync("/api/saga/batches", cancellationToken);
            if (response.IsSuccessStatusCode)
            {
                var data = await response.Content.ReadFromJsonAsync<SAGABatchListResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<SAGABatchListResponse> { Success = true, Data = data, StatusCode = (int)response.StatusCode };
            }
            var error = await response.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<SAGABatchListResponse> { Success = false, Error = error, StatusCode = (int)response.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<SAGABatchListResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<SAGABatchDetail>> GetBatchAsync(
        string batchId,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var response = await _httpClient.GetAsync($"/api/saga/batch/{batchId}", cancellationToken);
            if (response.IsSuccessStatusCode)
            {
                var data = await response.Content.ReadFromJsonAsync<SAGABatchDetail>(_jsonOptions, cancellationToken);
                return new ApiResponse<SAGABatchDetail> { Success = true, Data = data, StatusCode = (int)response.StatusCode };
            }
            var error = await response.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<SAGABatchDetail> { Success = false, Error = error, StatusCode = (int)response.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<SAGABatchDetail> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<SAGAVerificationResult>> VerifyBatchAsync(
        string batchId,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var response = await _httpClient.PostAsync($"/api/saga/batch/{batchId}/verify", null, cancellationToken);
            if (response.IsSuccessStatusCode)
            {
                var data = await response.Content.ReadFromJsonAsync<SAGAVerificationResult>(_jsonOptions, cancellationToken);
                return new ApiResponse<SAGAVerificationResult> { Success = true, Data = data, StatusCode = (int)response.StatusCode };
            }
            var error = await response.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<SAGAVerificationResult> { Success = false, Error = error, StatusCode = (int)response.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<SAGAVerificationResult> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }

    public async Task<ApiResponse<SAGAFlushResponse>> FlushCurrentBatchAsync(
        CancellationToken cancellationToken = default)
    {
        try
        {
            var response = await _httpClient.PostAsync("/api/saga/flush", null, cancellationToken);
            if (response.IsSuccessStatusCode)
            {
                var data = await response.Content.ReadFromJsonAsync<SAGAFlushResponse>(_jsonOptions, cancellationToken);
                return new ApiResponse<SAGAFlushResponse> { Success = true, Data = data, StatusCode = (int)response.StatusCode };
            }
            var error = await response.Content.ReadAsStringAsync(cancellationToken);
            return new ApiResponse<SAGAFlushResponse> { Success = false, Error = error, StatusCode = (int)response.StatusCode };
        }
        catch (Exception ex)
        {
            return new ApiResponse<SAGAFlushResponse> { Success = false, Error = ex.Message, StatusCode = 0 };
        }
    }
}
