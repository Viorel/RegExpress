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


namespace SubRegPlugin
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
                throw new Exception( string.Format( "SubReg only supports ASCII character encoding.\r\nThe pattern contains an invalid character at position {0}.", exc.Index ) );
            }

            try
            {
                _ = StrictAsciiEncoding.GetBytes( text );
            }
            catch( EncoderFallbackException exc )
            {
                throw new Exception( string.Format( "SubReg only supports ASCII character encoding.\r\nThe text contains an invalid character at position {0}.", exc.Index ) );
            }

            if( string.IsNullOrWhiteSpace( options.max_depth ) || !Int32.TryParse( options.max_depth, out int max_depth ) || max_depth < 0 )
            {
                throw new Exception( string.Format( "Invalid maximum depth. Enter a number between 0 and {0}", Int32.MaxValue ) );
            }


            MemoryStream? stdout_contents;
            string? stderr_contents;


            Action<Stream> stdin_writer = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.ASCII, leaveOpen: false ) )
                {
                    bw.Write( "m" );

                    bw.Write( (byte)'b' );

                    bw.Write( pattern );
                    bw.Write( text );
                    bw.Write( max_depth );

                    bw.Write( (byte)'e' );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            using( var br = new BinaryReader( stdout_contents, Encoding.UTF8 ) )
            {
                List<IMatch> matches = new List<IMatch>( );
                ISimpleTextGetter stg = new SimpleTextGetter( text );
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
                    }
                    break;
                    case (byte)'g':
                    {
                        if( current_match == null ) throw new Exception( "Invalid response." );
                        Int64 index = br.ReadInt64( );
                        Int64 length = br.ReadInt64( );
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
        }


        public static string? GetVersion( ICancellable cnc )
        {
            MemoryStream? stdout_contents;
            string? stderr_contents;

            Action<Stream> stdinWriter = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.UTF8, leaveOpen: false ) )
                {
                    bw.Write( "v" );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdinWriter, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            using( var br = new BinaryReader( stdout_contents, Encoding.UTF8 ) )
            {
                string version_s = br.ReadString( );

                return version_s;
            }
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"SubRegWorker.bin" );

            return worker_exe;
        }

    }
}
