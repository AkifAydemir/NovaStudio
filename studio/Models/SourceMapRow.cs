using System.ComponentModel;
namespace NovaStudio.Models;

internal sealed class SourceMapRow : INotifyPropertyChanged
{
    private bool _isCurrent;
    private bool _isBreakpoint;
    public required uint FileId { get; init; }
    public required string FileName { get; init; }
    public required uint Line { get; init; }
    public required uint Column { get; init; }
    public required uint StartAddress { get; init; }
    public required uint EndAddress { get; init; }
    public required uint StatementKind { get; init; }
    public required uint FunctionIndex { get; init; }
    public required string SourceText { get; init; }
    public string FileText => System.IO.Path.GetFileName(FileName);
    public string LineText => Line.ToString();
    public string LocationText => $"{FileText}:{Line}";
    public string AddressText => $"0x{StartAddress:X8}";
    public string RangeText => $"0x{StartAddress:X8}–0x{EndAddress:X8}";
    public string KindText => StatementKind switch
    {
        1 => "decl",
        2 => "assign",
        3 => "if",
        4 => "while",
        5 => "return",
        6 => "expr",
        7 => "for",
        8 => "break",
        9 => "continue",
        10 => "switch",
        11 => "case",
        12 => "default",
        13 => "do-while",
        14 => "label",
        15 => "goto",
        _ => "stmt"
    };
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
