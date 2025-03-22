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
using RegExpressLibrary.Matches.Simple;

namespace RustPlugin
{
    internal static class MatcherFancyRegex
    {
        sealed class VersionResponse
        {
            public string? version { get; set; }
        }

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

            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            var o = new StringBuilder( );

            if( options.case_insensitive ) o.Append( "i" );

            var obj = new
            {
                s = options.@struct,
                p = pattern,
                t = text,
                o = o.ToString( ),
                bl = options.backtrack_limit?.Trim( ) ?? "",
                dsl = options.delegate_size_limit?.Trim( ) ?? "",
                ddsl = options.delegate_dfa_size_limit?.Trim( ) ?? "",
            };

            string json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( ph.OutputStream );

            if( response == null || response.matches == null || response.names == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = null;

            foreach( var m in response.matches )
            {
                SimpleMatch? match = null;

                for( int group_index = 0; group_index < m.Length; group_index++ )
                {
                    int[] g = m[group_index];
                    bool success = g.Length == 2;

                    int byte_start = success ? g[0] : 0;
                    int byte_end = success ? g[1] : 0;

                    int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                    int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end );
                    int char_length = char_end - char_start;

                    if( group_index == 0 )
                    {
                        Debug.Assert( match == null );
                        Debug.Assert( success );

                        stg ??= new SimpleTextGetter( text );

                        match = SimpleMatch.Create( char_start, char_end - char_start, stg );
                    }

                    Debug.Assert( match != null );

                    string name = response.names[group_index];
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
            string worker_exe = Path.Combine( assembly_dir, @"RustFancyWorker.bin" );

            return worker_exe;
        }
    }
}
