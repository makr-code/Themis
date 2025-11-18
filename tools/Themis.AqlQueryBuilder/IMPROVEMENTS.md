# Suggested Improvements for AQL Query Builder

Based on research of industry best practices, WPF MVVM patterns, and OOP principles, here are recommended enhancements for the Themis AQL Query Builder.

**Philosophy**: Minimize third-party dependencies, focus on built-in .NET features, open-source, and on-premise deployment.

## 1. Architecture & Design Patterns

### 1.1 Service Layer (Manual Dependency Injection)
**Current**: ViewModel directly instantiates HttpClient
**Improvement**: Implement lightweight manual DI pattern

```csharp
// Add to App.xaml.cs - No third-party dependencies
public partial class App : Application
{
    private static ServiceContainer? _services;
    
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        
        // Manual service registration
        _services = new ServiceContainer();
        _services.RegisterSingleton<ISchemaService, SchemaService>();
        _services.RegisterSingleton<IConnectionService, ConnectionService>();
        _services.RegisterTransient<IAqlQueryService, AqlQueryService>();
        _services.RegisterTransient<MainViewModel>();
        
        // Create main window
        var mainWindow = _services.Resolve<MainWindow>();
        mainWindow.DataContext = _services.Resolve<MainViewModel>();
        mainWindow.Show();
    }
}

// Simple built-in DI container (no external dependencies)
public class ServiceContainer
{
    private readonly Dictionary<Type, Func<object>> _services = new();
    private readonly Dictionary<Type, object> _singletons = new();
    
    public void RegisterSingleton<TInterface, TImplementation>() 
        where TImplementation : TInterface, new()
    {
        _services[typeof(TInterface)] = () =>
        {
            if (!_singletons.ContainsKey(typeof(TInterface)))
                _singletons[typeof(TInterface)] = new TImplementation();
            return _singletons[typeof(TInterface)];
        };
    }
    
    public void RegisterTransient<TInterface, TImplementation>() 
        where TImplementation : TInterface, new()
    {
        _services[typeof(TInterface)] = () => new TImplementation();
    }
    
    public T Resolve<T>()
    {
        return (T)_services[typeof(T)]();
    }
}
```

**Benefits**:
- No external dependencies
- Better testability
- Loose coupling
- Full control over service lifetime
- Follows SOLID principles

### 1.2 Repository Pattern for Data Access
**Current**: Direct API calls in ViewModel
**Improvement**: Abstract data access behind repositories

```csharp
public interface IAqlQueryRepository
{
    Task<QueryResult> ExecuteQueryAsync(string aql, CancellationToken ct = default);
    Task<bool> TestConnectionAsync(string serverUrl, CancellationToken ct = default);
}

public interface ISchemaRepository
{
    Task<IEnumerable<SchemaCollection>> GetCollectionsAsync(CancellationToken ct = default);
    Task<SchemaCollection?> GetCollectionByNameAsync(string name, CancellationToken ct = default);
}
```

**Benefits**:
- Single Responsibility Principle
- Easy to switch implementations (HTTP/Socket/Direct API)
- Better unit testing

### 1.3 Command Pattern Enhancement
**Current**: Commands defined inline in ViewModel
**Improvement**: Extract complex commands to separate classes

```csharp
// No external logging dependencies - use Debug or custom logger
public class ExecuteQueryCommand : IAsyncRelayCommand
{
    private readonly IAqlQueryRepository _repository;
    
    public ExecuteQueryCommand(IAqlQueryRepository repository)
    {
        _repository = repository;
    }
    
    public async Task ExecuteAsync(object? parameter)
    {
        try
        {
            Debug.WriteLine("Executing AQL query");
            var result = await _repository.ExecuteQueryAsync(parameter as string);
            // Handle result
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Query execution failed: {ex.Message}");
            throw;
        }
    }
}

// Optional: Simple custom logger (no dependencies)
public interface ISimpleLogger
{
    void LogInfo(string message);
    void LogError(string message, Exception? ex = null);
}

public class DebugLogger : ISimpleLogger
{
    public void LogInfo(string message) => Debug.WriteLine($"[INFO] {message}");
    public void LogError(string message, Exception? ex = null) 
        => Debug.WriteLine($"[ERROR] {message}: {ex?.Message}");
}
```

**Benefits**:
- No external dependencies
- Reusable logic
- Better error handling
- Easier testing
- Uses built-in Debug class

## 2. Code Quality & Best Practices

