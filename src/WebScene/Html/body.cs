using System;
using Avalonia;
using Avalonia.Controls;

namespace WebScene;

public class body : DockPanel
{
    protected override Type StyleKeyOverride => typeof(DockPanel);

    public static readonly DirectProperty<body, string?> idProperty =
        NameProperty.AddOwner<body>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<double> widthProperty =
        WidthProperty.AddOwner<body>();

    public static readonly StyledProperty<double> heightProperty =
        HeightProperty.AddOwner<body>();

    public static readonly StyledProperty<string?> classProperty =
        HtmlElementBase.classProperty.AddOwner<body>();

    public static readonly StyledProperty<string?> styleProperty =
        HtmlElementBase.styleProperty.AddOwner<body>();

    public static readonly StyledProperty<string?> scrollProperty =
        AvaloniaProperty.Register<body, string?>(nameof(scroll));

    public static readonly StyledProperty<bool> disabledProperty =
        HtmlElementBase.disabledProperty.AddOwner<body>();

    public static readonly StyledProperty<string?> titleProperty =
        HtmlElementBase.titleProperty.AddOwner<body>();

    static body()
    {
        classProperty.Changed.AddClassHandler<body>((o, e) => HtmlElementBase.ApplyClasses(o, e.NewValue as string));
        styleProperty.Changed.AddClassHandler<body>((o, e) => HtmlElementBase.ApplyStyles(o, e.NewValue as string));
        disabledProperty.Changed.AddClassHandler<body>((o, e) => HtmlElementBase.ApplyDisabled(o, e.NewValue is bool b && b));
        titleProperty.Changed.AddClassHandler<body>((o, e) => HtmlElementBase.ApplyTitle(o, e.NewValue as string));
    }

    public double width
    {
        get { return GetValue(widthProperty); }
        set { SetValue(widthProperty, value); }
    }

    public double height
    {
        get { return GetValue(heightProperty); }
        set { SetValue(heightProperty, value); }
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

    public string? scroll
    {
        get => GetValue(scrollProperty);
        set => SetValue(scrollProperty, value);
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

    public string? id
    {
        get => Name;
        set => Name = value;
    }
}
