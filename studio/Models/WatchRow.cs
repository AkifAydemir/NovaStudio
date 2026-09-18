using System.ComponentModel;
namespace NovaStudio.Models;

internal sealed class WatchRow : INotifyPropertyChanged
{
    private string _value = "—";
    public required string Name { get; init; }
    public required string Type { get; init; }
    public required uint RegisterIndex { get; init; }
    public required uint DeclarationLine { get; init; }
    public required uint FileId { get; init; }
    public required uint FunctionIndex { get; init; }
    public required bool IsParameter { get; init; }
    public required uint StorageKind { get; init; }
    public required uint ByteOffset { get; init; }
    public required uint ByteSize { get; init; }
    public required uint ElementCount { get; init; }
    public required uint LifetimeStart { get; init; }
    public required uint LifetimeEnd { get; init; }
    public required uint ScopeDepth { get; init; }
    public string Location => StorageKind == Interop.NovaCompilerNative.DebugStorageMemory
    ? $"[R{RegisterIndex}+0x{ByteOffset:X}]"
    : $"R{RegisterIndex}";
    public string Scope => IsParameter ? "param" : $"local · scope {ScopeDepth}";
    public string Value
    {
        get => _value;
        set
        {
            if (_value == value) return;
            _value = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Value)));
        }
    }
    public event PropertyChangedEventHandler? PropertyChanged;
}