### 2.1 Validation Layer
**Current**: No input validation
**Improvement**: Add built-in validation using INotifyDataErrorInfo

```csharp
// No external dependencies - using built-in .NET validation
public partial class AqlQueryModel : ObservableObject, INotifyDataErrorInfo
{
    private readonly Dictionary<string, List<string>> _errors = new();
    
    public bool HasErrors => _errors.Any();
    
    public event EventHandler<DataErrorsChangedEventArgs>? ErrorsChanged;
    
    public IEnumerable GetErrors(string? propertyName)
    {
        return propertyName != null && _errors.ContainsKey(propertyName)
            ? _errors[propertyName]
            : Enumerable.Empty<string>();
    }
    
    public ValidationResult Validate()
    {
        _errors.Clear();
        
        // Validate FOR clauses
        if (!ForClauses.Any())
            AddError(nameof(ForClauses), "At least one FOR clause is required");
            
        foreach (var forClause in ForClauses)
        {
            if (!IsValidIdentifier(forClause.Variable))
                AddError(nameof(ForClauses), $"Invalid variable name: {forClause.Variable}");
                
            if (string.IsNullOrWhiteSpace(forClause.Collection))
                AddError(nameof(ForClauses), "Collection name is required");
        }
        
        // Validate LIMIT
        if (Limit != null && (Limit.Count <= 0 || Limit.Count > 10000))
            AddError(nameof(Limit), "Limit must be between 1 and 10000");
            
        return new ValidationResult(HasErrors, _errors);
    }
    
    private void AddError(string propertyName, string error)
    {
        if (!_errors.ContainsKey(propertyName))
            _errors[propertyName] = new List<string>();
            
        _errors[propertyName].Add(error);
        ErrorsChanged?.Invoke(this, new DataErrorsChangedEventArgs(propertyName));
    }
    
    private static bool IsValidIdentifier(string identifier)
    {
        return !string.IsNullOrWhiteSpace(identifier) && 
               Regex.IsMatch(identifier, @"^[a-zA-Z_][a-zA-Z0-9_]*$");
    }
}

public class ValidationResult
{
    public bool IsValid { get; }
    public Dictionary<string, List<string>> Errors { get; }
    
    public ValidationResult(bool hasErrors, Dictionary<string, List<string>> errors)
    {
        IsValid = !hasErrors;
        Errors = errors;
    }
}
```

**Benefits**:
- No external dependencies
- Built-in WPF validation support
- Prevents invalid queries
- Better user feedback

### 2.2 Error Handling Strategy
**Current**: Basic try-catch
**Improvement**: Centralized error handling with Result pattern

```csharp
public class Result<T>
{
    public bool IsSuccess { get; }
    public T? Value { get; }
    public string? Error { get; }
    public Exception? Exception { get; }
    
    private Result(bool isSuccess, T? value, string? error, Exception? exception)
    {
        IsSuccess = isSuccess;
        Value = value;
        Error = error;
        Exception = exception;
    }
    
    public static Result<T> Success(T value) => new(true, value, null, null);
    public static Result<T> Failure(string error, Exception? ex = null) => new(false, default, error, ex);
}

// Usage
public async Task<Result<QueryResult>> ExecuteQueryAsync(string aql)
{
    try
    {
        var result = await _httpClient.PostAsync(/*...*/);
        if (!result.IsSuccessStatusCode)
            return Result<QueryResult>.Failure($"HTTP {result.StatusCode}");
            
        var data = await result.Content.ReadFromJsonAsync<QueryResult>();
        return Result<QueryResult>.Success(data!);
    }
    catch (Exception ex)
    {
        _logger.LogError(ex, "Query execution failed");
        return Result<QueryResult>.Failure("Query execution failed", ex);
    }
}
```

**Benefits**:
- Explicit error handling
- No exception swallowing
- Better debugging

### 2.3 Async/Await Best Practices
**Current**: Some async operations, but not consistent
**Improvement**: ConfigureAwait and CancellationToken throughout

```csharp
public async Task<Result<QueryResult>> ExecuteQueryAsync(
    string aql, 
    CancellationToken cancellationToken = default)
{
    try
    {
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromSeconds(30)); // Timeout
        
        var response = await _httpClient
            .PostAsync(endpoint, content, cts.Token)
            .ConfigureAwait(false);
            
        // Rest of implementation
    }
    catch (OperationCanceledException)
    {
        return Result<QueryResult>.Failure("Query execution timed out");
    }
}
```

**Benefits**:
- Prevents deadlocks
- Better performance
- Proper timeout handling

