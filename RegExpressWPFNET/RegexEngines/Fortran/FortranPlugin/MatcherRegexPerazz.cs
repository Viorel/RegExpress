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
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace FortranPlugin
{
    static partial class MatcherRegexPerazz
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string adjusted_pattern = pattern.Replace( "\x1B", " " ).Replace( "\r", "\x1Br" ).Replace( "\n", "\x1Bn" );
            string adjusted_text = text.Replace( "\x1B", " " ).Replace( "\r", "\x1Br" ).Replace( "\n", "\x1Bn" );

            string flags = "";
            if( options.MatchAll ) flags += "A";
            //flags += "o"; // for overlapped matches

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.WriteLine( "m" );
                sw.WriteLine( adjusted_pattern );
                sw.WriteLine( adjusted_text );
                sw.WriteLine( flags );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            List<SimpleMatch> matches = [];
            SimpleTextGetter text_getter = new( text );
            SimpleMatch? match = null;
            string? line;

            while( ( line = ph.StreamReader.ReadLine( ) ) != null )
            {
                line = line.Trim( );

                if( line.Length == 0 ) continue;
                if( line.StartsWith( "d " ) ) continue; // (for debugging)

                {
                    Match m = ParseMatchRegex( ).Match( line );
                    if( m.Success )
                    {
                        int byte_start = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)

                        if( byte_start > 0 )
                        {
                            int byte_length = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture );
                            int byte_end = byte_start + byte_length - 1; // (inclusive, 1..)
                            if( byte_end < byte_start ) byte_end = byte_start - 1; // empty match

                            --byte_start; // (keep 'byte_end')

                            int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                            int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end ); // (exclusive)

                            match = SimpleMatch.Create( char_start, char_end - char_start, text_getter );
                            match.AddGroup( match.Index, match.Length, true, "" ); // default group

                            matches.Add( match );
                        }

                        continue;
                    }
                }

#if DEBUG
                if( !string.IsNullOrWhiteSpace( line ) )
                {
                    // invalid line
                    InternalConfig.HandleOtherCriticalError("Invalid Line");
                }
#endif
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"FortranRegexPerazzWorker.bin" );

            return worker_exe;
        }

        [GeneratedRegex( @"(?x)^\s* m \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );
    }
}
