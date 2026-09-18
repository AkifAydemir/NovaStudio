using System.ComponentModel;
namespace NovaStudio.Models;

internal sealed class StackFrameRow : INotifyPropertyChanged
{
    private bool _isSelected;
    public required uint NativeFrameIndex { get; init; }
    public required uint FunctionIndex { get; init; }
    public required string Function { get; init; }
    public required string Range { get; init; }
    public required string State { get; init; }
    public bool IsSelected
    {
        get => _isSelected;
        set
        {
            if (_isSelected == value) return;
            _isSelected = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelected)));
        }
    }
    public event PropertyChangedEventHandler? PropertyChanged;
}