## 3. UI/UX Enhancements

### 3.1 Query History & Favorites
**New Feature**: Save and reuse queries

```csharp
public interface IQueryHistoryService
{
    Task<IEnumerable<SavedQuery>> GetHistoryAsync();
    Task SaveQueryAsync(SavedQuery query);
    Task<SavedQuery?> GetFavoriteAsync(string name);
    Task AddFavoriteAsync(SavedQuery query);
}

public class SavedQuery
{
    public string Id { get; set; } = Guid.NewGuid().ToString();
    public string Name { get; set; } = string.Empty;
    public string AqlQuery { get; set; } = string.Empty;
    public QueryType QueryType { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public bool IsFavorite { get; set; }
    public Dictionary<string, object> Metadata { get; set; } = new();
}
```

### 3.2 Query Builder Intellisense
**New Feature**: Autocomplete for collections and fields

```csharp
public class IntellisenseProvider
{
    private readonly ISchemaService _schemaService;
    
    public async Task<IEnumerable<CompletionItem>> GetCollectionCompletionsAsync(string prefix)
    {
        var collections = await _schemaService.GetCollectionsAsync();
        return collections
            .Where(c => c.Name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            .Select(c => new CompletionItem
            {
                Text = c.Name,
                Description = $"{c.Type} - {c.FieldCount} fields",
                Icon = GetIconForType(c.Type)
            });
    }
    
    public async Task<IEnumerable<CompletionItem>> GetFieldCompletionsAsync(
        string collection, 
        string prefix)
    {
        var schema = await _schemaService.GetCollectionByNameAsync(collection);
        return schema?.Fields
            .Where(f => f.Name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            .Select(f => new CompletionItem
            {
                Text = f.Name,
                Description = f.DataType,
                Icon = GetIconForDataType(f.DataType)
            }) ?? Enumerable.Empty<CompletionItem>();
    }
}
```

### 3.3 Query Execution Plan Visualization
**New Feature**: Show query execution statistics

```csharp
public class QueryPlan
{
    public TimeSpan ExecutionTime { get; set; }
    public int DocumentsScanned { get; set; }
    public int DocumentsReturned { get; set; }
    public List<ExecutionStep> Steps { get; set; } = new();
    public Dictionary<string, long> IndexUsage { get; set; } = new();
}

public class ExecutionStep
{
    public string Operation { get; set; } = string.Empty;
    public TimeSpan Duration { get; set; }
    public int InputDocuments { get; set; }
    public int OutputDocuments { get; set; }
}
```

## 4. Performance Optimizations

### 4.1 Collection Virtualization
**Current**: All items loaded in memory
**Improvement**: Use VirtualizingStackPanel for large result sets

```xaml
<ListBox VirtualizingPanel.IsVirtualizing="True"
         VirtualizingPanel.VirtualizationMode="Recycling"
         VirtualizingPanel.CacheLength="20,20"
         VirtualizingPanel.CacheLengthUnit="Item">
    <!-- Items -->
</ListBox>
```

### 4.2 Lazy Loading for Schema
**Current**: All schema loaded upfront
**Improvement**: Lazy loading using built-in Lazy<T>

```csharp
// Using built-in Lazy<T> - no external dependencies
public class SchemaService : ISchemaService
{
    private readonly Lazy<Task<IEnumerable<SchemaCollection>>> _schemaCache;
    private readonly ISchemaRepository _repository;
    
    public SchemaService(ISchemaRepository repository)
    {
        _repository = repository;
        _schemaCache = new Lazy<Task<IEnumerable<SchemaCollection>>>(
            async () => await _repository.GetAllCollectionsAsync());
    }
    
    public async Task<IEnumerable<SchemaCollection>> GetCollectionsAsync()
    {
        return await _schemaCache.Value;
    }
    
    public void ClearCache()
    {
        // Reset lazy if needed
        if (_schemaCache.IsValueCreated)
        {
            _schemaCache = new Lazy<Task<IEnumerable<SchemaCollection>>>(
                async () => await _repository.GetAllCollectionsAsync());
        }
    }
}
```

**Benefits**:
- Built-in .NET feature
- No external dependencies
- Thread-safe initialization
- Improved startup performance

### 4.3 Debouncing for Real-time Query Generation
**Current**: Updates on every keystroke
**Improvement**: Debounce query generation

