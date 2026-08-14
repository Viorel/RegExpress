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
    static class MatcherRegexr
    {
        public class Rootobject
        {
            public required string[] names { get; set; }
            public required Match[] matches { get; set; }
        }

        public class Match
        {
            public required int[][] g { get; set; }
            public required Ng[] ng { get; set; }
        }

        public class Ng
        {
            public required string n { get; set; }
            public required int[] g { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.crate == CrateEnum.regexr );

            bool use_builder = options.UseBuilder;

            var obj = new
            {
                use_builder = use_builder,
                pattern = pattern,
                text = text,
                options = new
                {
                    options.jit,
                    options.optimize_prefixes,
                    size_limit = use_builder ? ValidationUtilities.ParseUInt32( "size_limit", options.size_limit ) : null,
                    nest_limit = use_builder ? ValidationUtilities.ParseUInt32( "nest_limit", options.nest_limit ) : null,
                    backtrack_limit = use_builder ? ValidationUtilities.ParseUInt32( "backtrack_limit", options.backtrack_limit ) : null,
                }
            };

            string json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );

            using ProcessHelper ph = new( GetWorkerExePath( ) );

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
            Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( output );
#else
            Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

            if( root_object == null || root_object.matches == null || root_object.names == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in root_object.matches )
            {
                SimpleMatch? match = null;

                for( int group_index = 0; group_index < m.g.Length; group_index++ )
                {
                    int[] g = m.g[group_index];
                    bool success = g.Length == 2;

                    int native_start = success ? g[0] : 0;
                    int native_end = success ? g[1] : 0;
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    if( group_index == 0 )
                    {
                        Debug.Assert( match == null );
                        Debug.Assert( success );

                        match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                    }

                    Debug.Assert( match != null );

                    string name = group_index.ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                    }
                }

                Debug.Assert( match != null );

                foreach( var ng in m.ng )
                {
                    bool success = ng.g.Length == 2;

                    if( !success )
                    {
                        match.AddFailedGroup( ng.n );
                    }
                    else
                    {
                        int native_start = ng.g[0];
                        int native_end = ng.g[1];
                        int native_length = native_end - native_start;

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_length, char_start, char_length, ng.n );
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
            string worker_exe = Path.Combine( assembly_dir, @"RustRegexrWorker.bin" );

            return worker_exe;
        }
    }
}
