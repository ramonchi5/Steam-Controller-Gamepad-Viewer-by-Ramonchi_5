using System.Text.Json;
using System.Reflection;
using System.Globalization;
using SteamControllerGamepadViewer.Hid;
using SteamControllerGamepadViewer.Sdl;
using SteamControllerGamepadViewer.State;

SdlNative.Configure(args);

var physicalWebRoot = WebRootResolver.ResolvePhysical();
var builder = WebApplication.CreateBuilder(new WebApplicationOptions
{
    Args = args,
    WebRootPath = physicalWebRoot ?? AppContext.BaseDirectory,
});

builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(options =>
{
    options.SingleLine = true;
    options.TimestampFormat = "HH:mm:ss ";
});

builder.WebHost.UseUrls(builder.Configuration["urls"] ?? builder.Configuration["Urls"] ?? "http://127.0.0.1:31337");

builder.Services.AddSingleton<ControllerStateHub>();
builder.Services.AddSingleton<SteamHidState>();
builder.Services.AddHostedService<SteamHidTouchpadService>();
builder.Services.AddHostedService<SdlControllerService>();

var app = builder.Build();

if (physicalWebRoot is not null)
{
    app.UseDefaultFiles();
    app.UseStaticFiles();
}

app.MapGet("/api/state", (ControllerStateHub hub) => Results.Json(hub.Current, AppJson.Options));

app.MapGet("/events", async (HttpContext context, ControllerStateHub hub) =>
{
    context.Response.Headers.CacheControl = "no-cache";
    context.Response.Headers.Connection = "keep-alive";
    context.Response.ContentType = "text/event-stream";

    await foreach (var snapshot in hub.Subscribe(context.RequestAborted))
    {
        var json = JsonSerializer.Serialize(snapshot, AppJson.Options);
        await context.Response.WriteAsync($"data: {json}\n\n", context.RequestAborted);
        await context.Response.Body.FlushAsync(context.RequestAborted);
    }
});

app.MapGet("/health", (ControllerStateHub hub) => Results.Json(new
{
    ok = true,
    connected = hub.Current.Connected,
    name = hub.Current.Name,
    status = hub.Current.Status,
}, AppJson.Options));

app.MapGet("/api/hid", (SteamHidState hid) => Results.Json(hid.Status, AppJson.Options));

app.MapGet("/controller-art.svg", async (HttpContext context) =>
{
    if (!EmbeddedWebAssets.TryReadText("assets/controller_config_controller_triton.svg", out var svg))
    {
        context.Response.StatusCode = StatusCodes.Status404NotFound;
        return;
    }

    var bodyOutline = QueryLineScale(QueryFirst(context.Request.Query, "bodyLines", "bodyOutline"), 10);
    var innerBodyOutline = QueryLineScale(QueryFirst(context.Request.Query, "innerBodyLines", "innerBodyOutline"), 10);
    var joystickOutline = QueryLineScale(QueryFirst(context.Request.Query, "joystickLines", "joystickOutline"), 10);
    var buttonOutline = QueryLineScale(QueryFirst(context.Request.Query, "btnLines", "buttonOutline"), 10);
    var lineColor = QueryHexColor(QueryFirst(context.Request.Query, "linesColor", "LinesColor"), "ffffff");
    var lineOpacity = QueryPercentOpacity(QueryFirst(context.Request.Query, "linesOpac", "LinesOpac", "linesOpacity", "LinesOpacity"), 55);
    context.Response.Headers.CacheControl = "no-store";
    context.Response.ContentType = "image/svg+xml";
    await context.Response.WriteAsync(ControllerArtStyler.Apply(svg, bodyOutline, innerBodyOutline, joystickOutline, buttonOutline, lineColor, lineOpacity), context.RequestAborted);
});

app.MapGet("/{**assetPath}", async (string? assetPath, HttpContext context) =>
{
    var path = string.IsNullOrWhiteSpace(assetPath) ? "index.html" : assetPath;
    if (!EmbeddedWebAssets.TryOpen(path, out var stream, out var contentType))
    {
        context.Response.StatusCode = StatusCodes.Status404NotFound;
        return;
    }

    await using (stream)
    {
        context.Response.ContentType = contentType;
        await stream.CopyToAsync(context.Response.Body, context.RequestAborted);
    }
});

app.Run();

static double QueryNumber(string? value, double fallback, double min, double max)
{
    value = value?.Trim();
    if (value?.EndsWith('%') == true)
    {
        value = value[..^1].TrimEnd();
    }

    if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
    {
        return fallback;
    }

    return Math.Min(max, Math.Max(min, number));
}

static double QueryLineScale(string? value, double fallbackUnits)
    => QueryNumber(value, fallbackUnits, 0, 40) / 10;

static double QueryPercentOpacity(string? value, double fallbackPercent)
    => QueryNumber(value, fallbackPercent, 0, 100) / 100;

static string? QueryFirst(IQueryCollection query, params string[] names)
{
    foreach (var name in names)
    {
        if (query.TryGetValue(name, out var values) && values.Count > 0)
        {
            return values[0];
        }
    }

    return null;
}

static string QueryHexColor(string? value, string fallback)
{
    var hex = value?.Trim().TrimStart('#').ToLowerInvariant();
    if (hex is null)
    {
        return "#" + fallback;
    }

    if (hex.Length == 3)
    {
        hex = string.Concat(hex.Select(c => new string(c, 2)));
    }
    else if (hex.Length == 4)
    {
        hex = string.Concat(hex.Take(3).Select(c => new string(c, 2)));
    }
    else if (hex.Length == 8)
    {
        hex = hex[..6];
    }

    return hex.Length == 6 && hex.All(Uri.IsHexDigit)
        ? "#" + hex
        : "#" + fallback;
}

