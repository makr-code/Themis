using System.Net.Http.Json;
using System.Text;
using System.Text.Json;

namespace ThemisDB.Client;

/// <summary>
/// Represents a database transaction
/// </summary>
public class Transaction : IAsyncDisposable
{
    private readonly ThemisClient _client;
    private readonly HttpClient _httpClient;
    private readonly string _transactionId;
    private readonly SemaphoreSlim _stateLock = new(1, 1);
    private bool _isActive = true;
    private bool _disposed;

    internal Transaction(ThemisClient client, string transactionId, HttpClient httpClient)
    {
        _client = client ?? throw new ArgumentNullException(nameof(client));
        _transactionId = transactionId ?? throw new ArgumentNullException(nameof(transactionId));
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
    }

    /// <summary>
    /// Gets the transaction ID
    /// </summary>
    public string TransactionId => _transactionId;

    /// <summary>
    /// Gets whether the transaction is still active
    /// </summary>
    public async Task<bool> IsActiveAsync()
    {
        await _stateLock.WaitAsync();
        try
        {
            return _isActive;
        }
        finally
        {
            _stateLock.Release();
        }
    }

    /// <summary>
    /// Gets a document within the transaction
    /// </summary>
    /// <typeparam name="T">Type of the document</typeparam>
    /// <param name="model">Model name</param>
    /// <param name="collection">Collection name</param>
    /// <param name="uuid">Document UUID</param>
    /// <param name="cancellationToken">Cancellation token</param>
    /// <returns>The retrieved document</returns>
    public async Task<T?> GetAsync<T>(string model, string collection, string uuid, CancellationToken cancellationToken = default)
    {
        await EnsureActiveAsync();
        
        var endpoint = await _client.GetCurrentEndpointAsync();
        var url = $"{endpoint}/api/{model}/{collection}/{uuid}";
        
        var request = new HttpRequestMessage(HttpMethod.Get, url);
        request.Headers.Add("X-Transaction-Id", _transactionId);
        
        var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        return await response.Content.ReadFromJsonAsync<T>(cancellationToken: cancellationToken);
    }

    /// <summary>
    /// Puts (creates or updates) a document within the transaction
    /// </summary>
    /// <typeparam name="T">Type of the document</typeparam>
    /// <param name="model">Model name</param>
    /// <param name="collection">Collection name</param>
    /// <param name="uuid">Document UUID</param>
    /// <param name="data">Document data</param>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task PutAsync<T>(string model, string collection, string uuid, T data, CancellationToken cancellationToken = default)
    {
        await EnsureActiveAsync();
        
        var endpoint = await _client.GetCurrentEndpointAsync();
        var url = $"{endpoint}/api/{model}/{collection}/{uuid}";
        
        var request = new HttpRequestMessage(HttpMethod.Put, url);
        request.Headers.Add("X-Transaction-Id", _transactionId);
        request.Content = JsonContent.Create(data);
        
        var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
    }

    /// <summary>
    /// Deletes a document within the transaction
    /// </summary>
    /// <param name="model">Model name</param>
    /// <param name="collection">Collection name</param>
    /// <param name="uuid">Document UUID</param>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task DeleteAsync(string model, string collection, string uuid, CancellationToken cancellationToken = default)
    {
        await EnsureActiveAsync();
        
        var endpoint = await _client.GetCurrentEndpointAsync();
        var url = $"{endpoint}/api/{model}/{collection}/{uuid}";
        
        var request = new HttpRequestMessage(HttpMethod.Delete, url);
        request.Headers.Add("X-Transaction-Id", _transactionId);
        
        var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
    }

    /// <summary>
    /// Executes an AQL query within the transaction
    /// </summary>
    /// <typeparam name="T">Type of the query results</typeparam>
    /// <param name="aql">AQL query string</param>
    /// <param name="cancellationToken">Cancellation token</param>
    /// <returns>Query results</returns>
    public async Task<List<T>> QueryAsync<T>(string aql, CancellationToken cancellationToken = default)
    {
        await EnsureActiveAsync();
        
        var endpoint = await _client.GetCurrentEndpointAsync();
        var url = $"{endpoint}/query";
        
        var request = new HttpRequestMessage(HttpMethod.Post, url);
        request.Headers.Add("X-Transaction-Id", _transactionId);
        request.Content = new StringContent(
            JsonSerializer.Serialize(new { aql }),
            Encoding.UTF8,
            "application/json"
        );
        
        var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        return await response.Content.ReadFromJsonAsync<List<T>>(cancellationToken: cancellationToken) ?? new List<T>();
    }

    /// <summary>
    /// Commits the transaction
    /// </summary>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task CommitAsync(CancellationToken cancellationToken = default)
    {
        await EnsureActiveAsync();
        
        var endpoint = await _client.GetCurrentEndpointAsync();
        var url = $"{endpoint}/transaction/commit";
        
        var content = new StringContent(
            JsonSerializer.Serialize(new { transaction_id = _transactionId }),
            Encoding.UTF8,
            "application/json"
        );
        
        var response = await _httpClient.PostAsync(url, content, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        await _stateLock.WaitAsync(cancellationToken);
        try
        {
            _isActive = false;
        }
        finally
        {
            _stateLock.Release();
        }
    }

    /// <summary>
    /// Rolls back the transaction
    /// </summary>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task RollbackAsync(CancellationToken cancellationToken = default)
    {
        await _stateLock.WaitAsync(cancellationToken);
        try
        {
            if (!_isActive) return;
        }
        finally
        {
            _stateLock.Release();
        }
        
        var endpoint = await _client.GetCurrentEndpointAsync();
        var url = $"{endpoint}/transaction/rollback";
        
        var content = new StringContent(
            JsonSerializer.Serialize(new { transaction_id = _transactionId }),
            Encoding.UTF8,
            "application/json"
        );
        
        var response = await _httpClient.PostAsync(url, content, cancellationToken);
        response.EnsureSuccessStatusCode();
        
        await _stateLock.WaitAsync(cancellationToken);
        try
        {
            _isActive = false;
        }
        finally
        {
            _stateLock.Release();
        }
    }

    private async Task EnsureActiveAsync()
    {
        if (!await IsActiveAsync())
        {
            throw new InvalidOperationException("Transaction is not active");
        }
    }

    /// <summary>
    /// Disposes the transaction, rolling back if still active
    /// </summary>
    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        
        if (await IsActiveAsync())
        {
            try
            {
                await RollbackAsync();
            }
            catch
            {
                // Ignore errors during cleanup
            }
        }
        
        _stateLock?.Dispose();
        _disposed = true;
        
        GC.SuppressFinalize(this);
    }
}
