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

            var o = new StringBuilder( );

            if( options.case_insensitive ) o.Append( " i " );
            if( options.dot_matches_new_line ) o.Append( " s " );
            if( options.multi_line ) o.Append( " m " );
            if( options.ignore_whitespace ) o.Append( " x " );
            if( options.hardened ) o.Append( " H " );
            if( options.unbounded_size ) o.Append( " S " );

            switch( options.UnicodeMode )
            {
            case UnicodeModeEnum.Ascii:
                o.Append( " UA " );
                break;
            case UnicodeModeEnum.Full:
                o.Append( " UF " );
                break;
            case UnicodeModeEnum.Javascript:
                o.Append( " UJ " );
                break;
            }

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = o.ToString( ),
                max_dfa_capacity = max_dfa_capacity,
                lookahead_context_max = lookahead_context_max,
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

            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );

            foreach( var m in response.matches )
            {
                int byte_start = m[0];
                int byte_end = m[1];

                int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end );
                int char_length = char_end - char_start;

                SimpleMatch? match = SimpleMatch.Create( char_start, char_length, stg );

                match.AddGroup( char_start, char_length, true, "0" ); // default group

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
