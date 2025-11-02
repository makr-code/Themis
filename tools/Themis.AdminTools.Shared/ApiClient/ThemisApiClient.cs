using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using Themis.AdminTools.Shared.ApiClient.Endpoints;
using Themis.AdminTools.Shared.Models;

namespace Themis.AdminTools.Shared.ApiClient;

public class ThemisApiClient : IDisposable
{
    private readonly HttpClient _httpClient;
    private readonly ThemisServerConfig _config;
    private readonly JsonSerializerOptions _jsonOptions;
    
    // New endpoint-based composition (maintains BC via delegating methods below)
    public AuditLogEndpoint Audit { get; }
    public PiiEndpoint Pii { get; }
    public SagaEndpoint Saga { get; }
    public RetentionEndpoint Retention { get; }
    public ClassificationEndpoint Classification { get; }
    public KeysEndpoint Keys { get; }
    public ReportsEndpoint Reports { get; }

    public ThemisApiClient(HttpClient httpClient, ThemisServerConfig config)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
        _config = config ?? throw new ArgumentNullException(nameof(config));

        _httpClient.BaseAddress = new Uri(_config.BaseUrl);
        _httpClient.Timeout = TimeSpan.FromSeconds(_config.Timeout);

        // Security: API keys must be provided via configuration/secret store at runtime.
        // Never hardcode keys in source code or commit them to the repository.
        if (!string.IsNullOrEmpty(_config.ApiKey))
        {
            _httpClient.DefaultRequestHeaders.Add("X-API-Key", _config.ApiKey);
        }

        // Optional JWT Bearer support
        if (!string.IsNullOrEmpty(_config.JwtToken))
        {
            _httpClient.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", _config.JwtToken);
        }

        _jsonOptions = new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        };

        // Initialize endpoints
    Audit = new AuditLogEndpoint(_httpClient, _jsonOptions);
    Pii = new PiiEndpoint(_httpClient, _jsonOptions);
    Saga = new SagaEndpoint(_httpClient, _jsonOptions);
    Retention = new RetentionEndpoint(_httpClient, _jsonOptions);
    Classification = new ClassificationEndpoint(_httpClient, _jsonOptions);
    Keys = new KeysEndpoint(_httpClient, _jsonOptions);
    Reports = new ReportsEndpoint(_httpClient, _jsonOptions);
    }

    public async Task<ApiResponse<AuditLogResponse>> GetAuditLogsAsync(
        AuditLogFilter filter, 
        CancellationToken cancellationToken = default)
    {
        return await Audit.GetAuditLogsAsync(filter, cancellationToken);
    }

    public async Task<ApiResponse<byte[]>> ExportAuditLogsToCsvAsync(
        AuditLogFilter filter,
        CancellationToken cancellationToken = default)
    {
        return await Audit.ExportAuditLogsToCsvAsync(filter, cancellationToken);
    }

    // PII API Methods
    public async Task<ApiResponse<PiiListResponse>> GetPiiMappingsAsync(
        string? originalUuid = null,
        string? pseudonym = null,
        bool? activeOnly = null,
        int page = 1,
        int pageSize = 100,
        CancellationToken cancellationToken = default)
    {
        return await Pii.GetPiiMappingsAsync(originalUuid, pseudonym, activeOnly, page, pageSize, cancellationToken);
    }

    public async Task<ApiResponse<byte[]>> ExportPiiCsvAsync(
        string? originalUuid = null,
        string? pseudonym = null,
        bool? activeOnly = null,
        CancellationToken cancellationToken = default)
    {
        return await Pii.ExportPiiCsvAsync(originalUuid, pseudonym, activeOnly, cancellationToken);
    }

    public async Task<ApiResponse<bool>> DeletePiiByUuidAsync(
        string uuid,
        CancellationToken cancellationToken = default)
    {
        return await Pii.DeletePiiByUuidAsync(uuid, cancellationToken);
    }

    // SAGA API Methods

    public async Task<ApiResponse<SAGABatchListResponse>> GetSAGABatchesAsync(
        CancellationToken cancellationToken = default)
    {
        return await Saga.GetBatchesAsync(cancellationToken);
    }

    public async Task<ApiResponse<SAGABatchDetail>> GetSAGABatchDetailAsync(
        string batchId,
        CancellationToken cancellationToken = default)
    {
        return await Saga.GetBatchAsync(batchId, cancellationToken);
    }

    public async Task<ApiResponse<SAGAVerificationResult>> VerifySAGABatchAsync(
        string batchId,
        CancellationToken cancellationToken = default)
    {
        return await Saga.VerifyBatchAsync(batchId, cancellationToken);
    }

    public async Task<ApiResponse<SAGAFlushResponse>> FlushCurrentSAGABatchAsync(
        CancellationToken cancellationToken = default)
    {
        return await Saga.FlushCurrentBatchAsync(cancellationToken);
    }

    // Note: BuildQueryString moved into AuditLogEndpoint

    public void Dispose()
    {
        _httpClient?.Dispose();
        GC.SuppressFinalize(this);
    }
}