```csharp
public class DebouncedRelayCommand : IAsyncRelayCommand
{
    private readonly TimeSpan _delay;
    private CancellationTokenSource? _cts;
    
    public DebouncedRelayCommand(Func<Task> execute, TimeSpan delay)
    {
        _execute = execute;
        _delay = delay;
    }
    
    public async Task ExecuteAsync(object? parameter)
    {
        _cts?.Cancel();
        _cts = new CancellationTokenSource();
        
        try
        {
            await Task.Delay(_delay, _cts.Token);
            await _execute();
        }
        catch (TaskCanceledException) { }
    }
}
```

## 5. Testing Infrastructure

### 5.1 Unit Tests for ViewModels
**New**: Add comprehensive unit tests using built-in patterns

```csharp
// Using manual mocking - no Moq dependency
public class MainViewModelTests
{
    private readonly MockAqlQueryRepository _mockRepository;
    private readonly MainViewModel _viewModel;
    
    public MainViewModelTests()
    {
        _mockRepository = new MockAqlQueryRepository();
        _viewModel = new MainViewModel(_mockRepository);
    }
    
    [Fact]
    public async Task ExecuteQuery_WithValidQuery_ReturnsResults()
    {
        // Arrange
        var expectedResult = new QueryResult { /* ... */ };
        _mockRepository.SetupExecuteQuery(expectedResult);
            
        // Act
        await _viewModel.ExecuteQueryCommand.ExecuteAsync(null);
        
        // Assert
        Assert.False(_viewModel.IsExecuting);
        Assert.NotNull(_viewModel.QueryResult);
    }
}

// Manual mock implementation (no Moq dependency)
public class MockAqlQueryRepository : IAqlQueryRepository
{
    private QueryResult? _queryResult;
    private bool _connectionResult;
    
    public void SetupExecuteQuery(QueryResult result)
    {
        _queryResult = result;
    }
    
    public void SetupTestConnection(bool success)
    {
        _connectionResult = success;
    }
    
    public Task<Result<QueryResult>> ExecuteQueryAsync(string aql, CancellationToken ct = default)
    {
        return Task.FromResult(_queryResult != null
            ? Result<QueryResult>.Success(_queryResult)
            : Result<QueryResult>.Failure("No result configured"));
    }
    
    public Task<bool> TestConnectionAsync(string serverUrl, CancellationToken ct = default)
    {
        return Task.FromResult(_connectionResult);
    }
}
```

**Benefits**:
- No Moq dependency
- Full control over test doubles
- Simple and maintainable
- Works with xUnit (minimal dependency)

### 5.2 Integration Tests
**New**: Test actual API integration using built-in HttpClient

```csharp
// No WebApplicationFactory dependency - direct HttpClient testing
public class AqlQueryServiceIntegrationTests
{
    private readonly HttpClient _client;
    
    public AqlQueryServiceIntegrationTests()
    {
        // Direct HttpClient usage - no additional dependencies
        _client = new HttpClient
        {
            BaseAddress = new Uri("http://localhost:8080")
        };
    }
    
    [Fact]
    public async Task ExecuteQuery_ValidAql_ReturnsResults()
    {
        // Arrange
        var service = new AqlQueryService(_client);
        var query = "FOR u IN users LIMIT 10 RETURN u";
        
        // Act
        var result = await service.ExecuteQueryAsync(query);
        
        // Assert
        Assert.True(result.IsSuccess);
        Assert.NotNull(result.Value);
    }
    
    [Fact]
    public async Task TestConnection_ValidServer_ReturnsTrue()
    {
        // Arrange
        var service = new AqlQueryService(_client);
        
        // Act
        var connected = await service.TestConnectionAsync("http://localhost:8080");
        
        // Assert
        Assert.True(connected);
    }
}
```

**Benefits**:
- No external test framework dependencies beyond xUnit
- Tests real HTTP communication
- Simple and direct
- Easy to understand and maintain

## 6. Documentation & Maintainability

### 6.1 XML Documentation
**Current**: Partial documentation
**Improvement**: Complete XML docs for all public APIs

```csharp
/// <summary>
/// Service for executing AQL queries against Themis database
/// </summary>
/// <remarks>
/// Supports multiple connection types including HTTP, TCP Socket, UDP, and direct API access.
/// All methods are thread-safe and support cancellation.
/// </remarks>
public interface IAqlQueryService
{
    /// <summary>
    /// Executes an AQL query and returns the results
    /// </summary>
    /// <param name="aql">The AQL query string to execute</param>
    /// <param name="cancellationToken">Cancellation token for the operation</param>
    /// <returns>A Result containing the query results or an error</returns>
    /// <exception cref="ArgumentNullException">Thrown when aql is null or empty</exception>
    /// <exception cref="InvalidQueryException">Thrown when the AQL syntax is invalid</exception>
    Task<Result<QueryResult>> ExecuteQueryAsync(string aql, CancellationToken cancellationToken = default);
}
```

