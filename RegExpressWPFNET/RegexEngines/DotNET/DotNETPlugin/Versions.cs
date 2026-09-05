using RegExpressLibrary;
using System;


namespace DotNETPlugin;

class Versions
{
    static readonly Lazy<string?> LazyVersionDotNet = new( SubengineDotNet.GetVersion( ICancellable.NonCancellable ) );
    static readonly Lazy<string?> LazyVersionDotNetFramework = new( SubengineDotNetFramework.GetVersion( ICancellable.NonCancellable ) );

    public static string? DotNet { get; } = LazyVersionDotNet.Value;
    public static string? DotNetFramework { get; } = LazyVersionDotNetFramework.Value;
    public static string? ReSharp { get; } = "1.0.5";
    public static string? Scout { get; } = "0.6.1";
    public static string? LokadUtf8Regex { get; } = "0.3.0";
}
