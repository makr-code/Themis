using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Themis.AqlQueryBuilder.Models;

/// <summary>
/// Represents a vector similarity search query
/// </summary>
public partial class VectorQuery : ObservableObject
{
    [ObservableProperty]
    private string _vectorCollection = "embeddings";

    [ObservableProperty]
    private string _vectorField = "vector";

    [ObservableProperty]
    private VectorSearchMode _searchMode = VectorSearchMode.Similarity;

    [ObservableProperty]
    private DistanceMetric _distanceMetric = DistanceMetric.Cosine;

    [ObservableProperty]
    private string _referenceVector = "";

    [ObservableProperty]
    private string _referenceItemId = "";

    [ObservableProperty]
    private int _topK = 10;

    [ObservableProperty]
    private double _similarityThreshold = 0.7;

    [ObservableProperty]
    private bool _useThreshold = false;

    [ObservableProperty]
    private bool _hybridSearch = false;

    public ObservableCollection<VectorFilter> MetadataFilters { get; } = new();

    public ObservableCollection<VectorWeight> MultiVectorWeights { get; } = new();

    /// <summary>
    /// Generates AQL query for vector similarity search
    /// </summary>
    public string ToAql()
    {
        var sb = new StringBuilder();

        // Start with FOR clause
        sb.AppendLine($"FOR doc IN {VectorCollection}");

        // Add metadata filters for hybrid search
        if (HybridSearch && MetadataFilters.Count > 0)
        {
            foreach (var filter in MetadataFilters)
            {
                sb.AppendLine($"  FILTER doc.{filter.Field} {GetOperatorSymbol(filter.Operator)} {filter.Value}");
            }
        }

        // Vector similarity search
        if (SearchMode == VectorSearchMode.Similarity)
        {
            // Build vector expression
            string vectorExpr;
            if (!string.IsNullOrWhiteSpace(ReferenceItemId))
            {
                vectorExpr = $"(FOR ref IN {VectorCollection} FILTER ref._id == \"{ReferenceItemId}\" RETURN ref.{VectorField})[0]";
            }
            else if (!string.IsNullOrWhiteSpace(ReferenceVector))
            {
                vectorExpr = ReferenceVector;
            }
            else
            {
                vectorExpr = "[]"; // Empty vector as placeholder
            }

            // Calculate distance
            string distanceFunction = DistanceMetric switch
            {
                DistanceMetric.Cosine => "COSINE_SIMILARITY",
                DistanceMetric.Euclidean => "L2_DISTANCE",
                DistanceMetric.DotProduct => "DOT_PRODUCT",
                DistanceMetric.Manhattan => "L1_DISTANCE",
                _ => "COSINE_SIMILARITY"
            };

            sb.AppendLine($"  LET similarity = {distanceFunction}(doc.{VectorField}, {vectorExpr})");

            // Apply threshold if enabled
            if (UseThreshold)
            {
                string thresholdOp = (DistanceMetric == DistanceMetric.Euclidean || DistanceMetric == DistanceMetric.Manhattan) 
                    ? "<" : ">=";
                sb.AppendLine($"  FILTER similarity {thresholdOp} {SimilarityThreshold}");
            }

            // Sort by similarity
            string sortOrder = (DistanceMetric == DistanceMetric.Euclidean || DistanceMetric == DistanceMetric.Manhattan)
                ? "ASC" : "DESC";
            sb.AppendLine($"  SORT similarity {sortOrder}");

            // Limit results
            sb.AppendLine($"  LIMIT {TopK}");

            // Return with similarity score
            sb.AppendLine("  RETURN {doc, similarity}");
        }
        else if (SearchMode == VectorSearchMode.MultiVector)
        {
            // Multi-vector weighted search
            var weightExprs = MultiVectorWeights
                .Select(w => $"({w.Weight} * {GetDistanceMetricFunction(DistanceMetric)}(doc.{w.VectorField}, {w.ReferenceVector}))")
                .ToList();

            if (weightExprs.Count > 0)
            {
                sb.AppendLine($"  LET combinedScore = {string.Join(" + ", weightExprs)}");
                sb.AppendLine($"  SORT combinedScore DESC");
                sb.AppendLine($"  LIMIT {TopK}");
                sb.AppendLine("  RETURN {doc, combinedScore}");
            }
            else
            {
                sb.AppendLine("  RETURN doc");
            }
        }
        else // Range search
        {
            sb.AppendLine("  RETURN doc");
        }

        return sb.ToString().TrimEnd();
    }

    private string GetOperatorSymbol(FilterOperator op) => op switch
    {
        FilterOperator.Equals => "==",
        FilterOperator.NotEquals => "!=",
        FilterOperator.GreaterThan => ">",
        FilterOperator.LessThan => "<",
        FilterOperator.GreaterThanOrEqual => ">=",
        FilterOperator.LessThanOrEqual => "<=",
        FilterOperator.Contains => "CONTAINS",
        FilterOperator.StartsWith => "STARTS_WITH",
        FilterOperator.In => "IN",
        _ => "=="
    };

    private string GetDistanceMetricFunction(DistanceMetric metric) => metric switch
    {
        DistanceMetric.Cosine => "COSINE_SIMILARITY",
        DistanceMetric.Euclidean => "L2_DISTANCE",
        DistanceMetric.DotProduct => "DOT_PRODUCT",
        DistanceMetric.Manhattan => "L1_DISTANCE",
        _ => "COSINE_SIMILARITY"
    };
}

/// <summary>
/// Vector search modes
/// </summary>
public enum VectorSearchMode
{
    Similarity,    // Standard k-NN similarity search
    MultiVector,   // Multi-vector weighted search
    Range          // Range-based search
}

/// <summary>
/// Distance metrics for vector similarity
/// </summary>
public enum DistanceMetric
{
    Cosine,        // Cosine similarity (good for normalized vectors)
    Euclidean,     // L2 distance (Euclidean distance)
    DotProduct,    // Dot product similarity
    Manhattan      // L1 distance (Manhattan distance)
}

/// <summary>
/// Metadata filter for hybrid search
/// </summary>
public partial class VectorFilter : ObservableObject
{
    [ObservableProperty]
    private string _field = "";

    [ObservableProperty]
    private FilterOperator _operator = FilterOperator.Equals;

    [ObservableProperty]
    private string _value = "";
}

/// <summary>
/// Weight for multi-vector search
/// </summary>
public partial class VectorWeight : ObservableObject
{
    [ObservableProperty]
    private string _vectorField = "vector";

    [ObservableProperty]
    private string _referenceVector = "[]";

    [ObservableProperty]
    private double _weight = 1.0;

    [ObservableProperty]
    private string _label = "Vector 1";
}
