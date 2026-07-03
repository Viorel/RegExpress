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
    static class MatcherResharp
    {
        class MatchesResponse
        {
            public int[][]? matches { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            if( options.@struct == StructEnum.None )
            {
                throw new ApplicationException( "Invalid struct." );
            }

            UInt64? max_dfa_capacity = ValidationUtilities.ParseUInt64( "max_dfa_capacity", options.max_dfa_capacity );
            UInt64? lookahead_context_max = ValidationUtilities.ParseUInt64( "lookahead_context_max", options.lookahead_context_max );

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = new
                {
                    options.case_insensitive,
                    options.dot_matches_new_line,
                    options.multi_line,
                    options.ignore_whitespace,
                    options.hardened,
                    options.unbounded_size,
                    unicode_mode = options.UnicodeMode switch { UnicodeModeEnum.Ascii => "Ascii", UnicodeModeEnum.Full => "Full", UnicodeModeEnum.Javascript => "Javascript", _ => null },
                    max_dfa_capacity = max_dfa_capacity,
                    lookahead_context_max = lookahead_context_max,
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

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

            if( response == null || response.matches == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in response.matches )
            {
                int native_start = m[0];
                int native_end = m[1];
                int native_length = native_end - native_start;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                SimpleMatch? match = SimpleMatch.Create( native_start, native_end, char_start, char_length, stg );

                match.AddDefaultGroup( );

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"RustResharpWorker.bin" );

            return worker_exe;
        }
    }
}
