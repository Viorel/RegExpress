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


namespace TinyRegexCPlugin
{
    static class Matcher
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
                throw new Exception( string.Format( "Tiny-regex-c only supports ASCII character encoding.\r\nThe pattern contains an invalid character at position {0}.", exc.Index ) );
            }

            try
            {
                _ = StrictAsciiEncoding.GetBytes( text );
            }
            catch( EncoderFallbackException exc )
            {
                throw new Exception( string.Format( "Tiny-regex-c only supports ASCII character encoding.\r\nThe text contains an invalid character at position {0}.", exc.Index ) );
            }

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "m" );

                bw.Write( (byte)'b' );

                bw.Write( pattern );
                bw.Write( text );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );
            SimpleMatch? current_match = null;

            if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response." );

            bool done = false;

            while( !done )
            {
                switch( br.ReadByte( ) )
                {
                case (byte)'m':
                {
                    Int64 index = br.ReadInt64( );
                    Int64 length = br.ReadInt64( );
                    current_match = SimpleMatch.Create( (int)index, (int)length, stg );
                    matches.Add( current_match );
                    // default group
                    string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );
                    current_match.AddGroup( (int)index, (int)length, true, name );
                }
                break;
                case (byte)'e':
                    done = true;
                    break;
                default:
                    throw new Exception( "Invalid response." );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.BinaryWriter = bw =>
            {
                bw.Write( "v" );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string version_s = ph.BinaryReader.ReadString( );

            return version_s;
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"TinyRegexCWorker.bin" );

            return worker_exe;
        }
    }
}
