namespace Themis.AqlQueryBuilder.Models;

/// <summary>
/// Represents a graph node in the visual pattern builder
/// </summary>
public class GraphNode
{
    public string Id { get; set; } = Guid.NewGuid().ToString();
    public string Variable { get; set; } = "node";
    public string Collection { get; set; } = string.Empty;
    public List<GraphNodeProperty> Properties { get; set; } = new();
    
    // UI positioning
    public double X { get; set; }
    public double Y { get; set; }
    
    // Visual properties
    public string Label { get; set; } = string.Empty;
    public string Color { get; set; } = "#007ACC";
}

/// <summary>
/// Represents a graph edge/relationship in the visual pattern builder
/// </summary>
public class GraphEdge
{
    public string Id { get; set; } = Guid.NewGuid().ToString();
    public string Variable { get; set; } = "edge";
    public string Collection { get; set; } = "edges";
    public string FromNodeId { get; set; } = string.Empty;
    public string ToNodeId { get; set; } = string.Empty;
    public string EdgeType { get; set; } = string.Empty;
    public EdgeDirection Direction { get; set; } = EdgeDirection.Outbound;
    public List<GraphNodeProperty> Properties { get; set; } = new();
    
    // Visual properties
    public string Label { get; set; } = string.Empty;
    public string Color { get; set; } = "#8B5CF6";
}

/// <summary>
/// Property/condition on a graph node or edge
/// </summary>
public class GraphNodeProperty
{
    public string PropertyName { get; set; } = string.Empty;
    public FilterOperator Operator { get; set; } = FilterOperator.Equals;
    public string Value { get; set; } = string.Empty;
}

/// <summary>
/// Edge direction for graph traversal
/// </summary>
public enum EdgeDirection
{
    Outbound,   // ->
    Inbound,    // <-
    Any         // <>
}

/// <summary>
/// Graph traversal pattern
/// </summary>
public class GraphPattern
{
    public List<GraphNode> Nodes { get; set; } = new();
    public List<GraphEdge> Edges { get; set; } = new();
    public int MinDepth { get; set; } = 1;
    public int MaxDepth { get; set; } = 1;
    
    /// <summary>
    /// Generates AQL for the graph pattern
    /// </summary>
    public string ToAql()
    {
        var aql = new List<string>();
        
        // For each edge, generate traversal logic
        foreach (var edge in Edges)
        {
            var fromNode = Nodes.FirstOrDefault(n => n.Id == edge.FromNodeId);
            var toNode = Nodes.FirstOrDefault(n => n.Id == edge.ToNodeId);
            
            if (fromNode == null || toNode == null) continue;
            
            // Start with FROM node
            if (!aql.Any())
            {
                aql.Add($"FOR {fromNode.Variable} IN {fromNode.Collection}");
                
                // Add node filters
                foreach (var prop in fromNode.Properties)
                {
                    var condition = prop.Operator switch
                    {
                        FilterOperator.Equals => $"{fromNode.Variable}.{prop.PropertyName} == {prop.Value}",
                        FilterOperator.NotEquals => $"{fromNode.Variable}.{prop.PropertyName} != {prop.Value}",
                        FilterOperator.GreaterThan => $"{fromNode.Variable}.{prop.PropertyName} > {prop.Value}",
                        FilterOperator.LessThan => $"{fromNode.Variable}.{prop.PropertyName} < {prop.Value}",
                        _ => $"{fromNode.Variable}.{prop.PropertyName} == {prop.Value}"
                    };
                    aql.Add($"  FILTER {condition}");
                }
            }
            
            // Add edge traversal
            aql.Add($"  FOR {edge.Variable} IN {edge.Collection}");
            
            // Add edge direction filter
            var directionFilter = edge.Direction switch
            {
                EdgeDirection.Outbound => $"{edge.Variable}._from == {fromNode.Variable}._id",
                EdgeDirection.Inbound => $"{edge.Variable}._to == {fromNode.Variable}._id",
                EdgeDirection.Any => $"({edge.Variable}._from == {fromNode.Variable}._id OR {edge.Variable}._to == {fromNode.Variable}._id)",
                _ => $"{edge.Variable}._from == {fromNode.Variable}._id"
            };
            aql.Add($"    FILTER {directionFilter}");
            
            // Add edge type filter if specified
            if (!string.IsNullOrWhiteSpace(edge.EdgeType))
            {
                aql.Add($"    FILTER {edge.Variable}._type == \"{edge.EdgeType}\"");
            }
            
            // Add edge property filters
            foreach (var prop in edge.Properties)
            {
                var condition = prop.Operator switch
                {
                    FilterOperator.Equals => $"{edge.Variable}.{prop.PropertyName} == {prop.Value}",
                    FilterOperator.NotEquals => $"{edge.Variable}.{prop.PropertyName} != {prop.Value}",
                    _ => $"{edge.Variable}.{prop.PropertyName} == {prop.Value}"
                };
                aql.Add($"    FILTER {condition}");
            }
            
            // Add TO node
            aql.Add($"    FOR {toNode.Variable} IN {toNode.Collection}");
            
            var toNodeFilter = edge.Direction switch
            {
                EdgeDirection.Outbound => $"{toNode.Variable}._id == {edge.Variable}._to",
                EdgeDirection.Inbound => $"{toNode.Variable}._id == {edge.Variable}._from",
                EdgeDirection.Any => $"({toNode.Variable}._id == {edge.Variable}._to OR {toNode.Variable}._id == {edge.Variable}._from)",
                _ => $"{toNode.Variable}._id == {edge.Variable}._to"
            };
            aql.Add($"      FILTER {toNodeFilter}");
            
            // Add to-node property filters
            foreach (var prop in toNode.Properties)
            {
                var condition = prop.Operator switch
                {
                    FilterOperator.Equals => $"{toNode.Variable}.{prop.PropertyName} == {prop.Value}",
                    FilterOperator.NotEquals => $"{toNode.Variable}.{prop.PropertyName} != {prop.Value}",
                    _ => $"{toNode.Variable}.{prop.PropertyName} == {prop.Value}"
                };
                aql.Add($"      FILTER {condition}");
            }
        }
        
        return string.Join("\n", aql);
    }
}
