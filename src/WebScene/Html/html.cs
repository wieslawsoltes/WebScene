using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Metadata;
using Avalonia.Markup.Xaml;
using Avalonia.Styling;
using Avalonia.Platform;

namespace WebScene;

public class html : Window
{
    protected override Type StyleKeyOverride => typeof(Window);

    public static readonly StyledProperty<content> contentProperty =
        AvaloniaProperty.Register<html, content>(nameof(content));
    
    public static readonly DirectProperty<html, string?> nameProperty =
        Window.NameProperty.AddOwner<html>(o => o.Name, (o, v) => o.Name = v);

    public static readonly StyledProperty<double> widthProperty =
        Window.WidthProperty.AddOwner<html>();

    public static readonly StyledProperty<double> heightProperty =
        Window.HeightProperty.AddOwner<html>();

    public static readonly StyledProperty<string?> titleProperty =
        Window.TitleProperty.AddOwner<html>();
    
    public static readonly StyledProperty<head?> headProperty =
        AvaloniaProperty.Register<html, head?>(nameof(head));

    public static readonly StyledProperty<body?> bodyProperty =
        AvaloniaProperty.Register<html, body?>(nameof(body));
    
    [Content]
    public content content { get; } = new content();

    protected override void OnLoaded(RoutedEventArgs e)
    {
        base.OnLoaded(e);

        head? headElement = head;
        body? bodyElement = body;

        // Fallback: find first <head> and <body> among [Content]
        if (headElement is null || bodyElement is null)
        {
            foreach (var child in content)
            {
                if (headElement is null && child is head h)
                {
                    headElement = h;
                    continue;
                }

                if (bodyElement is null && child is body b)
                {
                    bodyElement = b;
                    continue;
                }
            }
        }

        // Prefer rendering <body>, otherwise fallback to first control-like child
        if (bodyElement is not null)
        {
            // Optional scrolling
            if (string.Equals(bodyElement.scroll, "auto", StringComparison.OrdinalIgnoreCase))
            {
                Content = new ScrollViewer { Content = bodyElement };
            }
            else
            {
                Content = new Viewbox
                {
                    Child = bodyElement,
                    Stretch = Avalonia.Media.Stretch.Uniform,
                    StretchDirection = Avalonia.Media.StretchDirection.Both
                };
            }
        }
        else
        {
            foreach (var child in content)
            {
                if (child is Control c)
                {
                    Content = new Viewbox
                    {
                        Child = c,
                        Stretch = Avalonia.Media.Stretch.Uniform,
                        StretchDirection = Avalonia.Media.StretchDirection.Both
                    };
                    break;
                }
            }
        }

        // If <head><title> exists, use it for Window.Title
        if (headElement is not null)
        {
            title? t = null;
            foreach (var hChild in headElement.content)
            {
                if (hChild is title ht)
                {
                    t = ht;
                    break;
                }
            }

            if (t?.Text is { } text && !string.IsNullOrWhiteSpace(text))
            {
                Title = text;
            }

            // Apply styles from <link rel="stylesheet"> and <style>
            ApplyHeadStyles(headElement);

        }

        // Fallback: use html.title or default
        if (string.IsNullOrWhiteSpace(Title))
        {
            if (!string.IsNullOrWhiteSpace(title))
            {
                Title = title;
            }
            else
            {
                Title = "Untitled";
            }
        }
    }

    private void ApplyHeadStyles(head headElement)
    {
        foreach (var hChild in headElement.content)
        {
            switch (hChild)
            {
                case link l when string.Equals(l.rel, "stylesheet", StringComparison.OrdinalIgnoreCase):
                    LoadStylesheet(l.href, l.type);
                    break;
                case style st:
                    LoadInlineStyle(st);
                    break;
            }
        }
    }

    private void LoadStylesheet(string? href, string? type)
    {
        if (string.IsNullOrWhiteSpace(href))
            return;

        try
        {
            if (Uri.TryCreate(href, UriKind.Absolute, out var uri))
            {
                var effType = type;
                if (string.IsNullOrWhiteSpace(effType) && uri.IsAbsoluteUri && uri.AbsolutePath.EndsWith(".css", StringComparison.OrdinalIgnoreCase))
                    effType = "text/css";
                if (uri.Scheme == "avares")
                {
                    using var stream = AssetLoader.Open(uri);
                    using var reader = new StreamReader(stream);
                    var text = reader.ReadToEnd();
                    TryParseAndApplyStyle(text, effType);
                    return;
                }

                if (uri.IsFile && File.Exists(uri.LocalPath))
                {
                    var text = File.ReadAllText(uri.LocalPath);
                    TryParseAndApplyStyle(text, effType);
                    return;
                }
            }
            else if (File.Exists(href))
            {
                var text = File.ReadAllText(href);
                var effType = type;
                if (string.IsNullOrWhiteSpace(effType) && href.EndsWith(".css", StringComparison.OrdinalIgnoreCase))
                    effType = "text/css";
                TryParseAndApplyStyle(text, effType);
                return;
            }
        }
        catch
        {
            // ignore
        }
    }

    private void LoadInlineStyle(style st)
    {
        if (string.IsNullOrWhiteSpace(st.Text))
            return;
        TryParseAndApplyStyle(st.Text!, st.type);
    }

    private void TryParseAndApplyStyle(string text, string? type)
    {
        // Apply a minimal CSS subset at runtime by walking the visual tree.
        var isCss = (!string.IsNullOrWhiteSpace(type) && type.Contains("css", StringComparison.OrdinalIgnoreCase))
                    || LooksLikeCss(text);
        if (isCss && Content is Control root)
        {
            CssRuntimeApplier.ApplyCss(text, root);
        }
    }

    private static bool LooksLikeCss(string text)
    {
        // crude heuristic: contains '{' and ':' pairs beyond XAML typical patterns, and no '<Style' tag
        return text.Contains('{') && text.Contains(':') && !text.Contains("<Style", StringComparison.OrdinalIgnoreCase);
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
    
    public string? title
    {
        get { return GetValue(titleProperty); }
        set { SetValue(titleProperty, value); }
    }

    public head? head
    {
        get => GetValue(headProperty);
        set => SetValue(headProperty, value);
    }

    public body? body
    {
        get => GetValue(bodyProperty);
        set => SetValue(bodyProperty, value);
    }
}
