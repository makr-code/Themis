using System;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Themis.AqlQueryBuilder.Infrastructure;

namespace Themis.AqlQueryBuilder.Services;

/// <summary>
/// Implementation of AQL Query Service using HTTP REST API
/// </summary>
public class AqlQueryService : IAqlQueryService
{
    private readonly HttpClient _httpClient;
    private readonly string _serverUrl;

    public AqlQueryService(HttpClient httpClient, string serverUrl)
    {
        _httpClient = httpClient;
        _serverUrl = serverUrl;
    }

    public async Task<Result<string>> ExecuteQueryAsync(string aql, CancellationToken ct = default)
    {
        try
        {
            var requestBody = JsonSerializer.Serialize(new { query = aql });
            var content = new StringContent(requestBody, Encoding.UTF8, "application/json");
            
            var response = await _httpClient.PostAsync($"{_serverUrl}/api/query/aql", content, ct).ConfigureAwait(false);
            
            if (!response.IsSuccessStatusCode)
            {
                var errorContent = await response.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
                return Result.Failure<string>($"Query failed: {response.StatusCode} - {errorContent}");
            }
            
            var result = await response.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
            return Result.Success(result);
        }
        catch (TaskCanceledException)
        {
            return Result.Failure<string>("Query execution cancelled");
        }
        catch (HttpRequestException ex)
        {
            return Result.Failure<string>($"Connection error: {ex.Message}");
        }
        catch (Exception ex)
        {
            return Result.Failure<string>($"Unexpected error: {ex.Message}");
        }
    }

    public async Task<Result> TestConnectionAsync(CancellationToken ct = default)
    {
        try
        {
            var response = await _httpClient.GetAsync($"{_serverUrl}/api/health", ct).ConfigureAwait(false);
            
            if (response.IsSuccessStatusCode)
            {
                return Result.Success();
            }
            
            return Result.Failure($"Connection test failed: {response.StatusCode}");
        }
        catch (TaskCanceledException)
        {
            return Result.Failure("Connection test cancelled");
        }
        catch (HttpRequestException ex)
        {
            return Result.Failure($"Cannot connect to server: {ex.Message}");
        }
        catch (Exception ex)
        {
            return Result.Failure($"Unexpected error: {ex.Message}");
        }
    }
}
