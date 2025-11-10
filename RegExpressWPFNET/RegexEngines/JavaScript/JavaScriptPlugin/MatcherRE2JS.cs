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
    static partial class MatcherRE2JS
    {
        public class Response
        {
            public Match[]? Matches { get; set; }
            public string? Error { get; set; }
        }

        public class Match
        {
            public int[][]? ag { get; set; }
            public Ng[]? ng { get; set; }
        }

        public class Ng
        {
            public string? n { get; set; }
            public int? s { get; set; }
            public int? e { get; set; }
        }


        static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string flags = string.Concat(
                options.i ? "i" : "",
                options.m ? "m" : "",
                options.s ? "s" : "",
                options.DISABLE_UNICODE_GROUPS ? "U" : "",
                options.LONGEST_MATCH ? "l" : ""
                );

            var data = new { pattern, text, flags };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new( GetQuickJsExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = [GetRE2JSWorkerPath( )];
            ph.WorkingDirectory = GetRE2JSWorkerDirectory( );

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            Response? response = JsonSerializer.Deserialize<Response>( ph.OutputStream );

            if( response == null ) throw new Exception( "JavaScript failed." );
            if( !string.IsNullOrWhiteSpace( response.Error ) ) throw new Exception( response.Error );
            if( response.Matches == null ) throw new Exception( "Invalid null response." );

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );
            SimpleMatch? current_match = null;

            foreach( Match response_match in response.Matches )
            {
                /*
                 * Example:
                 * {"Matches":[{"ag":[[0,3],[1,2],[2,3],[-1,-1]],"ng":[["n",2,3],["x",-1,-1]]},{"ag":[[4,7],[5,6],[6,7],[-1,-1]],"ng":[["n",6,7],["x",-1,-1]]}]}
                 */

                HashSet<string> used_names = [];

                for( int i = 0; i < response_match.ag!.Length; i++ )
                {
                    int[] g = response_match.ag[i];

                    int start = g[0];
                    int end = g[1];

                    if( i == 0 )
                    {
                        Debug.Assert( start >= 0 );
                        Debug.Assert( start <= end );

                        SimpleMatch sm = SimpleMatch.Create( start, end - start, stg );

                        sm.AddGroup( sm.Index, sm.Length, true, "0" ); // (default group)

                        matches.Add( sm );

                        current_match = sm;
                    }
                    else
                    {
                        if( current_match == null ) throw new ApplicationException( );

                        if( start < 0 )
                        {
                            string name = i.ToString( CultureInfo.InvariantCulture );

                            current_match.AddGroup( -1, 0, false, name );
                        }
                        else
                        {
                            // find name
                            string? name;
                            name = response_match.ng!.FirstOrDefault( g => g.s >= 0 && g.s == start && g.e == end && !used_names.Contains( g.n! ) )?.n;
                            if( name != null ) used_names.Add( name );
                            name ??= response_match.ng!.FirstOrDefault( g => g.s >= 0 && g.s == start && g.e == end )?.n;
                            name ??= i.ToString( CultureInfo.InvariantCulture );

                            current_match.AddGroup( start, end - start, true, name );
                        }
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            try
            {
                // example: " * @version v1.2.0"

                string js_path = Path.Combine( GetRE2JSWorkerDirectory( ), "re2js", "build", "index.esm.js" );
                string? version = File
                    .ReadLines( js_path )
                    .Select( line => RegexGetVersion( ).Match( line ) )
                    .Where( m => m.Success )
                    .Select( m => m.Groups["version"].Value )
                    .FirstOrDefault( );

                return version;
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }
        }

        static string GetPluginDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = System.IO.Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }

        static string GetQuickJsWorkerDirectory( )
        {
            return Path.Combine( GetPluginDirectory( ), "QuickJsWorker" );
        }

        static string GetRE2JSWorkerDirectory( )
        {
            return Path.Combine( GetPluginDirectory( ), "RE2JSWorker" );
        }

        static string GetQuickJsExePath( )
        {
            return Path.Combine( GetQuickJsWorkerDirectory( ), "qjs.exe" );
        }

        static string GetRE2JSWorkerPath( )
        {
            return Path.Combine( GetRE2JSWorkerDirectory( ), "RE2JSWorker.js" );
        }

        internal static void StartGetVersion( Action<string?> setVersion )
        {
            if( LazyVersion.IsValueCreated )
            {
                setVersion( LazyVersion.Value );

                return;
            }

            Thread t = new( ( ) =>
            {
                setVersion( LazyVersion.Value );
            } )
            {
                IsBackground = true
            };

            t.Start( );
        }


        // example: " * @version v1.2.0"
        [GeneratedRegex( @"(?inx) .* @version (:\s*|\s+) v?(?<version>\d+([.]\d+)+)" )]
        private static partial Regex RegexGetVersion( );
    }
}