### 6.2 Architecture Decision Records (ADR)
**New**: Document key architectural decisions

Create `/docs/adr/` directory with:
- `001-mvvm-pattern.md` - Why MVVM was chosen
- `002-dependency-injection.md` - DI container selection
- `003-multi-model-support.md` - Multi-model query approach
- `004-connection-types.md` - Multiple connection type support

## 7. Security Enhancements

### 7.1 Query Sanitization
**New**: Prevent injection attacks

```csharp
public class QuerySanitizer
{
    private static readonly Regex SafeIdentifier = new(@"^[a-zA-Z_][a-zA-Z0-9_]*$");
    
    public static bool IsValidIdentifier(string identifier)
    {
        return !string.IsNullOrWhiteSpace(identifier) && SafeIdentifier.IsMatch(identifier);
    }
    
    public static string SanitizeStringValue(string value)
    {
        // Escape special characters in string literals
        return value.Replace("\\", "\\\\").Replace("\"", "\\\"");
    }
}
```

### 7.2 Secure Connection Configuration
**Current**: Plain text passwords
**Improvement**: Use Windows DPAPI for encryption (built-in)

```csharp
using System.Security.Cryptography;

// Using built-in Windows Data Protection API - no external dependencies
public class SecureConnectionConfig
{
    public async Task SaveConnectionAsync(ConnectionConfig config)
    {
        if (!string.IsNullOrEmpty(config.Password))
        {
            // Encrypt using Windows DPAPI
            var passwordBytes = Encoding.UTF8.GetBytes(config.Password);
            var encryptedBytes = ProtectedData.Protect(
                passwordBytes, 
                null, 
                DataProtectionScope.CurrentUser);
            
            // Save encrypted password
            var encryptedPassword = Convert.ToBase64String(encryptedBytes);
            await SaveToFileAsync(config.ServerUrl, config.Username, encryptedPassword);
            
            config.Password = null; // Don't keep in memory
        }
    }
    
    public async Task<string?> RetrievePasswordAsync(string serverUrl, string username)
    {
        var encryptedPassword = await LoadFromFileAsync(serverUrl, username);
        if (string.IsNullOrEmpty(encryptedPassword))
            return null;
            
        // Decrypt using Windows DPAPI
        var encryptedBytes = Convert.FromBase64String(encryptedPassword);
        var decryptedBytes = ProtectedData.Unprotect(
            encryptedBytes, 
            null, 
            DataProtectionScope.CurrentUser);
            
        return Encoding.UTF8.GetString(decryptedBytes);
    }
    
    private async Task SaveToFileAsync(string server, string user, string encryptedPwd)
    {
        var configPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ThemisDB", "connections.json");
            
        // Save to file (implementation details)
    }
}
```

**Benefits**:
- No external dependencies
- Windows built-in encryption
- Secure credential storage
- Per-user protection

## 8. Additional Features

### 8.1 Export Query Results
**New**: Export to JSON, CSV using built-in .NET features

```csharp
// No external dependencies - using built-in .NET features
public interface IQueryResultExporter
{
    Task ExportToCsvAsync(QueryResult result, string filePath);
    Task ExportToJsonAsync(QueryResult result, string filePath);
}

public class QueryResultExporter : IQueryResultExporter
{
    public async Task ExportToJsonAsync(QueryResult result, string filePath)
    {
        // Using System.Text.Json (built-in .NET)
        var options = new JsonSerializerOptions 
        { 
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        };
        
        var json = JsonSerializer.Serialize(result, options);
        await File.WriteAllTextAsync(filePath, json);
    }
    
    public async Task ExportToCsvAsync(QueryResult result, string filePath)
    {
        // Manual CSV generation - no external library needed
        using var writer = new StreamWriter(filePath);
        
        // Write headers
        if (result.Rows.Any())
        {
            var headers = string.Join(",", result.Columns.Select(EscapeCsv));
            await writer.WriteLineAsync(headers);
            
            // Write data
            foreach (var row in result.Rows)
            {
                var values = row.Select(v => EscapeCsv(v?.ToString() ?? ""));
                await writer.WriteLineAsync(string.Join(",", values));
            }
        }
    }
    
    private static string EscapeCsv(string value)
    {
        if (value.Contains(',') || value.Contains('"') || value.Contains('\n'))
            return $"\"{value.Replace("\"", "\"\"")}\"";
        return value;
    }
}
```

