using RegExpressLibrary;
using System;


namespace DotNETPlugin;

static class Versions
{
    static readonly Lazy<string?> LazyVersionDotNet = new( SubengineDotNet.GetVersion( ICancellable.NonCancellable ) );
    static readonly Lazy<string?> LazyVersionDotNetFramework = new( SubengineDotNetFramework.GetVersion( ICancellable.NonCancellable ) );

    public static string? DotNet { get; } = LazyVersionDotNet.Value;
    public static string? DotNetFramework { get; } = LazyVersionDotNetFramework.Value;
    public static string? ReSharp { get; } = "1.0.6";
    public static string? Scout { get; } = "0.8.0";
    public static string? LokadUtf8Regex { get; } = "0.3.0";
    public static string? GregsEx { get; } = "0.5.4";
    public static string? CorvusEcma262 { get; } = "1.0.8";
    public static string? FuzzyRegex { get; } = "1.1.0";
}
