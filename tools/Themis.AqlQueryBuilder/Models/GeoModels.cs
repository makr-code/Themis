using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;

namespace Themis.AqlQueryBuilder.Models
{
    /// <summary>
    /// Represents a geo-spatial query for location-based searches
    /// </summary>
    public class GeoQuery
    {
        public string GeoCollection { get; set; } = "stores";
        public string GeoField { get; set; } = "location";
        public GeoShape Shape { get; set; } = new GeoShape();
        public SpatialOperator Operator { get; set; } = SpatialOperator.Distance;
        public double DistanceValue { get; set; } = 5000; // meters
        public DistanceUnit DistanceUnit { get; set; } = DistanceUnit.Meters;
        public bool HybridSearch { get; set; } = false;
        public ObservableCollection<GeoFilter> MetadataFilters { get; set; } = new ObservableCollection<GeoFilter>();

        /// <summary>
        /// Converts the geo query to AQL syntax
        /// </summary>
        public string ToAql()
        {
            var sb = new StringBuilder();

            // FOR clause
            sb.AppendLine($"FOR doc IN {GeoCollection}");

            // Generate spatial filter based on operator
            switch (Operator)
            {
                case SpatialOperator.Distance:
                case SpatialOperator.Near:
                    {
                        var distanceInMeters = ConvertToMeters(DistanceValue, DistanceUnit);
                        var geoPoint = Shape.ToGeoPoint();
                        sb.AppendLine($"  FILTER GEO_DISTANCE(doc.{GeoField}, {geoPoint}) <= {distanceInMeters}");
                    }
                    break;

                case SpatialOperator.Within:
                    {
                        var geoShape = Shape.ToGeoShape();
                        sb.AppendLine($"  FILTER GEO_CONTAINS({geoShape}, doc.{GeoField})");
                    }
                    break;

                case SpatialOperator.Contains:
                    {
                        var geoShape = Shape.ToGeoShape();
                        sb.AppendLine($"  FILTER GEO_CONTAINS(doc.{GeoField}, {geoShape})");
                    }
                    break;

                case SpatialOperator.Intersects:
                    {
                        var geoShape = Shape.ToGeoShape();
                        sb.AppendLine($"  FILTER GEO_INTERSECTS(doc.{GeoField}, {geoShape})");
                    }
                    break;
            }

            // Hybrid search metadata filters
            if (HybridSearch && MetadataFilters.Any())
            {
                foreach (var filter in MetadataFilters)
                {
                    sb.AppendLine($"  FILTER doc.{filter.Field} {filter.Operator} {filter.Value}");
                }
            }

            // For distance-based queries, calculate distance
            if (Operator == SpatialOperator.Distance || Operator == SpatialOperator.Near)
            {
                var geoPoint = Shape.ToGeoPoint();
                sb.AppendLine($"  LET distance = GEO_DISTANCE(doc.{GeoField}, {geoPoint})");
                sb.AppendLine("  SORT distance ASC");
            }

            // RETURN clause
            if (Operator == SpatialOperator.Distance || Operator == SpatialOperator.Near)
            {
                sb.AppendLine("  RETURN {doc, distance}");
            }
            else
            {
                sb.AppendLine("  RETURN doc");
            }

            return sb.ToString().TrimEnd();
        }

        private double ConvertToMeters(double value, DistanceUnit unit)
        {
            return unit switch
            {
                DistanceUnit.Meters => value,
                DistanceUnit.Kilometers => value * 1000,
                DistanceUnit.Miles => value * 1609.34,
                DistanceUnit.Feet => value * 0.3048,
                _ => value
            };
        }
    }

    /// <summary>
    /// Represents a geometric shape for spatial queries
    /// </summary>
    public class GeoShape
    {
        public ShapeType Type { get; set; } = ShapeType.Point;
        public string Coordinates { get; set; } = "52.5200, 13.4050"; // Berlin
        public string GeoJsonInput { get; set; } = "";
        public string WktInput { get; set; } = "";
        public double Radius { get; set; } = 1000; // For circle

