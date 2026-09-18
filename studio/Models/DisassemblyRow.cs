using System.ComponentModel;
namespace NovaStudio.Models;

internal sealed class DisassemblyRow : INotifyPropertyChanged
{
    private bool _isCurrent;
    private bool _isBreakpoint;
    public required uint Address { get; init; }
    public required byte Opcode { get; init; }
    public required int Size { get; init; }
    public required string Bytes { get; init; }
    public required string Text { get; init; }
    public string AddressText => $"0x{Address:X8}";
    public bool IsCurrent
    {
        get => _isCurrent;
        set
        {
            if (_isCurrent == value) return;
            _isCurrent = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsCurrent)));
        }
    }
    public bool IsBreakpoint
    {
        get => _isBreakpoint;
        set
        {
            if (_isBreakpoint == value) return;
            _isBreakpoint = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsBreakpoint)));
        }
    }
    public event PropertyChangedEventHandler? PropertyChanged;
}
