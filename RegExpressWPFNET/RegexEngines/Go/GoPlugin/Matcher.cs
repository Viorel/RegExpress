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
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace GoPlugin
{
    class Matcher
    {

        // example:     {"Names":["","nameA","nameB",""],"Matches":[[3,6,4,5,-1,-1,5,6],[7,10,8,9,-1,-1,9,10]]}
        // pattern was: "a(?<nameA>.)(?<nameB>Z)?(.)"
        // text was:    "xx abc ade"
        // the indices are for UTF-8 bytes

        public class RootObject
        {
            public string[]? Names { get; set; }
            public int[][]? Matches { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string flags = $"{( options.posix_syntax ? "P" : "" )}{( options.longest_match ? "L" : "" )}{( options.literal ? "Q" : "" )}";

            var data = new { pattern, text, flags };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

#if DEBUG
            using StreamReader sr = new( ph.OutputStream );
            string output = sr.ReadToEnd( );
            RootObject? root_object = JsonSerializer.Deserialize<RootObject>( output );
#else
            RootObject? root_object = JsonSerializer.Deserialize<RootObject>( ph.OutputStream );
#endif

            if( root_object == null ) throw new Exception( "Invalid response." );

            List<IMatch> matches = [];

            if( root_object.Matches != null )
            {
                SimpleTextGetter stg = new( text );

                byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

                foreach( int[] m in root_object.Matches )
                {
                    if( m.Length < 2 || ( m.Length % 2 ) != 0 ) throw new Exception( $"Invalid length: {m.Length}." );

                    SimpleMatch match;

                    {
                        // main group

                        Debug.Assert( m[0] >= 0 );
                        Debug.Assert( m[1] >= m[0] );

                        int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, m[0] );
                        int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, m[1] );
                        Debug.Assert( char_end >= char_start );
                        int char_length = char_end - char_start;

                        match = SimpleMatch.Create( char_start, char_length, stg );
                        match.AddGroup( char_start, char_length, true, "0" );
                    }

                    {
                        // other groups

                        for( int i = 2; i < m.Length; i += 2 )
                        {
                            int group_index = i / 2;

                            string? name = root_object.Names?[group_index];
                            if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                            bool success = m[i] >= 0;

                            if( success )
                            {
                                int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, m[i] );
                                int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, m[i + 1] );
                                int char_length = char_end - char_start;

                                match.AddGroup( char_start, char_length, true, name );
                            }
                            else
                            {
                                match.AddGroup( 0, 0, false, name );
                            }
                        }
                    }

                    matches.Add( match );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            return "1.26.4"; // TODO: get from worker
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"GoWorker.bin" );

            return worker_exe;
        }

    }
}