internal static class AppJson
{
    public static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = false,
    };
}

internal static class WebRootResolver
{
    public static string? ResolvePhysical()
    {
        var candidates = new[]
        {
            Path.Combine(Environment.CurrentDirectory, "wwwroot"),
            Path.Combine(Environment.CurrentDirectory, "src", "SteamControllerGamepadViewer", "wwwroot"),
            Path.Combine(AppContext.BaseDirectory, "wwwroot"),
        };

        return candidates.FirstOrDefault(Directory.Exists);
    }
}

internal static class EmbeddedWebAssets
{
    private static readonly Assembly Assembly = typeof(EmbeddedWebAssets).Assembly;
    private static readonly IReadOnlyDictionary<string, string> ResourceNames = Assembly
        .GetManifestResourceNames()
        .ToDictionary(NormalizeResourceName, StringComparer.OrdinalIgnoreCase);

    public static bool TryOpen(string path, out Stream stream, out string contentType)
    {
        path = path.Replace('\\', '/').TrimStart('/');
        if (path.Length == 0)
        {
            path = "index.html";
        }

        contentType = ContentTypeFor(path);
        if (!ResourceNames.TryGetValue($"wwwroot/{path}", out var resourceName))
        {
            stream = Stream.Null;
            return false;
        }

        stream = Assembly.GetManifestResourceStream(resourceName) ?? Stream.Null;
        return !ReferenceEquals(stream, Stream.Null);
    }

    public static bool TryReadText(string path, out string text)
    {
        if (!TryOpen(path, out var stream, out _))
        {
            text = string.Empty;
            return false;
        }

        using (stream)
        using (var reader = new StreamReader(stream))
        {
            text = reader.ReadToEnd();
            return true;
        }
    }

    private static string NormalizeResourceName(string resourceName)
        => resourceName.Replace('\\', '/').TrimStart('/');

    private static string ContentTypeFor(string path)
        => Path.GetExtension(path).ToLowerInvariant() switch
        {
            ".css" => "text/css; charset=utf-8",
            ".html" => "text/html; charset=utf-8",
            ".js" => "text/javascript; charset=utf-8",
            ".json" => "application/json; charset=utf-8",
            ".svg" => "image/svg+xml",
            _ => "application/octet-stream",
        };
}

internal static class ControllerArtStyler
{
    public static string Apply(string svg, double bodyOutlineScale, double innerBodyOutlineScale, double joystickOutlineScale, double buttonOutlineScale, string lineColor, double lineOpacity)
    {
        var body = (1.3 * bodyOutlineScale).ToString("0.###", CultureInfo.InvariantCulture);
        var inner = (0.9 * innerBodyOutlineScale).ToString("0.###", CultureInfo.InvariantCulture);
        var joystick = (0.9 * joystickOutlineScale).ToString("0.###", CultureInfo.InvariantCulture);
        var button = (0.9 * buttonOutlineScale).ToString("0.###", CultureInfo.InvariantCulture);
        var opacity = lineOpacity.ToString("0.###", CultureInfo.InvariantCulture);

        return MarkInnerBodyOutlines(svg)
            .Replace(
                "<svg width=\"456\" height=\"320\" viewBox=\"0 0 456 320\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">",
                "<svg width=\"504\" height=\"368\" viewBox=\"-24 -24 504 368\" fill=\"none\" overflow=\"visible\" xmlns=\"http://www.w3.org/2000/svg\">",
                StringComparison.Ordinal)
            .Replace(
                "path[stroke], circle[stroke], rect[stroke] { stroke-width: 0.9px; }",
                $"svg {{ overflow: visible; }}\npath[stroke], circle[stroke], rect[stroke] {{ stroke-width: {button}px; }}",
                StringComparison.Ordinal)
            .Replace(
                "path[stroke-width=\"4\"] { stroke-width: 1.3px; }",
                $"path[stroke-width=\"4\"] {{ stroke-width: {body}px; }}\n.inner-body-outline[stroke] {{ stroke-width: {inner}px; }}\n.joystick-outline[stroke] {{ stroke-width: {joystick}px; }}",
                StringComparison.Ordinal)
            .Replace("</style>", $"</style>\n<g opacity=\"{opacity}\">", StringComparison.Ordinal)
            .Replace("stroke=\"white\"", $"stroke=\"{lineColor}\"", StringComparison.Ordinal)
            .Replace("fill=\"white\"", $"fill=\"{lineColor}\"", StringComparison.Ordinal)
            .Replace("</svg>", "</g>\n</svg>", StringComparison.Ordinal);
    }

    private static string MarkInnerBodyOutlines(string svg)
        => svg
            .Replace(
                "<circle cx=\"162.133\" cy=\"108.758\"",
                "<circle class=\"joystick-outline\" cx=\"162.133\" cy=\"108.758\"",
                StringComparison.Ordinal)
            .Replace(
                "<circle cx=\"34.5\" cy=\"34.5\"",
                "<circle class=\"joystick-outline\" cx=\"34.5\" cy=\"34.5\"",
                StringComparison.Ordinal)
            .Replace(
                "<path d=\"M260.328 121.388",
                "<path class=\"inner-body-outline\" d=\"M260.328 121.388",
                StringComparison.Ordinal)
            .Replace(
                "<path d=\"M129.908 122.978",
                "<path class=\"inner-body-outline\" d=\"M129.908 122.978",
                StringComparison.Ordinal)
            .Replace(
                "<path d=\"M380.707 147.108",
                "<path class=\"inner-body-outline\" d=\"M380.707 147.108",
                StringComparison.Ordinal);
}
