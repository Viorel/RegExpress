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
    class Matcher : IMatcher
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


        readonly string Pattern;
        readonly Options Options;


        public Matcher( string pattern, Options options )
        {
            Pattern = pattern;
            Options = options;
        }


        #region IMatcher

        public RegexMatches Matches( string text, ICancellable cnc )
        {
            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            StringBuilder flags = new( );

            //if( Options.g ) flags.Append( 'g' );
            if( Options.i ) flags.Append( 'i' );
            if( Options.m ) flags.Append( 'm' );
            if( Options.s ) flags.Append( 's' );
            if( Options.x ) flags.Append( 'x' );

            var obj = new
            {
                p = Pattern,
                t = text,
                f = flags.ToString( ),
            };

            string json = JsonSerializer.Serialize( obj );

            string? stdout_contents;
            string? stderr_contents;

            Action<StreamWriter> stdinWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdinWriter, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( stdout_contents );

            if( response == null ) throw new Exception( "Null response" );

            var matches = new List<IMatch>( );

            foreach( var m in response.matches! )
            {
                SimpleMatch? match = null;
                ISimpleTextGetter? stg = null;

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

                        if( stg == null ) stg = new SimpleTextGetter( text );

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

        #endregion IMatcher


        public static string? GetVersion( ICancellable cnc )
        {
            string? stdout_contents;
            string? stderr_contents;

            Action<StreamWriter> stdinWriter = sw =>
            {
                sw.Write( "{\"c\":\"v\"}" );
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdinWriter, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            VersionResponse? r = JsonSerializer.Deserialize<VersionResponse>( stdout_contents );

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
