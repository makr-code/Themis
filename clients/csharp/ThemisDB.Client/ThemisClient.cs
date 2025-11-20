using System.Net.Http.Json;
using System.Text;
using System.Text.Json;

namespace ThemisDB.Client;

/// <summary>
/// Client for interacting with ThemisDB
/// </summary>
public class ThemisClient : IDisposable
{
    private readonly HttpClient _httpClient;
    private readonly List<string> _endpoints;
    private readonly SemaphoreSlim _endpointLock = new(1, 1);
    private int _currentEndpointIndex;
    private bool _disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="ThemisClient"/> class
    /// </summary>
    /// <param name="endpoints">List of ThemisDB server endpoints</param>
    /// <param name="timeout">Optional HTTP timeout</param>
    /// <exception cref="ArgumentException">Thrown when endpoints list is null or empty</exception>
    public ThemisClient(IEnumerable<string> endpoints, TimeSpan? timeout = null)
    {
        if (endpoints == null || !endpoints.Any())
        {
            throw new ArgumentException("At least one endpoint must be provided", nameof(endpoints));
        }

        _endpoints = endpoints.ToList();
        _httpClient = new HttpClient
        {
            Timeout = timeout ?? TimeSpan.FromSeconds(30)
        };
    }

    /// <summary>
    /// Gets the current endpoint being used
    /// </summary>
    public async Task<string> GetCurrentEndpointAsync()
    {
        await _endpointLock.WaitAsync();
        try
        {
            return _endpoints[_currentEndpointIndex];
        }
        finally
        {
            _endpointLock.Release();
        }
    }

    /// <summary>
    /// Rotates to the next available endpoint
    /// </summary>
    private async Task RotateEndpointAsync()
    {
        await _endpointLock.WaitAsync();
        try
        {
            _currentEndpointIndex = (_currentEndpointIndex + 1) % _endpoints.Count;
        }
        finally
        {
            _endpointLock.Release();
        }
    }

    /// <summary>
    /// Gets a document from the database
    /// </summary>
    /// <typeparam name="T">Type of the document</typeparam>
    /// <param name="model">Model name (e.g., "relational", "document", "graph")</param>
    /// <param name="collection">Collection name</param>
    /// <param name="uuid">Document UUID</param>
    /// <param name="cancellationToken">Cancellation token</param>
    /// <returns>The retrieved document</returns>
    public async Task<T?> GetAsync<T>(string model, string collection, string uuid, CancellationToken cancellationToken = default)
    {
        var endpoint = await GetCurrentEndpointAsync();
        var url = $"{endpoint}/api/{model}/{collection}/{uuid}";
        
        var response = await _httpClient.GetAsync(url, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        return await response.Content.ReadFromJsonAsync<T>(cancellationToken: cancellationToken);
    }

    /// <summary>
    /// Puts (creates or updates) a document in the database
    /// </summary>
    /// <typeparam name="T">Type of the document</typeparam>
    /// <param name="model">Model name</param>
    /// <param name="collection">Collection name</param>
    /// <param name="uuid">Document UUID</param>
    /// <param name="data">Document data</param>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task PutAsync<T>(string model, string collection, string uuid, T data, CancellationToken cancellationToken = default)
    {
        var endpoint = await GetCurrentEndpointAsync();
        var url = $"{endpoint}/api/{model}/{collection}/{uuid}";
        
        var response = await _httpClient.PutAsJsonAsync(url, data, cancellationToken);
        response.EnsureSuccessStatusCode();
    }

    /// <summary>
    /// Deletes a document from the database
    /// </summary>
    /// <param name="model">Model name</param>
    /// <param name="collection">Collection name</param>
    /// <param name="uuid">Document UUID</param>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task DeleteAsync(string model, string collection, string uuid, CancellationToken cancellationToken = default)
    {
        var endpoint = await GetCurrentEndpointAsync();
        var url = $"{endpoint}/api/{model}/{collection}/{uuid}";
        
        var response = await _httpClient.DeleteAsync(url, cancellationToken);
        response.EnsureSuccessStatusCode();
    }

    /// <summary>
    /// Executes an AQL query
    /// </summary>
    /// <typeparam name="T">Type of the query results</typeparam>
    /// <param name="aql">AQL query string</param>
    /// <param name="cancellationToken">Cancellation token</param>
    /// <returns>Query results</returns>
    public async Task<List<T>> QueryAsync<T>(string aql, CancellationToken cancellationToken = default)
    {
        var endpoint = await GetCurrentEndpointAsync();
        var url = $"{endpoint}/query";
        
        var content = new StringContent(
            JsonSerializer.Serialize(new { aql }),
            Encoding.UTF8,
            "application/json"
        );
        
        var response = await _httpClient.PostAsync(url, content, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        return await response.Content.ReadFromJsonAsync<List<T>>(cancellationToken: cancellationToken) ?? new List<T>();
    }

    /// <summary>
    /// Begins a new transaction
    /// </summary>
    /// <param name="options">Transaction options</param>
    /// <param name="cancellationToken">Cancellation token</param>
    /// <returns>A new transaction instance</returns>
    public async Task<Transaction> BeginTransactionAsync(TransactionOptions? options = null, CancellationToken cancellationToken = default)
    {
        options ??= new TransactionOptions();
        
        var endpoint = await GetCurrentEndpointAsync();
        var url = $"{endpoint}/transaction/begin";
        
        var content = new StringContent(
            JsonSerializer.Serialize(new 
            { 
                isolation_level = options.IsolationLevel.ToString().ToUpperInvariant(),
                timeout = options.Timeout?.TotalSeconds 
            }),
            Encoding.UTF8,
            "application/json"
        );
        
        var response = await _httpClient.PostAsync(url, content, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        var result = await response.Content.ReadFromJsonAsync<TransactionBeginResponse>(cancellationToken: cancellationToken);
        if (result == null || string.IsNullOrEmpty(result.TransactionId))
        {
            throw new InvalidOperationException("Failed to begin transaction");
        }
        
        return new Transaction(this, result.TransactionId, _httpClient);
    }

    /// <summary>
    /// Disposes the client and releases resources
    /// </summary>
    public void Dispose()
    {
        if (_disposed) return;
        
        _httpClient?.Dispose();
        _endpointLock?.Dispose();
        _disposed = true;
        
        GC.SuppressFinalize(this);
    }

    private class TransactionBeginResponse
    {
        public string TransactionId { get; set; } = string.Empty;
    }
}
