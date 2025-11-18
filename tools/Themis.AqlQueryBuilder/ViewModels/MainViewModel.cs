using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using System.Collections.ObjectModel;
using System.Net.Http;
using System.Windows;
using Themis.AqlQueryBuilder.Models;
using Themis.AdminTools.Shared.ApiClient;
using Themis.AdminTools.Shared.Models;
using System.Text.Json;

namespace Themis.AqlQueryBuilder.ViewModels;

/// <summary>
/// Main ViewModel for the AQL Query Builder
/// </summary>
public partial class MainViewModel : ObservableObject
{
    private readonly HttpClient _httpClient;

    [ObservableProperty]
    private AqlQueryModel _query;

    [ObservableProperty]
    private string _generatedQuery = string.Empty;

    [ObservableProperty]
    private string _queryResult = string.Empty;

    [ObservableProperty]
    private bool _isExecuting;

    [ObservableProperty]
    private string _serverUrl = "http://localhost:8080";

    // Collections for UI binding
    public ObservableCollection<ForClause> ForClauses { get; } = new();
    public ObservableCollection<LetClause> LetClauses { get; } = new();
    public ObservableCollection<FilterClause> FilterClauses { get; } = new();
    public ObservableCollection<SortClause> SortClauses { get; } = new();
    
    // Phase 1.5: COLLECT clause support
    public ObservableCollection<GroupByField> GroupByFields { get; } = new();
    public ObservableCollection<AggregateField> AggregateFields { get; } = new();

    public ObservableCollection<string> AvailableCollections { get; } = new()
    {
        "users", "orders", "products", "customers", "cities"
    };

    public ObservableCollection<string> AggregationFunctions { get; } = new()
    {
        "COUNT", "SUM", "AVG", "MIN", "MAX"
    };

    public ObservableCollection<SchemaCollection> SchemaCollections { get; } = new();

    [ObservableProperty]
    private QueryType _selectedQueryType = QueryType.Relational;

    [ObservableProperty]
    private ConnectionConfig _connectionConfig = new();

    [ObservableProperty]
    private ConnectionStatus _connectionStatus = ConnectionStatus.Disconnected;

    public ObservableCollection<ConnectionType> AvailableConnectionTypes { get; } = new()
    {
        ConnectionType.HttpRest,
        ConnectionType.Socket,
        ConnectionType.Udp,
        ConnectionType.DirectCSharp,
        ConnectionType.DirectCpp
    };

    public MainViewModel()
    {
        _httpClient = new HttpClient();
        _query = new AqlQueryModel();
        
        // Initialize schema with sample collections
        LoadSampleSchema();
        
        // Initialize with a sample query
        AddSampleQuery();
    }

    private void LoadSampleSchema()
    {
        // Sample schema - in real app, this would be loaded from API
        SchemaCollections.Add(new SchemaCollection
        {
            Name = "users",
            Type = CollectionType.Relational,
            EstimatedDocumentCount = 1250,
            Fields = new List<SchemaField>
            {
                new() { Name = "_key", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "name", DataType = FieldDataType.String },
                new() { Name = "email", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "age", DataType = FieldDataType.Integer },
                new() { Name = "city", DataType = FieldDataType.String },
                new() { Name = "created_at", DataType = FieldDataType.DateTime }
            }
        });

        SchemaCollections.Add(new SchemaCollection
        {
            Name = "products",
            Type = CollectionType.Hybrid,
            EstimatedDocumentCount = 5000,
            HasVectorIndex = true,
            Fields = new List<SchemaField>
            {
                new() { Name = "_key", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "name", DataType = FieldDataType.String },
                new() { Name = "description", DataType = FieldDataType.String },
                new() { Name = "price", DataType = FieldDataType.Float },
                new() { Name = "category", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "embedding", DataType = FieldDataType.Vector, IsVectorField = true, VectorDimension = 384 }
            }
        });

        SchemaCollections.Add(new SchemaCollection
        {
            Name = "stores",
            Type = CollectionType.Geo,
            EstimatedDocumentCount = 150,
            HasGeoIndex = true,
            Fields = new List<SchemaField>
            {
                new() { Name = "_key", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "name", DataType = FieldDataType.String },
                new() { Name = "address", DataType = FieldDataType.String },
                new() { Name = "location", DataType = FieldDataType.GeoPoint, IsGeoField = true },
                new() { Name = "service_area", DataType = FieldDataType.GeoPolygon, IsGeoField = true }
            }
        });

        SchemaCollections.Add(new SchemaCollection
        {
            Name = "follows",
            Type = CollectionType.Graph,
            EstimatedDocumentCount = 8500,
            HasGraphEdges = true,
            Fields = new List<SchemaField>
            {
                new() { Name = "_from", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "_to", DataType = FieldDataType.String, IsIndexed = true },
                new() { Name = "_type", DataType = FieldDataType.String },
                new() { Name = "since", DataType = FieldDataType.Date }
            }
        });
    }

