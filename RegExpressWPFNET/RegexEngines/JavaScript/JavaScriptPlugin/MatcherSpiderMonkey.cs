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
    static partial class MatcherSpiderMonkey
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

            List<string> arguments = ["--nowarnings", GetSpiderMonkeyWorkerJsPath( )];
            if( options.NoNativeRegexp ) arguments.Add( "--no-native-regexp" );
            if( options.EnableDuplicateNames ) arguments.Add( "--enable-regexp-duplicate-named-groups" );
            if( options.EnableRegexpModifiers ) arguments.Add( "--enable-regexp-modifiers" );

            using ProcessHelper ph = new( GetSpiderMonkeyExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = [.. arguments];

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
            using ProcessHelper ph = new( GetSpiderMonkeyExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = ["--version"]; // (Not documented?)

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string version;

            using( StreamReader sr = new( ph.OutputStream, Encoding.ASCII ) )
            {
                version = sr.ReadToEnd( ).Trim( ); // example: "JavaScript-C143.0"

                version = Regex.Replace( version, @"^JavaScript-", "" );
            }

            return version;
        }

        static string GetPluginDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }

        static string GetWorkerDirectory( )
        {
            return Path.Combine( GetPluginDirectory( ), "SpiderMonkeyWorker" );
        }

        static string GetSpiderMonkeyExePath( )
        {
            Decompress( );

            Debug.Assert( TempFolder != null );

            return Path.Combine( TempFolder!, "spidermonkey", "spidermonkey.exe" );
        }

        static string GetSpiderMonkeyWorkerJsPath( )
        {
            return Path.Combine( GetWorkerDirectory( ), "SpiderMonkeyWorker.js" );
        }

        static string GetTemporaryDirectory( )
        {
            string temp_path = Path.GetTempPath( );
            string dir = Path.Combine( temp_path, Path.GetRandomFileName( ) ); // TODO: exclude almost impossible collisions

            return dir;
        }

        static readonly Lock Locker = new( );
        static string? TempFolder = null;
        static bool IsExtractionDone = false;

        static void Decompress( )
        {
            if( IsExtractionDone ) return;

            lock( Locker )
            {
                if( IsExtractionDone ) return;

                try
                {
                    string worker_dir = GetWorkerDirectory( );
                    string dest_folder = GetTemporaryDirectory( );
                    string source_zip = Path.Combine( worker_dir, @"spidermonkey.zip" );

                    ZipFile.ExtractToDirectory( source_zip, dest_folder );

                    AppDomain.CurrentDomain.ProcessExit += ( s, a ) =>
                    {
                        try
                        {
                            Directory.Delete( dest_folder, recursive: true );
                        }
                        catch( Exception exc )
                        {
                            _ = exc;
                            if( Debugger.IsAttached ) Debugger.Break( );

                            // ignore
                        }
                    };

                    TempFolder = dest_folder;
                }
                catch( Exception exc )
                {
                    _ = exc;
                    if( Debugger.IsAttached ) Debugger.Break( );

                    TempFolder = null;
                }

                IsExtractionDone = true;
            }
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
