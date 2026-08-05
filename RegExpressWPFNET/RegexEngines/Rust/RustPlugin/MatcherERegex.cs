using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Interop;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;


namespace RustPlugin
{
    static class MatcherERegex
    {

        public class Rootobject
        {
            public required Group[][] matches { get; set; }
            public required Name[] names { get; set; }
        }

        public class Group
        {
            public int[] g { get; set; }
            public int[][]? c { get; set; }
        }

        public class Name
        {
            public required string n { get; set; }
            public required int i { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.crate == CrateEnum.eregex );

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = new
                {
                    IGNORECASE = options.case_insensitive,
                    MULTILINE = options.multi_line,
                    DOTALL = options.dot_matches_new_line,
                    UNICODE = options.unicode,
                    ASCII = options.ASCII,
                    VERBOSE = options.ignore_whitespace,
                    FULLCASE = options.FULLCASE,
                    WORD = options.WORD,
                    LOCALE = options.LOCALE,
                    VERSION0 = options.VERSION0,
                    VERSION1 = options.VERSION1,
                }
            };

            string json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

#if DEBUG
            ph.Environment.Add( "RUST_BACKTRACE", "1" );
#endif

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

#if DEBUG
            using StreamReader sr = new( ph.OutputStream );
            string output = sr.ReadToEnd( );
            Rootobject? response = JsonSerializer.Deserialize<Rootobject>( output );
#else
            Rootobject? response = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

            if( response == null || response.matches == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in response.matches )
            {
                SimpleMatch? match = null;

                for( int group_index = 0; group_index < m.Length; group_index++ )
                {
                    Group g = m[group_index];
                    bool success = g.g.Length == 2;

                    int native_start = success ? g.g[0] : 0;
                    int native_end = success ? g.g[1] : 0;
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    if( group_index == 0 )
                    {
                        Debug.Assert( match == null );
                        Debug.Assert( success );

                        match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                    }

                    Debug.Assert( match != null );

                    string? name = response.names?.Where( p => p.i == group_index ).Select( p => p.n ).FirstOrDefault( );
                    if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        var group = match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );

                        if( g.c != null)
                        {
                            foreach( var c in g.c)
                            {
                                int c_native_start =  c[0];
                                int c_native_end = c[1];
                                int c_native_length = native_end - native_start;

                                (int c_char_start, int c_char_length) = index_converter.Convert( c_native_start, c_native_end );

                                group.AddCapture( c_native_start, c_native_length, c_char_start, c_char_length );
                            }
                        }
                    }
                }

                Debug.Assert( match != null );

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"ERegexWorker.bin" );

            return worker_exe;
        }
    }
}


namespace Json
{

    public class Rootobject
    {
        public Match[][] matches { get; set; }
        public Name[] names { get; set; }
    }

    public class Match
    {
        public int?[] g { get; set; }
        public int[][] c { get; set; }
    }

    public class Name
    {
        public string n { get; set; }
        public int i { get; set; }
    }

}