    [RelayCommand]
    private void AddForClause()
    {
        var forClause = new ForClause 
        { 
            Variable = $"var{ForClauses.Count + 1}", 
            Collection = "users" 
        };
        ForClauses.Add(forClause);
        Query.ForClauses.Add(forClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void RemoveForClause(ForClause forClause)
    {
        ForClauses.Remove(forClause);
        Query.ForClauses.Remove(forClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void AddLetClause()
    {
        var letClause = new LetClause 
        { 
            Variable = $"let{LetClauses.Count + 1}", 
            Expression = "value" 
        };
        LetClauses.Add(letClause);
        Query.LetClauses.Add(letClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void RemoveLetClause(LetClause letClause)
    {
        LetClauses.Remove(letClause);
        Query.LetClauses.Remove(letClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void AddFilterClause()
    {
        var filterClause = new FilterClause 
        { 
            LeftOperand = ForClauses.FirstOrDefault()?.Variable + ".field" ?? "doc.field",
            Operator = FilterOperator.Equals,
            RightOperand = "\"value\""
        };
        filterClause.UpdateCondition();
        FilterClauses.Add(filterClause);
        Query.FilterClauses.Add(filterClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void RemoveFilterClause(FilterClause filterClause)
    {
        FilterClauses.Remove(filterClause);
        Query.FilterClauses.Remove(filterClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void AddSortClause()
    {
        var sortClause = new SortClause 
        { 
            Expression = ForClauses.FirstOrDefault()?.Variable + ".field" ?? "doc.field",
            Ascending = true 
        };
        SortClauses.Add(sortClause);
        Query.SortClauses.Add(sortClause);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void RemoveSortClause(SortClause sortClause)
    {
        SortClauses.Remove(sortClause);
        Query.SortClauses.Remove(sortClause);
        UpdateGeneratedQuery();
    }

    // Phase 1.5: COLLECT clause commands
    [RelayCommand]
    private void AddGroupByField()
    {
        var groupBy = new GroupByField
        {
            Variable = $"group{GroupByFields.Count + 1}",
            Expression = ForClauses.FirstOrDefault()?.Variable + ".field" ?? "doc.field"
        };
        GroupByFields.Add(groupBy);
        if (Query.Collect == null)
            Query.Collect = new CollectClause();
        Query.Collect.GroupByFields.Add(groupBy);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void RemoveGroupByField(GroupByField groupBy)
    {
        GroupByFields.Remove(groupBy);
        Query.Collect?.GroupByFields.Remove(groupBy);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void AddAggregateField()
    {
        var aggregate = new AggregateField
        {
            Variable = $"agg{AggregateFields.Count + 1}",
            Function = "COUNT",
            Expression = "1"
        };
        AggregateFields.Add(aggregate);
        if (Query.Collect == null)
            Query.Collect = new CollectClause();
        Query.Collect.AggregateFields.Add(aggregate);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void RemoveAggregateField(AggregateField aggregate)
    {
        AggregateFields.Remove(aggregate);
        Query.Collect?.AggregateFields.Remove(aggregate);
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void UpdateLimit()
    {
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void UpdateReturn()
    {
        Query.Return.SetForClauses(Query.ForClauses);
        Query.Return.UpdateExpression();
        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void UpdateGeneratedQuery()
    {
        GeneratedQuery = Query.ToAqlString();
    }

    [RelayCommand]
    private async Task ExecuteQuery()
    {
        if (string.IsNullOrWhiteSpace(GeneratedQuery))
        {
            QueryResult = "Error: Query is empty";
            return;
        }

        IsExecuting = true;
        ConnectionStatus = ConnectionStatus.Connecting;
        QueryResult = "Executing query...";

        try
        {
            // Create request body
            var requestBody = new
            {
                query = GeneratedQuery,
                explain = false
            };

            var json = JsonSerializer.Serialize(requestBody);
            var content = new StringContent(json, System.Text.Encoding.UTF8, "application/json");

            // Execute query via API
            _httpClient.BaseAddress = new Uri(ServerUrl);
            var response = await _httpClient.PostAsync("/api/query/aql", content);
            
            if (response.IsSuccessStatusCode)
            {
                ConnectionStatus = ConnectionStatus.Connected;
                var resultJson = await response.Content.ReadAsStringAsync();
                QueryResult = FormatJson(resultJson);
            }
            else
            {
                ConnectionStatus = ConnectionStatus.Error;
                QueryResult = $"Error: {response.StatusCode}\n{await response.Content.ReadAsStringAsync()}";
            }
        }
        catch (Exception ex)
        {
            ConnectionStatus = ConnectionStatus.Error;
            QueryResult = $"Error executing query:\n{ex.Message}\n\nNote: Make sure the Themis server is running at {ServerUrl}";
        }
        finally
        {
            IsExecuting = false;
        }
    }

    // Phase 1.5: Connection test command
    [RelayCommand]
    private async Task TestConnection()
    {
        ConnectionStatus = ConnectionStatus.Connecting;
        QueryResult = "Testing connection...";

        try
        {
            _httpClient.BaseAddress = new Uri(ServerUrl);
            _httpClient.Timeout = TimeSpan.FromSeconds(5);
            
            var response = await _httpClient.GetAsync("/api/health");
            
            if (response.IsSuccessStatusCode)
            {
                ConnectionStatus = ConnectionStatus.Connected;
                QueryResult = $"✅ Connected to {ServerUrl}\n{await response.Content.ReadAsStringAsync()}";
            }
            else
            {
                ConnectionStatus = ConnectionStatus.Error;
                QueryResult = $"❌ Connection failed: {response.StatusCode}";
            }
        }
        catch (Exception ex)
        {
            ConnectionStatus = ConnectionStatus.Error;
            QueryResult = $"❌ Connection error:\n{ex.Message}";
        }
    }

    [RelayCommand]
    private void ClearQuery()
    {
        ForClauses.Clear();
        LetClauses.Clear();
        FilterClauses.Clear();
        SortClauses.Clear();
        GroupByFields.Clear();
        AggregateFields.Clear();
        Query = new AqlQueryModel();
        GeneratedQuery = string.Empty;
        QueryResult = string.Empty;
    }

    [RelayCommand]
    private void AddSampleQuery()
    {
        ClearQuery();

        // Sample: FOR u IN users FILTER u.age > 18 SORT u.name ASC LIMIT 10 RETURN u
        var forClause = new ForClause { Variable = "u", Collection = "users" };
        ForClauses.Add(forClause);
        Query.ForClauses.Add(forClause);

        var filterClause = new FilterClause 
        { 
            LeftOperand = "u.age",
            Operator = FilterOperator.GreaterThan,
            RightOperand = "18"
        };
        filterClause.UpdateCondition();
        FilterClauses.Add(filterClause);
        Query.FilterClauses.Add(filterClause);

        var sortClause = new SortClause { Expression = "u.name", Ascending = true };
        SortClauses.Add(sortClause);
        Query.SortClauses.Add(sortClause);

        Query.Limit = new LimitClause { Count = 10 };

        Query.Return = new ReturnClause { Type = ReturnType.WholeDocument };
        Query.Return.SetForClauses(Query.ForClauses);
        Query.Return.UpdateExpression();

        UpdateGeneratedQuery();
    }

    [RelayCommand]
    private void CopyQuery()
    {
        if (!string.IsNullOrWhiteSpace(GeneratedQuery))
        {
            Clipboard.SetText(GeneratedQuery);
        }
    }

    private string FormatJson(string json)
    {
        try
        {
            var jsonDocument = JsonDocument.Parse(json);
            return JsonSerializer.Serialize(jsonDocument, new JsonSerializerOptions 
            { 
                WriteIndented = true 
            });
        }
        catch
        {
            return json;
        }
    }
}
