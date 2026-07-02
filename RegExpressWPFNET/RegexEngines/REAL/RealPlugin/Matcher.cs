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


namespace RealPlugin
{
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.InputEncoding = EncodingEnum.Unicode; //
            ph.OutputEncoding = EncodingEnum.Unicode; //
            ph.ErrorEncoding = EncodingEnum.ASCII;

            ph.BinaryWriter = bw =>
            {
                bw.Write( (byte)'b' );

                bw.Write( pattern );
                bw.Write( text );
                bw.Write( options.icase );
                bw.Write( options.multiline );
                bw.Write( options.dotall );
                //bw.Write( options.bytes ); // not supported here
                bw.Write( options.verbose );
                bw.Write( options.ecma );
                bw.Write( options.ascii );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

            var br = ph.BinaryReader;

            if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response [1]." );

            // read names

            Dictionary<string, int> names = [];

            for(; ; )
            {
                char b = (char)br.ReadByte( );
                if( b == '-' ) break;

                switch( b )
                {
                case 'n':
                {
                    string name = br.ReadString( );
                    int group_index = checked((int)br.ReadUInt64( ));
                    names.Add( name, group_index );
                }
                break;
                default:
                    throw new Exception( "Invalid response [2]." );
                }
            }

            // read matches

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );
            SimpleMatch? current_match = null;
            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            for(; ; )
            {
                char b = (char)br.ReadByte( );
                if( b == 'e' ) break;

                switch( b )
                {
                case 'm':
                {
                    int start = checked((int)br.ReadUInt64( )); // (UTF-8 index)
                    int end = checked((int)br.ReadUInt64( )); // (UTF-8 index)
                    //int length = end - start;
                    int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, start );
                    int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, end );
                    int char_length = char_end - char_start;

                    current_match = SimpleMatch.Create( char_start, char_length, stg );

                    // default group
                    current_match.AddGroup( char_start, char_length, true, "0" );

                    matches.Add( current_match );
                }
                break;
                case 'g':
                {
                    if( current_match == null ) throw new Exception( "Invalid response [3]." );

                    int group_index = current_match.Groups.Count( );

                    UInt64 start = br.ReadUInt64( ); // (UTF-8 index)
                    UInt64 end = br.ReadUInt64( ); // (UTF-8 index)

                    bool success = start < UInt64.MaxValue;

                    string? name = names.Where( p => p.Value == group_index ).Select( p => p.Key ).FirstOrDefault( );
                    name ??= group_index.ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        current_match.AddGroup( 0, 0, false, name );
                    }
                    else
                    {
                        int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, checked((int)start) );
                        int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, checked((int)end) );
                        int char_length = char_end - char_start;

                        current_match.AddGroup( char_start, char_length, true, name );
                    }
                }
                break;
                default:
                    throw new Exception( "Invalid response [4]." );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        private static string? AdjustErrorMessage( string error, string pattern )
        {
            // try to show character offset based on byte offset, which is used by REAL in error messages;
            // example of error message: "regex_error at 3: ..."

            Match m = RegexExtractByteOffset( ).Match( error );

            if( m.Success && int.TryParse( m.Groups[1].Value, out int byte_offset ) )
            {
                try
                {
                    byte[] utf8_bytes = Encoding.UTF8.GetBytes( pattern );
                    int char_offset = Encoding.UTF8.GetCharCount( utf8_bytes, 0, byte_offset );

                    if( char_offset != byte_offset )
                    {
                        string new_message = $"{error.TrimEnd( )}{Environment.NewLine}{Environment.NewLine}(character offset: {char_offset})";

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
            string worker_exe = Path.Combine( assembly_dir, @"RealWorker.bin" );

            return worker_exe;
        }

        [GeneratedRegex( @"^regex_error at (\d+): " )]
        private static partial Regex RegexExtractByteOffset( );
    }
}
