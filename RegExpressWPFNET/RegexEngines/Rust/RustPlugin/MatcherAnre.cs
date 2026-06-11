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
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Interop;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace RustPlugin
{
    static class MatcherAnre
    {
        sealed class GroupResponse
        {
            [JsonPropertyName("n")]
            public string? name { get; set; }

            [JsonPropertyName( "r" )]
            public int[]? range { get; set; } // start, end
        }

        sealed class MatchesResponse
        {
            [JsonPropertyName( "matches" )]
            public GroupResponse[][]? matches { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
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

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

            if( response == null || response.matches == null ) throw new Exception( "Null response" );

            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            List<IMatch> matches = [];
            SimpleTextGetter stg = new SimpleTextGetter( text );

            foreach( var m in response.matches )
            {
                SimpleMatch? match = null;

                for( int group_index = 0; group_index < m.Length; group_index++ )
                {
                    GroupResponse g = m[group_index];

                    // (Currently there is no difference between failed match and succeeded empty group; failed groups are returned as [0, 0]).

                    int byte_start = g.range![0];
                    int byte_end = g.range![1];

                    int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                    int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end );
                    int char_length = char_end - char_start;

                    if( group_index == 0 )
                    {
                        Debug.Assert( match == null );
                        Debug.Assert( char_start >= 0 );

                        match = SimpleMatch.Create( char_start, char_length, stg );
                    }

                    Debug.Assert( match != null );

                    bool success = char_length > 0;

                    string? name = g.name;
                    if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                    if( success )
                    {
                        match.AddGroup( char_start, char_length, true, name );
                    }
                    else
                    {
                        match.AddGroup( 0, 0, false, name );
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
            string worker_exe = Path.Combine( assembly_dir, @"RustAnreWorker.bin" );

            return worker_exe;
        }
    }
}
