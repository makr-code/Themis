using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace Themis.AqlQueryBuilder.Services;

/// <summary>
/// Implementation of help service with context-sensitive help
/// </summary>
public class HelpService : IHelpService
{
    private readonly Dictionary<string, string> _helpContent;
    private readonly Dictionary<string, string> _shortcuts;

    public HelpService()
    {
        _helpContent = new Dictionary<string, string>
        {
            ["FOR"] = "FOR clause iterates over a collection. Syntax: FOR variable IN collection",
            ["FILTER"] = "FILTER clause filters documents based on conditions. Supports AND, OR, NOT operators.",
            ["SORT"] = "SORT clause orders results. Use ASC for ascending, DESC for descending.",
            ["LIMIT"] = "LIMIT clause restricts the number of results. Syntax: LIMIT count or LIMIT offset, count",
            ["RETURN"] = "RETURN clause specifies what to return from the query.",
            ["COLLECT"] = "COLLECT clause groups results. Supports GROUP BY and AGGREGATE functions.",
            ["Graph"] = "Graph queries traverse relationships between nodes using edges.",
            ["Vector"] = "Vector similarity search finds similar items using embeddings and distance metrics.",
            ["Geo"] = "Geo spatial queries search locations using shapes and spatial operators."
        };

        _shortcuts = new Dictionary<string, string>
        {
            ["Copy"] = "Ctrl+C",
            ["Paste"] = "Ctrl+V",
            ["Delete"] = "Del",
            ["Duplicate"] = "Ctrl+D",
            ["Undo"] = "Ctrl+Z",
            ["Redo"] = "Ctrl+Y",
            ["Execute"] = "F5",
            ["Help"] = "F1",
            ["ZoomIn"] = "Ctrl+Plus",
            ["ZoomOut"] = "Ctrl+Minus",
            ["ResetZoom"] = "Ctrl+0",
            ["FitToView"] = "Ctrl+Shift+F"
        };
    }

    public string GetContextHelp(string context)
    {
        return _helpContent.TryGetValue(context, out string? help) 
            ? help 
            : $"No help available for {context}";
    }

    public void ShowTutorial(string tutorialId)
    {
        // Tutorial overlay would be shown here
        // For now, just show a message box
        MessageBox.Show($"Tutorial: {tutorialId}\n\nThis feature will show an interactive walkthrough.", 
            "Tutorial", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    public ToolTip CreateRichToolTip(string title, string content, string? shortcut = null)
    {
        var tooltip = new ToolTip();
        var stackPanel = new StackPanel();

        // Title
        var titleBlock = new TextBlock
        {
            Text = title,
            FontWeight = FontWeights.Bold,
            FontSize = 14,
            Margin = new Thickness(0, 0, 0, 5)
        };
        stackPanel.Children.Add(titleBlock);

        // Content
        var contentBlock = new TextBlock
        {
            Text = content,
            TextWrapping = TextWrapping.Wrap,
            MaxWidth = 300
        };
        stackPanel.Children.Add(contentBlock);

        // Shortcut
        if (!string.IsNullOrEmpty(shortcut))
        {
            var shortcutBlock = new TextBlock
            {
                Text = $"Shortcut: {shortcut}",
                FontStyle = FontStyles.Italic,
                Foreground = Brushes.Gray,
                Margin = new Thickness(0, 5, 0, 0)
            };
            stackPanel.Children.Add(shortcutBlock);
        }

        tooltip.Content = stackPanel;
        return tooltip;
    }

    public string GetShortcut(string action)
    {
        return _shortcuts.TryGetValue(action, out string? shortcut) 
            ? shortcut 
            : string.Empty;
    }
}
