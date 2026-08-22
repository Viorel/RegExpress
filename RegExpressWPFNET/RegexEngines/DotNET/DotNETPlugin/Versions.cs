using RegExpressLibrary;
using System;
using System.Collections.Generic;
using System.Text;

namespace DotNETPlugin
{
    internal class Versions
    {
        static Lazy<string?> LazyVersionDotNet = new( MatcherDotNet.GetVersion( ICancellable.NonCancellable ) );
        static Lazy<string?> LazyVersionDotNetFramework = new( MatcherDotNetFramework.GetVersion( ICancellable.NonCancellable ) );

        public static string? DotNet { get; } = LazyVersionDotNet.Value;
        public static string? DotNetFramework { get; } = LazyVersionDotNetFramework.Value;
        public static string? ReSharp { get; } = "1.0.5";
        public static string? Scout { get; } = "0.6.1";
    }
}
