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
    static class MatcherJavaRegex
    {


        public class MatchesResponse
        {
            public MatchResponse[] matches { get; set; }
        }

        public class MatchResponse
        {
            public int[] m { get; set; }
            public int[][] g { get; set; }
            public string[][] ng { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            bool is_builder = options.@struct == StructEnum.RegexBuilder;

            StringBuilder flags = new( );
            if( options.case_insensitive ) flags.Append( 'i' );
            if( options.multi_line ) flags.Append( 'm' );
            if( options.dot_matches_new_line ) flags.Append( 's' );
            if( options.ignore_whitespace ) flags.Append( 'x' );
            if( options.unicode ) flags.Append( 'u' );
            if( options.unicode_sets ) flags.Append( 'U' );
            if( options.d ) flags.Append( 'd' );
            if( options.l ) flags.Append( 'l' );

            var obj = new
            {
                pattern = pattern,
                text = text,
                flags = flags.ToString(),
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

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

            if( response == null || response.matches == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            CodepointIndexConverter index_converter = new( text );

            foreach( var m in response.matches )
            {
                SimpleMatch match;

                {
                    if( m.m.Length != 2 ) throw new Exception( "Invalid response [1]" );

                    int native_start = m.m[0];
                    int native_end = m.m[1];
                    int native_length = native_end - native_start;

                    (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                    match = SimpleMatch.Create( native_start, native_length, char_index, char_length, stg );
                    match.AddDefaultGroup( );
                }

                for( int i = 0; i < m.g.Length; i++ )
                {
                    int[]? g = m.g[i];

                    if( g.Length != 0 && g.Length != 2 ) throw new Exception( "Invalid response [2]" );

                    string name = ( i + 1 ).ToString( CultureInfo.InvariantCulture );

                    if( g.Length == 0 )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        int native_start = g[0];
                        int native_end = g[1];
                        int native_length = native_end - native_start;

                        (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_length, char_index, char_length, name );
                    }
                }


                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"RustJavaRegexWorker.bin" );

            return worker_exe;
        }
    }
}
