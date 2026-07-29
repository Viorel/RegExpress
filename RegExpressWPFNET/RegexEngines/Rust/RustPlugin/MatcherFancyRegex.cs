using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;

namespace RustPlugin
{
    internal static class MatcherFancyRegex
    {
        sealed class MatchesResponse
        {
            public string[]? names { get; set; }
            public int[][][]? matches { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            if( options.@struct == StructEnum.None )
            {
                throw new ApplicationException( "Invalid struct." );
            }

            bool is_builder = options.@struct == StructEnum.RegexBuilder;

            var obj = new
            {
                @struct = options.@struct,
                pattern = pattern,
                text = text,
                options = new
                {
                    options.case_insensitive,
                    options.multi_line,
                    options.ignore_whitespace,
                    options.dot_matches_new_line,
                    options.crlf,
                    options.unicode,
                    options.oniguruma_mode,
                    options.find_not_empty,
                    options.ignore_numbered_groups_when_named_groups_exist,
                    options.seek,
                    options.disallow_empty_match_at_eof_after_newline,
                    options.allow_input_assertion_overrides,
                    options.start_text,
                    options.end_text,
                    bytes_mode = options.bytes_mode.ToString(),
                    backtrack_limit = is_builder ? ValidationUtilities.ParseUInt32( "backtrack_limit", options.backtrack_limit ) : null,
                    delegate_size_limit = is_builder ? ValidationUtilities.ParseUInt32( "delegate_size_limit", options.delegate_size_limit ) : null,
                    delegate_dfa_size_limit = is_builder ? ValidationUtilities.ParseUInt32( "delegate_dfa_size_limit", options.delegate_dfa_size_limit ) : null,
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

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

            if( response == null || response.matches == null || response.names == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in response.matches )
            {
                SimpleMatch? match = null;

                for( int group_index = 0; group_index < m.Length; group_index++ )
                {
                    int[] g = m[group_index];
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

                    string name = response.names[group_index];
                    if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

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

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"RustFancyWorker.bin" );

            return worker_exe;
        }
    }
}
