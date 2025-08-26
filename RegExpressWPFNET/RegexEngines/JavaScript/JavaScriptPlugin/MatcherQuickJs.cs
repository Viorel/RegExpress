using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Formats.Tar;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace JavaScriptPlugin
{
    static partial class MatcherQuickJs
    {
        public class ResponseMatch
        {
            [JsonPropertyName( "g" )]
            public Dictionary<string, int[]>? Groups { get; set; }

            [JsonPropertyName( "i" )]
            public List<int[]>? Indices { get; set; }
        }

        public class ResponseMatches
        {
            public List<ResponseMatch>? Matches { get; set; }

            public string? Error { get; set; }
        }

        static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string flags = string.Concat(
                options.i ? "i" : "",
                options.m ? "m" : "",
                options.s ? "s" : "",
                options.u ? "u" : "",
                //options.v ? "v" : "",
                options.y ? "y" : "",
                options.g ? "g" : ""
                );

            string func = options.Function switch { FunctionEnum.MatchAll => "matchAll", FunctionEnum.Exec => "exec", _ => throw new InvalidOperationException( ) };

            var data = new { cmd = "match", pattern, text, flags, func };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new( GetQuickJsExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = [GetQuickJsWorkerPath( )];

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
                sw.Flush( );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            ResponseMatches? response = JsonSerializer.Deserialize<ResponseMatches>( ph.OutputStream );

            if( response == null ) throw new Exception( "JavaScript failed." );
            if( !string.IsNullOrWhiteSpace( response.Error ) ) throw new Exception( response.Error );

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );

            foreach( var cm in response.Matches! )
            {
                if( cm.Indices!.Any( ) )
                {
                    var start = cm.Indices![0][0];
                    var end = cm.Indices[0][1];

                    var sm = SimpleMatch.Create( start, end - start, stg );

                    sm.AddGroup( sm.Index, sm.Length, true, "0" ); // (default group)

                    HashSet<string> used_names = [];

                    for( int i = 1; i < cm.Indices.Count; ++i )
                    {
                        // figure out the name
                        string? n = cm.Groups?.FirstOrDefault( g => cm.Indices[i] != null && (g.Value[0], g.Value[1]) == (cm.Indices[i][0], cm.Indices[i][1]) && !used_names.Contains( g.Key ) ).Key;
                        n ??= cm.Groups?.FirstOrDefault( g => cm.Indices[i] != null && (g.Value[0], g.Value[1]) == (cm.Indices[i][0], cm.Indices[i][1]) ).Key;

                        string name;

                        if( n != null )
                        {
                            name = n;
                            used_names.Add( n );
                        }
                        else
                        {
                            name = i.ToString( CultureInfo.InvariantCulture );
                        }

                        var g = cm.Indices[i];

                        if( g == null )
                        {
                            sm.AddGroup( -1, 0, false, name );
                        }
                        else
                        {
                            start = cm.Indices[i][0];
                            end = cm.Indices[i][1];

                            sm.AddGroup( start, end - start, true, name );
                        }
                    }

                    matches.Add( sm );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            string version_path = Path.Combine( GetWorkerDirectory( ), "VERSION" );

            return File.ReadAllText( version_path ).Trim( );
        }

        static string GetPluginDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = System.IO.Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }

        static string GetWorkerDirectory( )
        {
            return Path.Combine( GetPluginDirectory( ), "QuickJsWorker" );
        }

        static string GetQuickJsExePath( )
        {
            return Path.Combine( GetWorkerDirectory( ), "qjs.exe" );
        }

        static string GetQuickJsWorkerPath( )
        {
            return Path.Combine( GetWorkerDirectory( ), "QuickJsWorker.js" );
        }

        internal static void StartGetVersion( Action<string?> setNodeJsVersion )
        {
            if( LazyVersion.IsValueCreated )
            {
                setNodeJsVersion( LazyVersion.Value );

                return;
            }

            Thread t = new( ( ) =>
            {
                setNodeJsVersion( LazyVersion.Value );
            } )
            {
                IsBackground = true
            };

            t.Start( );
        }
    }
}
