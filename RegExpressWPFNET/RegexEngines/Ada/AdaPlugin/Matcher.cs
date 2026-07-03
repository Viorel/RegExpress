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

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = new
                {
                    options.Case_Insensitive,
                    options.Single_Line,
                    options.Multiple_Lines,
                }
            };

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
            Utf8IndexConverter index_converter = new( text ); // (actually only ASCII is supported, therefore it works like an identity converter)
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
                        int native_start = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)
                        int native_end = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                        if( native_start <= 0 )
                        {
                            throw new Exception( $"Invalid output: {line}" );
                        }

                        // to 0... and exclusive end
                        --native_start;
                        --native_end;
                        if( native_end < native_start ) native_end = native_start + 1; else ++native_end; // 'end < start' in case of empty matches
                        int native_length = native_end - native_start;

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        current_match = SimpleMatch.Create( native_start, native_length, char_start, char_length, text_getter );
                        current_match.AddDefaultGroup( );
                        matches.Add( current_match );

                        continue;
                    }
                }

                {
                    Match g = ParseGroupRegex( ).Match( line );
                    if( g.Success )
                    {
                        if( current_match == null ) throw new ApplicationException( );

                        int native_start = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture ); // (1..)
                        int native_end = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture ); // (inclusive, 1..)

                        bool success = native_start > 0;

                        string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                        if( !success )
                        {
                            current_match.AddFailedGroup( name );
                        }
                        else
                        {
                            // to 0... and exclusive end
                            --native_start;
                            --native_end;
                            if( native_end < native_start ) native_end = native_start + 1; else ++native_end; // 'end < start' in case of empty matches
                            int native_length = native_end - native_start;

                            (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                            SimpleGroup group = current_match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                        }

                        continue;
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
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
