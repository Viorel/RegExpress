using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
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
    static partial class MatcherWebView2
    {
        public class ResponseVersion
        {
            public string? Version { get; set; }
        }

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
                options.v ? "v" : "",
                options.y ? "y" : "",
                options.g ? "g" : "",
                options.Function == FunctionEnum.Exec ? "E" : ""
                );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;
            ph.Arguments = ["b"];

            ph.BinaryWriter = bw =>
            {
                bw.Write( (byte)'b' );
                bw.Write( ToJavaScriptString( pattern ) );
                bw.Write( ToJavaScriptString( text ) );
                bw.Write( flags );
                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            using StreamReader sr = new( ph.OutputStream, Encoding.Unicode );
            string output_contents = sr.ReadToEnd( );

            ResponseMatches? client_response = JsonSerializer.Deserialize<ResponseMatches>( output_contents );

            if( client_response == null ) throw new Exception( "JavaScript failed." );
            if( !string.IsNullOrWhiteSpace( client_response.Error ) ) throw new Exception( client_response.Error );

            List<IMatch> matches = new( );
            SimpleTextGetter stg = new( text );

            foreach( var cm in client_response.Matches! )
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
            try
            {
                using ProcessHelper ph = new( GetWorkerExePath( ) );

                ph.AllEncoding = EncodingEnum.Unicode;
                ph.Arguments = ["v"];

                if( !ph.Start( cnc ) ) return null;

                if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

                using StreamReader sr = new( ph.OutputStream, Encoding.Unicode );
                string output_contents = sr.ReadToEnd( );

                ResponseVersion? v = JsonSerializer.Deserialize<ResponseVersion>( output_contents );

                string? version = v!.Version;

                // keep up to three components

                if( version != null )
                {
                    var m = SimplifyVersionRegex( ).Match( version );
                    if( m.Success )
                    {
                        version = m.Groups["v"].Value;
                    }
                }

                return version;
            }
            catch( Exception exc )
            {
                _ = exc;
                if (InternalConfig.HandleException( exc ))
                    throw;

                return null;
            }
        }


        static string ToJavaScriptString( string text )
        {
            return Encoding.UTF8.GetString( JsonEncodedText.Encode( text ).EncodedUtf8Bytes );
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"WebView2Worker.bin" );

            return worker_exe;
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

        [GeneratedRegex( @"^(?<v>\d+([.]\d+([.]\d+)?)?)([.]\d+)*$", RegexOptions.ExplicitCapture )]
        private static partial Regex SimplifyVersionRegex( );

    }
}
