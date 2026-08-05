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
using System.Threading.Tasks;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;

namespace RustPlugin
{
    internal static class MatcherRegress
    {
        sealed class NamedGroupResponse
        {
            [JsonPropertyName( "n" )]
            public string? Name { get; set; }

            [JsonPropertyName( "r" )]
            public int[]? Range { get; set; }
        }

        sealed class MatchResponse
        {
            [JsonPropertyName( "g" )]
            public int[][]? Groups { get; set; }

            [JsonPropertyName( "ng" )]
            public NamedGroupResponse[]? NamedGroups { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.crate == CrateEnum.regex_lite );

            if( options.@struct == StructEnum.None )
            {
                throw new ApplicationException( "Invalid struct." );
            }

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = new
                {
                    options.case_insensitive,
                    options.multi_line,
                    options.dot_matches_new_line,
                    options.unicode,
                    options.unicode_sets,
                    options.no_opt,
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

            MatchResponse[]? response = JsonSerializer.Deserialize<MatchResponse[]>( ph.OutputStream );

            if( response == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in response )
            {
                SimpleMatch? match = null;

                List<string> assigned_names = [];

                for( int group_index = 0; group_index < m.Groups!.Length; group_index++ )
                {
                    int[] g = m.Groups[group_index];
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

                    string? name = null;
                    if( success )
                    {
                        name = m.NamedGroups!
                            .Where( ng => ng.Range?.Length == 2 && ng.Range[0] == g[0] && ng.Range[1] == g[1] )
                            .Select( ng => ng.Name )
                            .Where( n => !assigned_names.Contains( n! ) )
                            .FirstOrDefault( );
                        if( name != null ) assigned_names.Add( name );
                    }
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
            string worker_exe = Path.Combine( assembly_dir, @"RustRegressWorker.bin" );

            return worker_exe;
        }
    }
}
