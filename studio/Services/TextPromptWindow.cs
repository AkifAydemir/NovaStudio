using System.Windows;
using System.Windows.Controls;
namespace NovaStudio.Services;

internal sealed class TextPromptWindow : Window
{
    private readonly TextBox _textBox;
    private TextPromptWindow(string title, string label, string initialValue, Window owner)
    {
        Title = title;
        Owner = owner;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ResizeMode = ResizeMode.NoResize;
        SizeToContent = SizeToContent.WidthAndHeight;
        Background = System.Windows.Media.Brushes.White;
        _textBox = new TextBox { Text = initialValue, MinWidth = 360, Margin = new Thickness(0, 8, 0, 12) };
        _textBox.SelectAll();
        var ok = new Button { Content = "OK", IsDefault = true, MinWidth = 80, Margin = new Thickness(6, 0, 0, 0) };
        ok.Click += (_, _) => { DialogResult = true; Close(); };
        var cancel = new Button { Content = "Cancel", IsCancel = true, MinWidth = 80, Margin = new Thickness(6, 0, 0, 0) };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
        buttons.Children.Add(cancel);
        buttons.Children.Add(ok);
        var panel = new StackPanel { Margin = new Thickness(18) };
        panel.Children.Add(new TextBlock { Text = label });
        panel.Children.Add(_textBox);
        panel.Children.Add(buttons);
        Content = panel;
        Loaded += (_, _) => _textBox.Focus();
    }
    internal static string? Show(Window owner, string title, string label, string initialValue = "")
    {
        var dialog = new TextPromptWindow(title, label, initialValue, owner);
        return dialog.ShowDialog() == true ? dialog._textBox.Text.Trim() : null;
    }
}
