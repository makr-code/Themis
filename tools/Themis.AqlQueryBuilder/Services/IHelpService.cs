using System.Windows.Controls;

namespace Themis.AqlQueryBuilder.Services;

/// <summary>
/// Service for providing context-sensitive help and tutorials
/// </summary>
public interface IHelpService
{
    /// <summary>
    /// Get help content for a specific context
    /// </summary>
    string GetContextHelp(string context);

    /// <summary>
    /// Show tutorial overlay for a specific feature
    /// </summary>
    void ShowTutorial(string tutorialId);

    /// <summary>
    /// Create a rich tooltip with title and content
    /// </summary>
    ToolTip CreateRichToolTip(string title, string content, string? shortcut = null);

    /// <summary>
    /// Get keyboard shortcut for an action
    /// </summary>
    string GetShortcut(string action);
}
