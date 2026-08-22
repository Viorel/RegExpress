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
    static partial class MatcherRealRegex
    {
        sealed class MatchesResponse
        {
            public string[]? names { get; set; }
            public int[][][]? matches { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.crate == CrateEnum.real_regex );

            bool use_builder = options.UseBuilder;

            var obj = new
            {
                use_builder = use_builder,
                pattern = pattern,
                text = text,
                options = new
                {
                    options.case_insensitive,
                    options.multi_line,
                    options.dot_matches_new_line,
                    //options.crlf,
                    //options.swap_greed,
                    options.ignore_whitespace,
                    options.unicode,
                    options.fallback,
                    //options.octal,

                    //sl = use_builder ? ValidationUtilities.ParseUInt32( "size_limit", options.size_limit ) : null, // (no-op)
                    //dsl = use_builder ? ValidationUtilities.ParseUInt32( "dfa_size_limit", options.dfa_size_limit ) : null,
                    //nl = use_builder ? ValidationUtilities.ParseUInt32( "nest_limit", options.nest_limit ) : null,
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

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

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

        private static string? AdjustErrorMessage( string error, string pattern )
        {
            // try to show character offset based on byte offset, which is used by REAL in error messages;
            // example of error message: "syntax error at 3: ..."

            Match m = RegexExtractByteOffset( ).Match( error );

            if( m.Success && int.TryParse( m.Groups[1].Value, out int byte_offset ) )
            {
                try
                {
                    byte[] utf8_bytes = Encoding.UTF8.GetBytes( pattern );
                    int char_offset = Encoding.UTF8.GetCharCount( utf8_bytes, 0, byte_offset );

                    if( char_offset != byte_offset )
                    {
                        string new_message = $"{error.TrimEnd( )}{Environment.NewLine}at character index {char_offset}";

                        return new_message;
                    }
                }
                catch
                {
                    if( Debugger.IsAttached ) Debugger.Break( );

                    // ignore
                }
            }

            return error;
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"RustRealRegexWorker.bin" );

            return worker_exe;
        }

        [GeneratedRegex( @"^syntax error at (\d+): " )]
        private static partial Regex RegexExtractByteOffset( );
    }
}
