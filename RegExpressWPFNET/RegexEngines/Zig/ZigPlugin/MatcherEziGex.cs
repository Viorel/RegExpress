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


namespace ZigPlugin
{

    static class MatcherEziGex
    {
        class RootObject
        {
            public string[]? names { get; set; }
            public int[][][]? matches { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.Library == RegexLibraryEnum.EziGex );

            var json_object = new
            {
                pattern = pattern,
                text = text,

                // Options cannot be set at runtime in 'ezi-gex' (they are 'comptime')

                //options = new
                //{
                //    case_insensitive = options.case_insensitive,
                //    multiline = options.multiline,
                //    dot_matches_newline = options.dot_all,
                //    unicode = options.unicode,
                //    unicode_word_boundary_in_dfa = options.unicode_word_boundary_in_dfa,
                //    prefilter = options.prefilter,
                //    case_fold = Enum.GetName( options.case_fold ),
                //    max_repetition = ValidationUtilities.ParseUInt32( "max_repetition", options.max_repetition ),
                //    byte_engine = Enum.GetName( options.byte_engine ),
                //    simd = Enum.GetName( options.simd ),
                //}
            };

            string json = JsonSerializer.Serialize( json_object );

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            JsonSerializerOptions json_options = new( )
            {
                Converters = { new RelaxedJsonConverter( ) },
            };

#if DEBUG
            using StreamReader sr = new( ph.OutputStream );
            string output = sr.ReadToEnd( );
            RootObject? response = JsonSerializer.Deserialize<RootObject>( output, json_options );
#else
            RootObject? response = JsonSerializer.Deserialize<RootObject>( ph.OutputStream, json_options );
#endif

            if( response == null || response.matches == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in response.matches )
            {
                SimpleMatch? match = null;

                {
                    int native_start = m[0][0];
                    int native_end = m[0][1];
                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    Debug.Assert( match == null );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                    match.AddDefaultGroup( );
                }

                for( int group_index = 1; group_index < m.Length; group_index++ )
                {
                    int native_start = m[group_index][0];
                    int native_end = m[group_index][1];

                    string name = response.names?[group_index - 1] ?? group_index.ToString( CultureInfo.InvariantCulture ); ;

                    if( native_start < 0 )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        int native_length = native_end - native_start;

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
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
            string worker_exe = Path.Combine( assembly_dir, @"EziGexWorker.bin" );

            return worker_exe;
        }
    }
}
