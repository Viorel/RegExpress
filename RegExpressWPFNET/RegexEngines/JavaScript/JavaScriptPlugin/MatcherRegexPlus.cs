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
    static partial class MatcherRegexPlus
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
            public string? Stack { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.Runtime == RuntimeEnum.RegexPlus );

            string flags = string.Concat(
                options.i ? "i" : "",
                options.m ? "m" : "",
                options.s ? "s" : "",
                //options.u ? "u" : "", // no use
                options.v ? "v" : "",
                options.x ? "x" : "",
                options.n ? "n" : "",
                options.y ? "y" : "",
                options.g ? "g" : "",
                options.subclass ? "C" : ""
                );

            string func = options.Function switch { FunctionEnum.MatchAll => "matchAll", FunctionEnum.Exec => "exec", _ => throw new InvalidOperationException( ) };

            var data = new
            {
                pattern,
                text,
                flags,
                func
            };

            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new( GetQuickJsExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = [GetRegexPlusWorkerPath( )];
            ph.WorkingDirectory = GetRegexPlusWorkerDirectory( );

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            ResponseMatches? response = JsonSerializer.Deserialize<ResponseMatches>( ph.OutputStream );

            if( response == null ) throw new Exception( "JavaScript failed." );
            if( !string.IsNullOrWhiteSpace( response.Error ) )
            {
#if DEBUG
                throw new Exception( $"{response.Error}\n{response.Stack}" );
#else
                throw new Exception( response.Error );
#endif
            }


            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );

            foreach( var cm in response.Matches! )
            {
                if( cm.Indices!.Any( ) )
                {
                    int native_start = cm.Indices![0][0];
                    int native_end = cm.Indices[0][1];
                    int native_length = native_end - native_start;

                    SimpleMatch sm = SimpleMatch.Create( native_start, native_length, stg );

                    sm.AddDefaultGroup( );

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

                        int[] g = cm.Indices[i];

                        if( g == null )
                        {
                            sm.AddFailedGroup( name );
                        }
                        else
                        {
                            native_start = g[0];
                            native_end = g[1];
                            native_length = native_end - native_start;

                            sm.AddSucceededGroup( native_start, native_length, name );
                        }
                    }

                    matches.Add( sm );
                }
            }

            return new RegexMatches( matches.Count, matches );
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

        static string GetRegexPlusWorkerDirectory( )
        {
            return Path.Combine( GetPluginDirectory( ), "RegexPlusWorker" );
        }

        static string GetQuickJsExePath( )
        {
            return Path.Combine( GetQuickJsWorkerDirectory( ), "qjs.exe" );
        }

        static string GetRegexPlusWorkerPath( )
        {
            return Path.Combine( GetRegexPlusWorkerDirectory( ), "RegexPlusWorker.js" );
        }
    }
}