**Benefits**:
- No external dependencies
- Built-in JSON serialization (System.Text.Json)
- Simple CSV generation
- Full control over export format

### 8.2 Query Templates
**New**: Pre-built query templates for common scenarios

```csharp
public class QueryTemplate
{
    public string Id { get; set; } = Guid.NewGuid().ToString();
    public string Name { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public QueryType Type { get; set; }
    public string TemplateAql { get; set; } = string.Empty;
    public List<TemplateParameter> Parameters { get; set; } = new();
}

public class TemplateParameter
{
    public string Name { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public string DefaultValue { get; set; } = string.Empty;
    public ParameterType Type { get; set; }
}
```

### 8.3 Dark Mode Support
**New**: Theme switching

```csharp
public class ThemeService : IThemeService
{
    public void ApplyTheme(AppTheme theme)
    {
        var dictionary = theme switch
        {
            AppTheme.Dark => new ResourceDictionary { Source = new Uri("Themes/Dark.xaml", UriKind.Relative) },
            AppTheme.Light => new ResourceDictionary { Source = new Uri("Themes/Light.xaml", UriKind.Relative) },
            _ => throw new ArgumentOutOfRangeException(nameof(theme))
        };
        
        Application.Current.Resources.MergedDictionaries.Clear();
        Application.Current.Resources.MergedDictionaries.Add(dictionary);
    }
}
```

## Priority Implementation Order

### High Priority (Immediate)
1. **Dependency Injection** - Foundation for better architecture
2. **Validation Layer** - Prevent invalid queries
3. **Error Handling** - Better user experience
4. **Query History** - Highly requested feature

### Medium Priority (Next Sprint)
5. **Repository Pattern** - Better separation of concerns
6. **Unit Tests** - Ensure code quality
7. **Intellisense** - Improve usability
8. **Export Results** - Common request

### Low Priority (Future)
9. **Query Templates** - Nice to have
10. **Dark Mode** - UI enhancement
11. **Advanced Performance Optimizations** - If needed
12. **Advanced Security Features** - Based on deployment needs

## Package Recommendations

**Minimal Dependencies Philosophy**: Use built-in .NET features where possible.

### Essential Only (if needed)

```xml
<!-- Unit Testing Framework - minimal and standard -->
<PackageReference Include="xUnit" Version="2.6.2" />
<PackageReference Include="xUnit.runner.visualstudio" Version="2.5.6" />

<!-- Already included in project -->
<PackageReference Include="CommunityToolkit.Mvvm" Version="8.2.2" />
```

### Optional (Evaluate on a case-by-case basis)

```xml
<!-- Retry logic - consider implementing manually first -->
<!-- <PackageReference Include="Polly" Version="8.2.0" /> -->

<!-- Logging - consider using built-in ILogger first -->
<!-- <PackageReference Include="Serilog.Extensions.Logging" Version="8.0.0" /> -->

<!-- Validation - use built-in INotifyDataErrorInfo first -->
<!-- <PackageReference Include="FluentValidation" Version="11.9.0" /> -->
```

### Built-in .NET Features to Use Instead

**Instead of Microsoft.Extensions.DependencyInjection:**
- Use manual service container (see Section 1.1)

**Instead of FluentValidation:**
- Use INotifyDataErrorInfo (see Section 2.1)

**Instead of Moq:**
- Use manual mock implementations (see Section 5.1)

**Instead of Serilog:**
- Use System.Diagnostics.Debug or built-in ILogger

**Instead of AsyncLazy:**
- Use built-in Lazy<Task<T>> (see Section 4.2)

**Instead of credential libraries:**
- Use Windows DPAPI (ProtectedData) (see Section 7.2)

**Instead of CSV/Excel libraries:**
- Manual CSV generation (see Section 8.1)
- System.Text.Json for JSON (built-in)

### On-Premise Considerations

**Database Connection:**
- Direct C# API integration (no network dependencies)
- Direct C++ P/Invoke (native performance)
- HTTP/Socket/UDP for flexibility

**Data Storage:**
- Local file system for query history
- SQLite for local database (if needed - lightweight, embedded)

**No Cloud Dependencies:**
- All features work offline
- No telemetry or external services
- Full data sovereignty

**Security:**
- Windows DPAPI for encryption
- Local credential storage
- No external authentication services