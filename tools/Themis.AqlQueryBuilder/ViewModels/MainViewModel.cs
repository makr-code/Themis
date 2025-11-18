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

    // Phase 2: Graph query support
    public ObservableCollection<GraphNode> GraphNodes { get; } = new();
    public ObservableCollection<GraphEdge> GraphEdges { get; } = new();
    
    [ObservableProperty]
    private GraphPattern _graphPattern = new();
    
    [ObservableProperty]
    private string _graphAql = string.Empty;

    // Phase 3: Vector search support
    public ObservableCollection<VectorFilter> VectorMetadataFilters { get; } = new();
    public ObservableCollection<VectorWeight> VectorWeights { get; } = new();
    
    [ObservableProperty]
    private VectorQuery _vectorQuery = new();
    
    [ObservableProperty]
    private string _vectorAql = string.Empty;

    public ObservableCollection<string> DistanceMetrics { get; } = new()
    {
        "Cosine", "Euclidean", "DotProduct", "Manhattan"
    };

    public ObservableCollection<string> VectorSearchModes { get; } = new()
    {
        "Similarity", "MultiVector", "Range"
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

    // Phase 2: Graph query commands
    [RelayCommand]
    private void AddGraphNode()
    {
        var node = new GraphNode
        {
            Variable = $"node{GraphNodes.Count + 1}",
            Collection = "users",
            X = 100 + GraphNodes.Count * 150,
            Y = 100,
            Label = $"Node {GraphNodes.Count + 1}"
        };
        GraphNodes.Add(node);
        GraphPattern.Nodes.Add(node);
        UpdateGraphAql();
    }

    [RelayCommand]
    private void RemoveGraphNode(GraphNode node)
    {
        // Remove edges connected to this node
        var connectedEdges = GraphEdges.Where(e => e.FromNodeId == node.Id || e.ToNodeId == node.Id).ToList();
        foreach (var edge in connectedEdges)
        {
            GraphEdges.Remove(edge);
            GraphPattern.Edges.Remove(edge);
        }
        
        GraphNodes.Remove(node);
        GraphPattern.Nodes.Remove(node);
        UpdateGraphAql();
    }

    [RelayCommand]
    private void AddGraphEdge()
    {
        if (GraphNodes.Count < 2)
        {
            QueryResult = "Need at least 2 nodes to create an edge";
            return;
        }

        var fromNode = GraphNodes[GraphNodes.Count - 2];
        var toNode = GraphNodes[GraphNodes.Count - 1];
        
        var edge = new GraphEdge
        {
            Variable = $"edge{GraphEdges.Count + 1}",
            FromNodeId = fromNode.Id,
            ToNodeId = toNode.Id,
            EdgeType = "CONNECTED_TO",
            Label = "Edge"
        };
        GraphEdges.Add(edge);
        GraphPattern.Edges.Add(edge);
        UpdateGraphAql();
    }

    [RelayCommand]
    private void RemoveGraphEdge(GraphEdge edge)
    {
        GraphEdges.Remove(edge);
        GraphPattern.Edges.Remove(edge);
        UpdateGraphAql();
    }

    [RelayCommand]
    private void UpdateGraphAql()
    {
        GraphAql = GraphPattern.ToAql();
        if (!string.IsNullOrWhiteSpace(GraphAql))
        {
            GeneratedQuery = GraphAql + "\n  RETURN " + string.Join(", ", GraphNodes.Select(n => n.Variable));
        }
    }

    [RelayCommand]
    private void AddSampleGraphPattern()
    {
        // Clear existing
        GraphNodes.Clear();
        GraphEdges.Clear();
        GraphPattern.Nodes.Clear();
        GraphPattern.Edges.Clear();
        
        // Create sample pattern: User -[FOLLOWS]-> User -[LIKES]-> Product
        var user1 = new GraphNode
        {
            Variable = "user1",
            Collection = "users",
            X = 100,
            Y = 150,
            Label = "User"
        };
        user1.Properties.Add(new GraphNodeProperty
        {
            PropertyName = "name",
            Operator = FilterOperator.Equals,
            Value = "\"Alice\""
        });
        
        var user2 = new GraphNode
        {
            Variable = "user2",
            Collection = "users",
            X = 350,
            Y = 150,
            Label = "Friend"
        };
        
        var product = new GraphNode
        {
            Variable = "product",
            Collection = "products",
            X = 600,
            Y = 150,
            Label = "Product"
        };
        product.Properties.Add(new GraphNodeProperty
        {
            PropertyName = "category",
            Operator = FilterOperator.Equals,
            Value = "\"Books\""
        });
        
        var follows = new GraphEdge
        {
            Variable = "follows",
            Collection = "edges",
            FromNodeId = user1.Id,
            ToNodeId = user2.Id,
            EdgeType = "FOLLOWS",
            Direction = EdgeDirection.Outbound,
            Label = "FOLLOWS"
        };
        
        var likes = new GraphEdge
        {
            Variable = "likes",
            Collection = "edges",
            FromNodeId = user2.Id,
            ToNodeId = product.Id,
            EdgeType = "LIKES",
            Direction = EdgeDirection.Outbound,
            Label = "LIKES"
        };
        
        GraphNodes.Add(user1);
        GraphNodes.Add(user2);
        GraphNodes.Add(product);
        GraphEdges.Add(follows);
        GraphEdges.Add(likes);
        
        GraphPattern.Nodes.Add(user1);
        GraphPattern.Nodes.Add(user2);
        GraphPattern.Nodes.Add(product);
        GraphPattern.Edges.Add(follows);
        GraphPattern.Edges.Add(likes);
        
        UpdateGraphAql();
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
        GraphNodes.Clear();
        GraphEdges.Clear();
        GraphPattern = new GraphPattern();
        Query = new AqlQueryModel();
        GeneratedQuery = string.Empty;
        GraphAql = string.Empty;
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

    // ===== Phase 3: Vector Search Commands =====

    [RelayCommand]
    private void AddVectorFilter()
    {
        var filter = new VectorFilter
        {
            Field = "category",
            Operator = FilterOperator.Equals,
            Value = "\"technology\""
        };
        VectorMetadataFilters.Add(filter);
        VectorQuery.MetadataFilters.Add(filter);
    }

    [RelayCommand]
    private void RemoveVectorFilter(VectorFilter filter)
    {
        VectorMetadataFilters.Remove(filter);
        VectorQuery.MetadataFilters.Remove(filter);
    }

    [RelayCommand]
    private void AddVectorWeight()
    {
        var weight = new VectorWeight
        {
            VectorField = $"vector{VectorWeights.Count + 1}",
            ReferenceVector = "[]",
            Weight = 1.0,
            Label = $"Vector {VectorWeights.Count + 1}"
        };
        VectorWeights.Add(weight);
        VectorQuery.MultiVectorWeights.Add(weight);
    }

    [RelayCommand]
    private void RemoveVectorWeight(VectorWeight weight)
    {
        VectorWeights.Remove(weight);
        VectorQuery.MultiVectorWeights.Remove(weight);
    }

    [RelayCommand]
    private void UpdateVectorAql()
    {
        VectorAql = VectorQuery.ToAql();
        GeneratedQuery = VectorAql;
    }

    [RelayCommand]
    private void AddSampleVectorQuery()
    {
        // Clear existing
        VectorMetadataFilters.Clear();
        VectorWeights.Clear();
        VectorQuery.MetadataFilters.Clear();
        VectorQuery.MultiVectorWeights.Clear();

        // Set up sample similarity search
        VectorQuery.VectorCollection = "product_embeddings";
        VectorQuery.VectorField = "description_vector";
        VectorQuery.SearchMode = VectorSearchMode.Similarity;
        VectorQuery.DistanceMetric = DistanceMetric.Cosine;
        VectorQuery.TopK = 10;
        VectorQuery.SimilarityThreshold = 0.75;
        VectorQuery.UseThreshold = true;
        VectorQuery.HybridSearch = true;

        // Add reference vector (example 384-dim embedding, truncated for display)
        VectorQuery.ReferenceVector = "[0.1, 0.2, 0.3, ..., 0.384]";

        // Add metadata filter for hybrid search
        var filter = new VectorFilter
        {
            Field = "category",
            Operator = FilterOperator.Equals,
            Value = "\"electronics\""
        };
        VectorMetadataFilters.Add(filter);
        VectorQuery.MetadataFilters.Add(filter);

        var filter2 = new VectorFilter
        {
            Field = "price",
            Operator = FilterOperator.LessThan,
            Value = "1000"
        };
        VectorMetadataFilters.Add(filter2);
        VectorQuery.MetadataFilters.Add(filter2);

        // Generate AQL
        UpdateVectorAql();
    }

    [RelayCommand]
    private void AddSampleMultiVectorQuery()
    {
        // Clear existing
        VectorMetadataFilters.Clear();
        VectorWeights.Clear();
        VectorQuery.MetadataFilters.Clear();
        VectorQuery.MultiVectorWeights.Clear();

        // Set up multi-vector weighted search
        VectorQuery.VectorCollection = "documents";
        VectorQuery.SearchMode = VectorSearchMode.MultiVector;
        VectorQuery.DistanceMetric = DistanceMetric.Cosine;
        VectorQuery.TopK = 5;

        // Add multiple vectors with weights
        var weight1 = new VectorWeight
        {
            VectorField = "title_vector",
            ReferenceVector = "[0.5, 0.3, ...]",
            Weight = 0.6,
            Label = "Title (60%)"
        };
        VectorWeights.Add(weight1);
        VectorQuery.MultiVectorWeights.Add(weight1);

        var weight2 = new VectorWeight
        {
            VectorField = "content_vector",
            ReferenceVector = "[0.2, 0.4, ...]",
            Weight = 0.4,
            Label = "Content (40%)"
        };
        VectorWeights.Add(weight2);
        VectorQuery.MultiVectorWeights.Add(weight2);

        // Generate AQL
        UpdateVectorAql();
    }

    // ==================== Phase 4: Geo Query Commands ====================

    // Phase 4: Geo search support
    public ObservableCollection<GeoFilter> GeoMetadataFilters { get; } = new();
    
    [ObservableProperty]
    private GeoQuery _geoQuery = new();
    
    [ObservableProperty]
    private string _geoAql = string.Empty;

    public ObservableCollection<string> ShapeTypes { get; } = new()
    {
        "Point", "LineString", "Polygon", "Circle", "BoundingBox"
    };

    public ObservableCollection<string> SpatialOperators { get; } = new()
    {
        "Distance", "Within", "Contains", "Intersects", "Near"
    };

    public ObservableCollection<string> DistanceUnits { get; } = new()
    {
        "Meters", "Kilometers", "Miles", "Feet"
    };

    [RelayCommand]
    private void AddGeoFilter()
    {
        var filter = new GeoFilter
        {
            Field = "category",
            Operator = "==",
            Value = "\"restaurant\""
        };
        GeoMetadataFilters.Add(filter);
        GeoQuery.MetadataFilters.Add(filter);
        UpdateGeoAql();
    }

    [RelayCommand]
    private void RemoveGeoFilter(GeoFilter filter)
    {
        GeoMetadataFilters.Remove(filter);
        GeoQuery.MetadataFilters.Remove(filter);
        UpdateGeoAql();
    }

    [RelayCommand]
    private void UpdateGeoAql()
    {
        try
        {
            GeoAql = GeoQuery.ToAql();
        }
        catch (Exception ex)
        {
            GeoAql = $"// Error generating AQL: {ex.Message}";
        }
    }

    [RelayCommand]
    private void AddSamplePointSearch()
    {
        // Clear existing
        GeoMetadataFilters.Clear();
        GeoQuery.MetadataFilters.Clear();

        // Set up point search for nearby restaurants
        GeoQuery.GeoCollection = "stores";
        GeoQuery.GeoField = "location";
        GeoQuery.Shape = new GeoShape
        {
            Type = ShapeType.Point,
            Coordinates = "52.5200, 13.4050" // Berlin center
        };
        GeoQuery.Operator = SpatialOperator.Distance;
        GeoQuery.DistanceValue = 5;
        GeoQuery.DistanceUnit = DistanceUnit.Kilometers;
        GeoQuery.HybridSearch = true;

        // Add metadata filter
        var filter = new GeoFilter
        {
            Field = "category",
            Operator = "==",
            Value = "\"restaurant\""
        };
        GeoMetadataFilters.Add(filter);
        GeoQuery.MetadataFilters.Add(filter);

        // Generate AQL
        UpdateGeoAql();
    }

    [RelayCommand]
    private void AddSamplePolygonSearch()
    {
        // Clear existing
        GeoMetadataFilters.Clear();
        GeoQuery.MetadataFilters.Clear();

        // Set up polygon search for stores in district
        GeoQuery.GeoCollection = "stores";
        GeoQuery.GeoField = "location";
        GeoQuery.Shape = new GeoShape
        {
            Type = ShapeType.Polygon,
            Coordinates = "" // Will use default polygon
        };
        GeoQuery.Operator = SpatialOperator.Within;
        GeoQuery.HybridSearch = true;

        // Add metadata filter
        var filter = new GeoFilter
        {
            Field = "type",
            Operator = "==",
            Value = "\"retail\""
        };
        GeoMetadataFilters.Add(filter);
        GeoQuery.MetadataFilters.Add(filter);

        // Generate AQL
        UpdateGeoAql();
    }
}
