using WebScene.Dom;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomDocumentCoreTests
{
    [Fact]
    public void TypedDocumentStatePreservesActiveElementIdentity()
    {
        var document = new RecordingDocument();
        var first = new RecordingElement();
        var second = new RecordingElement();

        Assert.Null(document.ActiveElement);

        document.ActiveElement = first;
        Assert.Same(first, document.ActiveElement);

        document.ActiveElement = second;
        Assert.Same(second, document.ActiveElement);

        document.ActiveElement = null;
        Assert.Null(document.ActiveElement);
    }

    private sealed class RecordingDocument : DomDocumentCore<RecordingElement>
    {
        public RecordingElement? ActiveElement
        {
            get => _virtualActiveElement;
            set => _virtualActiveElement = value;
        }
    }

    private sealed class RecordingElement : DomElementCore
    {
        public RecordingElement()
            : base(default)
        {
        }
    }
}
