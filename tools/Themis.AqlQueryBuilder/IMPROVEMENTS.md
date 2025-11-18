# Suggested Improvements for AQL Query Builder

Based on research of industry best practices, WPF MVVM patterns, and OOP principles, here are recommended enhancements for the Themis AQL Query Builder.

## 1. Architecture & Design Patterns

### 1.1 Service Layer (Dependency Injection)
**Current**: ViewModel directly instantiates HttpClient
**Improvement**: Implement proper DI container

```csharp
// Add to App.xaml.cs
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

public partial class App : Application
{
    private readonly IHost _host;
    
    public App()
    {
        _host = Host.CreateDefaultBuilder()
            .ConfigureServices((context, services) =>
            {
                // Services
                services.AddHttpClient<IAqlQueryService, AqlQueryService>();
                services.AddSingleton<ISchemaService, SchemaService>();
                services.AddSingleton<IConnectionService, ConnectionService>();
                
                // ViewModels
                services.AddTransient<MainViewModel>();
                
                // Views
                services.AddTransient<MainWindow>();
            })
            .Build();
    }
}
```

**Benefits**:
- Better testability
- Loose coupling
- Easier to mock dependencies
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
public class ExecuteQueryCommand : IAsyncRelayCommand
{
    private readonly IAqlQueryRepository _repository;
    private readonly ILogger<ExecuteQueryCommand> _logger;
    
    public ExecuteQueryCommand(IAqlQueryRepository repository, ILogger<ExecuteQueryCommand> logger)
    {
        _repository = repository;
        _logger = logger;
    }
    
    public async Task ExecuteAsync(object? parameter)
    {
        try
        {
            _logger.LogInformation("Executing AQL query");
            var result = await _repository.ExecuteQueryAsync(parameter as string);
            // Handle result
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Query execution failed");
            throw;
        }
    }
}
```

**Benefits**:
- Reusable logic
- Better error handling
- Easier testing

## 2. Code Quality & Best Practices

### 2.1 Validation Layer
**Current**: No input validation
**Improvement**: Add FluentValidation

```csharp
public class AqlQueryModelValidator : AbstractValidator<AqlQueryModel>
{
    public AqlQueryModelValidator()
    {
        RuleFor(x => x.ForClauses)
            .NotEmpty()
            .WithMessage("At least one FOR clause is required");
            
        RuleForEach(x => x.ForClauses)
            .SetValidator(new ForClauseValidator());
            
        RuleFor(x => x.Limit.Count)
            .GreaterThan(0)
            .LessThanOrEqualTo(10000)
            .WithMessage("Limit must be between 1 and 10000");
    }
}

public class ForClauseValidator : AbstractValidator<ForClause>
{
    public ForClauseValidator()
    {
        RuleFor(x => x.Variable)
            .NotEmpty()
            .Matches(@"^[a-zA-Z_][a-zA-Z0-9_]*$")
            .WithMessage("Variable must be a valid identifier");
            
        RuleFor(x => x.Collection)
            .NotEmpty()
            .WithMessage("Collection name is required");
    }
}
```

**Benefits**:
- Prevents invalid queries
- Better user feedback
- Consistent validation rules

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
**Improvement**: Load schema on-demand

```csharp
public class SchemaService : ISchemaService
{
    private readonly AsyncLazy<IEnumerable<SchemaCollection>> _schemaCache;
    
    public SchemaService(ISchemaRepository repository)
    {
        _schemaCache = new AsyncLazy<IEnumerable<SchemaCollection>>(
            async () => await repository.GetAllCollectionsAsync());
    }
    
    public async Task<IEnumerable<SchemaCollection>> GetCollectionsAsync()
    {
        return await _schemaCache.Value;
    }
}
```

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
**New**: Add comprehensive unit tests

```csharp
public class MainViewModelTests
{
    private readonly Mock<IAqlQueryRepository> _mockRepository;
    private readonly MainViewModel _viewModel;
    
    public MainViewModelTests()
    {
        _mockRepository = new Mock<IAqlQueryRepository>();
        _viewModel = new MainViewModel(_mockRepository.Object);
    }
    
    [Fact]
    public async Task ExecuteQuery_WithValidQuery_ReturnsResults()
    {
        // Arrange
        var expectedResult = new QueryResult { /* ... */ };
        _mockRepository
            .Setup(r => r.ExecuteQueryAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(Result<QueryResult>.Success(expectedResult));
            
        // Act
        await _viewModel.ExecuteQueryCommand.ExecuteAsync(null);
        
        // Assert
        Assert.False(_viewModel.IsExecuting);
        Assert.NotNull(_viewModel.QueryResult);
    }
}
```

### 5.2 Integration Tests
**New**: Test actual API integration

```csharp
public class AqlQueryServiceIntegrationTests : IClassFixture<WebApplicationFactory<Program>>
{
    private readonly HttpClient _client;
    
    public AqlQueryServiceIntegrationTests(WebApplicationFactory<Program> factory)
    {
        _client = factory.CreateClient();
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
}
```

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
**Improvement**: Use Windows Credential Manager or encrypted storage

```csharp
public class SecureConnectionConfig
{
    private readonly ICredentialService _credentialService;
    
    public async Task SaveConnectionAsync(ConnectionConfig config)
    {
        if (!string.IsNullOrEmpty(config.Password))
        {
            await _credentialService.SaveCredentialAsync(
                $"ThemisDB_{config.ServerUrl}", 
                config.Username, 
                config.Password);
            
            config.Password = null; // Don't store in memory
        }
    }
}
```

## 8. Additional Features

### 8.1 Export Query Results
**New**: Export to JSON, CSV, Excel

```csharp
public interface IQueryResultExporter
{
    Task ExportToCsvAsync(QueryResult result, string filePath);
    Task ExportToJsonAsync(QueryResult result, string filePath);
    Task ExportToExcelAsync(QueryResult result, string filePath);
}
```

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

Add these NuGet packages:

```xml
<!-- Dependency Injection -->
<PackageReference Include="Microsoft.Extensions.DependencyInjection" Version="8.0.0" />
<PackageReference Include="Microsoft.Extensions.Hosting" Version="8.0.0" />

<!-- Logging -->
<PackageReference Include="Serilog.Extensions.Logging" Version="8.0.0" />
<PackageReference Include="Serilog.Sinks.File" Version="5.0.0" />

<!-- Validation -->
<PackageReference Include="FluentValidation" Version="11.9.0" />

<!-- Testing -->
<PackageReference Include="xUnit" Version="2.6.2" />
<PackageReference Include="Moq" Version="4.20.69" />
<PackageReference Include="FluentAssertions" Version="6.12.0" />

<!-- Utilities -->
<PackageReference Include="Polly" Version="8.2.0" /> <!-- Retry policies -->
<PackageReference Include="AsyncLazy" Version="1.1.0" />
