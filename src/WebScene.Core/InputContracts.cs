namespace WebScene.Core;

public enum WebScenePointerEventKind
{
    Pressed,
    Moved,
    Released,
    Wheel,
    Entered,
    Exited,
    Canceled
}

public enum WebScenePointerType
{
    Mouse,
    Touch,
    Pen,
    Unknown
}

public sealed class WebScenePointerInputEventArgs : EventArgs
{
    public required WebScenePointerEventKind Kind { get; init; }

    public required WebScenePointerType PointerType { get; init; }

    public required long PointerId { get; init; }

    public required WebScenePoint Position { get; init; }

    public WebScenePoint Delta { get; init; }

    public int Button { get; init; }

    public int Buttons { get; init; }

    public bool AltKey { get; init; }

    public bool ControlKey { get; init; }

    public bool MetaKey { get; init; }

    public bool ShiftKey { get; init; }

    public WebSceneBackendHandle SourceHandle { get; init; }

    public WebSceneBackendHandle NativeEventHandle { get; init; }

    public bool Handled { get; set; }
}

public sealed class WebSceneKeyboardInputEventArgs : EventArgs
{
    public required string Type { get; init; }

    public required string Key { get; init; }

    public string? Code { get; init; }

    public bool IsRepeat { get; init; }

    public bool AltKey { get; init; }

    public bool ControlKey { get; init; }

    public bool MetaKey { get; init; }

    public bool ShiftKey { get; init; }

    public WebSceneBackendHandle SourceHandle { get; init; }

    public WebSceneBackendHandle NativeEventHandle { get; init; }

    public bool Handled { get; set; }
}

public sealed class WebSceneTextInputEventArgs : EventArgs
{
    public required string Text { get; init; }

    public WebSceneBackendHandle SourceHandle { get; init; }

    public WebSceneBackendHandle NativeEventHandle { get; init; }

    public bool Handled { get; set; }
}

public interface IWebSceneInputSource
{
    event EventHandler<WebScenePointerInputEventArgs>? Pointer;

    event EventHandler<WebSceneKeyboardInputEventArgs>? Keyboard;

    event EventHandler<WebSceneTextInputEventArgs>? TextInput;
}
