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
    static class MatcherReXile
    {

        sealed class Rootobject
        {
            public required int[][][] matches { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.crate == CrateEnum.rexile );

            var obj = new
            {
                pattern = pattern,
                text = text,
                //options = new
                //{
                //}
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

            if( root_object == null || root_object.matches == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in root_object.matches )
            {
                SimpleMatch? match = null;

                {
                    int native_start = m[0][0];
                    int native_end = m[0][1];
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                    match.AddDefaultGroup( );
                }

                for( int group_index = 1; group_index < m.Length; group_index++ )
                {
                    int[] g = m[group_index];

                    int native_start = g[0];
                    int native_end = g[1];
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    Debug.Assert( match != null );

                    string name = name = group_index.ToString( CultureInfo.InvariantCulture );

                    match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
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
            string worker_exe = Path.Combine( assembly_dir, @"ReXileWorker.bin" );

            return worker_exe;
        }
    }
}
