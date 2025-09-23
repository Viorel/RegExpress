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


namespace AdaPlugin
{
    static partial class Matcher
    {
        static readonly Encoding StrictAsciiEncoding = Encoding.GetEncoding( "ASCII", EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback );


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            try
            {
                _ = StrictAsciiEncoding.GetBytes( pattern );
            }
            catch( EncoderFallbackException exc )
            {
                throw new Exception( string.Format( "Ada engine only supports the ASCII character encoding.\r\nThe pattern contains an invalid character at position {0}.", exc.Index ) );
            }

            try
            {
                _ = StrictAsciiEncoding.GetBytes( text );
            }
            catch( EncoderFallbackException exc )
            {
                throw new Exception( string.Format( "Ada engine only supports the ASCII character encoding.\r\nThe text contains an invalid character at position {0}.", exc.Index ) );
            }

            string flags = "";
            if( options.Case_Insensitive ) flags += 'i';
            if( options.Single_Line ) flags += 's';
            if( options.Multiple_Lines ) flags += 'm';

            var obj = new { pattern = pattern, text = text, flags = flags };

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;

            ph.StreamWriter = sw =>
            {
                sw.WriteLine( JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions ) );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            List<SimpleMatch> matches = [];
            SimpleTextGetter text_getter = new( text );
            SimpleMatch? current_match = null;
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
                        int start = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)
                        int end = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                        if( start <= 0 )
                        {
                            throw new Exception( $"Invalid output: {line}" );
                        }

                        current_match = SimpleMatch.Create( start - 1, end >= start ? end - start + 1 : 0, text_getter ); // 'end < start' in case of empty matches
                        current_match.AddGroup( current_match.Index, current_match.Length, true, "" ); // default group
                        matches.Add( current_match );

                        continue;
                    }
                }

                {
                    Match g = ParseGroupRegex( ).Match( line );
                    if( g.Success )
                    {
                        int start = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)
                        int end = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                        bool success = start > 0;

                        if( current_match == null ) throw new InvalidOperationException( );

                        SimpleGroup group = current_match.AddGroup( success ? start - 1 : 0, success && end >= start ? end - start + 1 : 0, success, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );

                        continue;
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            return "15.2.0"; // TODO: determine programmatically
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"adaworker.bin" );

            return worker_exe;
        }


        [GeneratedRegex( @"(?x)^\s* m \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );

        [GeneratedRegex( @"(?x)^\s* g \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseGroupRegex( );
    }
}