        /// <summary>
        /// Converts shape to GEO_POINT for distance queries
        /// </summary>
        public string ToGeoPoint()
        {
            if (!string.IsNullOrWhiteSpace(GeoJsonInput))
            {
                // Parse GeoJSON if provided
                return GeoJsonInput;
            }
            else if (!string.IsNullOrWhiteSpace(WktInput))
            {
                // Parse WKT if provided
                return WktInput;
            }
            else
            {
                // Parse coordinates
                var coords = Coordinates.Split(',').Select(c => c.Trim()).ToArray();
                if (coords.Length >= 2)
                {
                    var lat = coords[0];
                    var lon = coords[1];
                    return $"GEO_POINT({lon}, {lat})";
                }
            }

            return "GEO_POINT(0, 0)";
        }

        /// <summary>
        /// Converts shape to GEO_POLYGON or other geo shape function
        /// </summary>
        public string ToGeoShape()
        {
            switch (Type)
            {
                case ShapeType.Point:
                    return ToGeoPoint();

                case ShapeType.Polygon:
                    if (!string.IsNullOrWhiteSpace(GeoJsonInput))
                    {
                        return GeoJsonInput;
                    }
                    else if (!string.IsNullOrWhiteSpace(WktInput))
                    {
                        return WktInput;
                    }
                    else
                    {
                        // Example polygon (Berlin district)
                        return "GEO_POLYGON([[[13.3, 52.5], [13.5, 52.5], [13.5, 52.6], [13.3, 52.6], [13.3, 52.5]]])";
                    }

                case ShapeType.Circle:
                    {
                        var point = ToGeoPoint();
                        return $"GEO_CIRCLE({point}, {Radius})";
                    }

                case ShapeType.BoundingBox:
                    {
                        // Parse bounding box coordinates
                        var coords = Coordinates.Split(',').Select(c => c.Trim()).ToArray();
                        if (coords.Length >= 4)
                        {
                            var minLat = coords[0];
                            var minLon = coords[1];
                            var maxLat = coords[2];
                            var maxLon = coords[3];
                            return $"GEO_POLYGON([[[{minLon}, {minLat}], [{maxLon}, {minLat}], [{maxLon}, {maxLat}], [{minLon}, {maxLat}], [{minLon}, {minLat}]]])";
                        }
                        return "GEO_POLYGON([[[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]]])";
                    }

                case ShapeType.LineString:
                    {
                        if (!string.IsNullOrWhiteSpace(GeoJsonInput))
                        {
                            return GeoJsonInput;
                        }
                        return "GEO_LINESTRING([[13.3, 52.5], [13.4, 52.5], [13.5, 52.5]])";
                    }

                default:
                    return ToGeoPoint();
            }
        }
    }

    /// <summary>
    /// Represents a metadata filter for hybrid geo search
    /// </summary>
    public class GeoFilter
    {
        public string Field { get; set; } = "category";
        public string Operator { get; set; } = "==";
        public string Value { get; set; } = "\"restaurant\"";
    }

    /// <summary>
    /// Types of geometric shapes
    /// </summary>
    public enum ShapeType
    {
        Point,        // Single point (lat, lon)
        LineString,   // Connected line segments
        Polygon,      // Closed area
        Circle,       // Point with radius
        BoundingBox   // Rectangle defined by min/max coordinates
    }

    /// <summary>
    /// Spatial query operators
    /// </summary>
    public enum SpatialOperator
    {
        Within,      // Geometry is within the shape
        Contains,    // Shape contains the geometry
        Intersects,  // Geometries overlap
        Near,        // Within distance (synonym for Distance)
        Distance     // Distance-based filtering with radius
    }

    /// <summary>
    /// Distance measurement units
    /// </summary>
    public enum DistanceUnit
    {
        Meters,
        Kilometers,
        Miles,
        Feet
    }
}
