using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using Themis.AqlQueryBuilder.Models;

namespace Themis.AqlQueryBuilder;

/// <summary>
/// Converts CollectionType to icon emoji
/// </summary>
public class CollectionTypeToIconConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is CollectionType type)
        {
            return type switch
            {
                CollectionType.Relational => "📊",
                CollectionType.Graph => "🕸️",
                CollectionType.Vector => "🔢",
                CollectionType.Geo => "📍",
                CollectionType.Hybrid => "🔀",
                _ => "📄"
            };
        }
        return "📄";
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}

/// <summary>
/// Converts FieldDataType to color for visual indication
/// </summary>
public class FieldTypeToColorConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is FieldDataType type)
        {
            return type switch
            {
                FieldDataType.String => new SolidColorBrush(Color.FromRgb(0, 122, 204)),      // Blue
                FieldDataType.Integer => new SolidColorBrush(Color.FromRgb(139, 92, 246)),    // Purple
                FieldDataType.Float => new SolidColorBrush(Color.FromRgb(249, 115, 22)),      // Orange
                FieldDataType.Boolean => new SolidColorBrush(Color.FromRgb(34, 197, 94)),     // Green
                FieldDataType.Date => new SolidColorBrush(Color.FromRgb(236, 72, 153)),       // Pink
                FieldDataType.DateTime => new SolidColorBrush(Color.FromRgb(236, 72, 153)),   // Pink
                FieldDataType.Vector => new SolidColorBrush(Color.FromRgb(249, 115, 22)),     // Orange
                FieldDataType.GeoPoint => new SolidColorBrush(Color.FromRgb(16, 185, 129)),   // Green
                FieldDataType.GeoPolygon => new SolidColorBrush(Color.FromRgb(16, 185, 129)), // Green
                FieldDataType.GeoLineString => new SolidColorBrush(Color.FromRgb(16, 185, 129)), // Green
                _ => new SolidColorBrush(Colors.Gray)
            };
        }
        return new SolidColorBrush(Colors.Gray);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
