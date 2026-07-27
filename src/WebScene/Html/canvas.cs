using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace WebScene;

public class canvas : Control
{
    public static readonly DirectProperty<canvas, string?> idProperty =
        NameProperty.AddOwner<canvas>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<canvas>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<canvas>();

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<canvas>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<canvas>();

    public static readonly StyledProperty<double> widthProperty =
        WidthProperty.AddOwner<canvas>();

    public static readonly StyledProperty<double> heightProperty =
        HeightProperty.AddOwner<canvas>();

    public static readonly StyledProperty<IBrush?> BackgroundProperty =
        AvaloniaProperty.Register<canvas, IBrush?>(nameof(Background));

    static canvas()
    {
        classProperty.Changed.AddClassHandler<canvas>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<canvas>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<canvas>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<canvas>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public canvas()
    {
        Focusable = true;
    }

    public string? @class
    {
        get => GetValue(classProperty);
        set => SetValue(classProperty, value);
    }

    public string? style
    {
        get => GetValue(styleProperty);
        set => SetValue(styleProperty, value);
    }

    public bool disabled
    {
        get => GetValue(disabledProperty);
        set => SetValue(disabledProperty, value);
    }

    public string? title
    {
        get => GetValue(titleProperty);
        set => SetValue(titleProperty, value);
    }

    public double width
    {
        get => GetValue(widthProperty);
        set => SetValue(widthProperty, value);
    }

    public double height
    {
        get => GetValue(heightProperty);
        set => SetValue(heightProperty, value);
    }

    public IBrush? Background
    {
        get => GetValue(BackgroundProperty);
        set => SetValue(BackgroundProperty, value);
    }

    public string? id
    {
        get => Name;
        set => Name = value;
    }

    // Drawing state
    private List<Stroke> _strokes = new();
    private List<Point> _currentPath = new();
    private Color _strokeColor = Colors.Black;
    private double _lineWidth = 2.0;
    private int _strokeMark = 0;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        // Fill background if provided
        if (Background is not null)
        {
            context.FillRectangle(Background, new Rect(Bounds.Size));
        }

        foreach (var s in _strokes)
        {
            if (s.Points.Count < 2)
                continue;

            var pen = new Pen(new SolidColorBrush(s.Color), s.LineWidth);
            for (int i = 1; i < s.Points.Count; i++)
            {
                context.DrawLine(pen, s.Points[i - 1], s.Points[i]);
            }
        }

        // Optionally draw current path preview (not stroked yet)
        if (_currentPath.Count >= 2)
        {
            var pen = new Pen(new SolidColorBrush(_strokeColor), _lineWidth);
            for (int i = 1; i < _currentPath.Count; i++)
            {
                context.DrawLine(pen, _currentPath[i - 1], _currentPath[i]);
            }
        }
    }

    // JavaScript API bridge
    public Canvas2DContext GetContext(string type)
    {
        return new Canvas2DContext(this);
    }

    public sealed class Canvas2DContext
    {
        private readonly canvas _owner;
        public Canvas2DContext(canvas owner) { _owner = owner; }

        public string strokeStyle
        {
            get => _owner._strokeColor.ToString();
            set
            {
                try
                {
                    _owner._strokeColor = Color.Parse(value);
                }
                catch { }
            }
        }

        public double lineWidth
        {
            get => _owner._lineWidth;
            set => _owner._lineWidth = value;
        }

        public void beginPath()
        {
            _owner._currentPath.Clear();
            _owner._strokeMark = 0;
        }

        public void moveTo(double x, double y)
        {
            _owner._currentPath.Add(new Point(x, y));
            _owner.InvalidateVisual();
        }

        public void lineTo(double x, double y)
        {
            _owner._currentPath.Add(new Point(x, y));
            _owner.InvalidateVisual();
        }

        public void stroke()
        {
            var count = _owner._currentPath.Count;
            if (count >= 2)
            {
                int start = _owner._strokeMark;
                if (start > 0) start -= 1; // ensure continuity
                if (start < 0) start = 0;
                if (start < count - 1)
                {
                    var segment = new List<Point>(count - start);
                    for (int i = start; i < count; i++) segment.Add(_owner._currentPath[i]);
                    _owner._strokes.Add(new Stroke
                    {
                        Color = _owner._strokeColor,
                        LineWidth = _owner._lineWidth,
                        Points = segment
                    });
                }
                _owner._strokeMark = count;
                _owner.InvalidateVisual();
            }
        }

        public void clearRect(double x, double y, double w, double h)
        {
            if (x <= 0 && y <= 0 && w >= _owner.Bounds.Width && h >= _owner.Bounds.Height)
            {
                _owner._strokes.Clear();
                _owner._currentPath.Clear();
                _owner.InvalidateVisual();
            }
        }
    }

    private sealed class Stroke
    {
        public Color Color { get; set; }
        public double LineWidth { get; set; }
        public List<Point> Points { get; set; } = new();
    }

    // Pointer events dispatch to JS engine via html host
    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        var p = e.GetPosition(this);
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        var p = e.GetPosition(this);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        var p = e.GetPosition(this);
    }
}
