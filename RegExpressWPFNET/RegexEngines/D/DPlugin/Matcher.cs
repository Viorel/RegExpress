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


namespace DPlugin
{
    static class Matcher
    {

        class VersionResponse
        {
            public string? version { get; set; }
        }


        public class MatchesResponse
        {
            public string[]? names { get; set; }
            public OneMatchResponse[]? matches { get; set; }
        }


        public class OneMatchResponse
        {
            [JsonPropertyName( "i" )]
            public int index { get; set; } // byte-index of the whole match

            [JsonPropertyName( "g" )]
            public int[][]? groups { get; set; } // [byte-index, byte-length], or [-1, 0] if failed

            [JsonPropertyName( "n" )]
            public int[][]? named_groups { get; set; } // [byte-index, byte-length], or [-1, 0] if failed
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            StringBuilder flags = new( );

            //if( Options.g ) flags.Append( 'g' );
            if( options.i ) flags.Append( 'i' );
            if( options.m ) flags.Append( 'm' );
            if( options.s ) flags.Append( 's' );
            if( options.x ) flags.Append( 'x' );

            var obj = new
            {
                p = pattern,
                t = text,
                f = flags.ToString( ),
            };

            string json = JsonSerializer.Serialize( obj );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

            if( response == null ) throw new Exception( "Null response" );

            List<IMatch> matches = new( );

            foreach( var m in response.matches! )
            {
                SimpleMatch? match = null;
                SimpleTextGetter? stg = null;

                for( int group_index = 0; group_index < m.groups!.Length; group_index++ )
                {
                    int[] g = m.groups[group_index];
                    bool success = g.Length == 2;

                    if( group_index == 0 && !success )
                    {
                        // if pattern is "()", which matches any position, 'std.regex' does not return captures, 
                        // even the main one (all are null); however the match object contains the valid index;
                        // this is a workaround:

                        success = true;
                        g = new[] { m.index, 0 };
                    }

                    int byte_start = success ? g[0] : 0;
                    int byte_end = byte_start + ( success ? g[1] : 0 );
                    int byte_length = byte_end - byte_start;

                    int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                    int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end );
                    int char_length = char_end - char_start;

                    if( group_index == 0 )
                    {
                        Debug.Assert( match == null );
                        Debug.Assert( success );

                        stg ??= new SimpleTextGetter( text );

                        match = SimpleMatch.Create( char_start, char_end - char_start, stg );
                    }

                    Debug.Assert( match != null );

                    // try to identify the named group by index and length;
                    // cannot be done univocally in situations like "(?P<name1>(?P<name2>(.))", because index and length are the same

                    string? name;

                    var np = m.named_groups!
                        .Where( _ => group_index != 0 )
                        .Select( ( ng, j ) => new { ng, j } )
                        .Where( p => p.ng[0] >= 0 )
                        .FirstOrDefault( z => z.ng[0] == byte_start && z.ng[1] == byte_length && !match.Groups.Any( q => q.Name == response.names![z.j] ) );

                    if( np == null )
                    {
                        name = null;
                    }
                    else
                    {
                        name = response.names![np.j];
                    }

                    if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                    if( success )
                    {
                        match.AddGroup( char_start, char_length, true, name );
                    }
                    else
                    {
                        match.AddGroup( 0, 0, false, name );
                    }
                }

                Debug.Assert( match != null );

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( "{\"c\":\"v\"}" );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            VersionResponse? r = JsonSerializer.Deserialize<VersionResponse>( ph.OutputStream );

            if( r == null ) throw new Exception( "Null response" );

            return r.version;
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"DWorker.bin" );

            return worker_exe;
        }

    }
}
