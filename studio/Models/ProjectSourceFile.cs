using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
namespace NovaStudio.Models;

internal sealed class ProjectSourceFile : INotifyPropertyChanged
{
    private string _name;
    private string _text;
    private string _savedText;
    public ProjectSourceFile(uint fileId, string name, string text)
    {
        FileId = fileId;
        _name = NormalizeName(name);
        _text = text;
        _savedText = text;
    }
    public uint FileId { get; private set; }
    public string Name
    {
        get => _name;
        private set
        {
            if (_name == value) return;
            _name = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(DisplayName));
            OnPropertyChanged(nameof(IsHeader));
            OnPropertyChanged(nameof(KindLabel));
        }
    }
    public string Text
    {
        get => _text;
        set
        {
            if (_text == value) return;
            _text = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsDirty));
            OnPropertyChanged(nameof(DisplayName));
        }
    }
    public bool IsHeader => Name.EndsWith(".h", StringComparison.OrdinalIgnoreCase);
    public string KindLabel => IsHeader ? "H" : "C";
    public string DisplayName => $"{(IsDirty ? "● " : string.Empty)}[{KindLabel}] {Name}";
    public string NodeId => $"source:{Name}";
    public bool IsDirty => !string.Equals(_text, _savedText, StringComparison.Ordinal);
    public void MarkSaved()
    {
        _savedText = _text;
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(DisplayName));

    }
    public void Rename(string newName)
    {
        Name = NormalizeName(newName);
    }
    public void ReassignFileId(uint fileId)
    {
        if (FileId == fileId) return;
        FileId = fileId;
        OnPropertyChanged(nameof(FileId));
        OnPropertyChanged(nameof(NodeId));
    }
    private static string NormalizeName(string value)
    => value.Replace('\\', '/').TrimStart('/');
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    public event PropertyChangedEventHandler? PropertyChanged;
}
