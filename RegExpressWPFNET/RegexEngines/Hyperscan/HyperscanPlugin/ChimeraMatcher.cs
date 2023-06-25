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


namespace HyperscanPlugin
{
    static class ChimeraMatcher
    {
        static readonly Encoding AsciiEncodingWithExceptionFallback = Encoding.GetEncoding( Encoding.ASCII.WebName, new EncoderExceptionFallback( ), new DecoderExceptionFallback( ) );


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, ChimeraOptions options )
        {
            if( !string.IsNullOrWhiteSpace( options.MatchLimit ) && !UInt32.TryParse( options.MatchLimit, out var _ ) )
            {
                throw new ApplicationException( "Invalid Match Limit." );
            }

            if( !string.IsNullOrWhiteSpace( options.MatchLimitRecursion ) && !UInt32.TryParse( options.MatchLimitRecursion, out var _ ) )
            {
                throw new ApplicationException( "Invalid Recursion Limit." );
            }

            if( !options.CH_FLAG_UTF8 )
            {
                bool is_bad_pattern = false;
                try
                {
                    AsciiEncodingWithExceptionFallback.GetByteCount( pattern );
                }
                catch( EncoderFallbackException )
                {
                    is_bad_pattern = true;
                }

                bool is_bad_text = false;
                try
                {
                    AsciiEncodingWithExceptionFallback.GetByteCount( text );
                }
                catch( EncoderFallbackException )
                {
                    is_bad_text = true;
                }

                if( is_bad_pattern && is_bad_text )
                {
                    throw new Exception( "The pattern and text contain non-ascii characters. (The 'CH_FLAG_UTF8' flag is required)." );
                }
                if( is_bad_pattern || is_bad_text )
                {
                    throw new Exception( $"The {( is_bad_pattern ? "pattern" : "text" )} contains non-ascii characters. (The 'CH_FLAG_UTF8' flag is required)." );
                }
            }

            UInt32 flags = 0;

            if( options.CH_FLAG_CASELESS ) flags |= 1 << 0;
            if( options.CH_FLAG_DOTALL ) flags |= 1 << 1;
            if( options.CH_FLAG_MULTILINE ) flags |= 1 << 2;
            if( options.CH_FLAG_SINGLEMATCH ) flags |= 1 << 3;
            if( options.CH_FLAG_UTF8 ) flags |= 1 << 4;
            if( options.CH_FLAG_UCP ) flags |= 1 << 5;

            MemoryStream? stdout_contents;
            string? stderr_contents;

            Action<Stream> stdin_writer = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.UTF8, leaveOpen: false ) )
                {
                    bw.Write( "chm" );
                    bw.Write( (byte)'b' );
                    bw.Write( pattern );
                    bw.Write( text );
                    bw.Write( flags );
                    bw.Write( string.IsNullOrWhiteSpace( options.MatchLimit ) ? UInt32.MaxValue : UInt32.Parse( options.MatchLimit ) );
                    bw.Write( string.IsNullOrWhiteSpace( options.MatchLimitRecursion ) ? UInt32.MaxValue : UInt32.Parse( options.MatchLimitRecursion ) );
                    bw.Write( checked((byte)options.Mode) );
                    bw.Write( (byte)'e' );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty; // (cancelled)
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            stdout_contents.Position = 0;

            using( var br = new BinaryReader( stdout_contents, Encoding.UTF8 ) )
            {
                string r = br.ReadString( );

                if( r != "r" )
                {
                    throw new Exception( "Unknown result" );
                }

                string code = br.ReadString( );

                switch( code )
                {
                case "e":
                    string error = br.ReadString( );
                    throw new Exception( error );
                case "m":
                    List<IMatch> matches = new List<IMatch>( );
                    ISimpleTextGetter stg = new SimpleTextGetter( text );

                    byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

                    int count = checked((int)br.ReadUInt64( ));

                    for( int i = 0; i < count; ++i )
                    {
                        int byte_index = checked((int)br.ReadUInt64( ));
                        int byte_length = checked((int)br.ReadUInt64( ));

                        int char_index = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_index );
                        int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_index + byte_length );
                        int char_length = char_end - char_index;

                        var m = SimpleMatch.Create( char_index, char_length, stg );

                        int group_count = checked((int)br.ReadUInt64( ));

                        if( group_count == 0 )
                        {
                            m.AddGroup( char_index, char_length, true, "0" ); // default group
                        }
                        else
                        {
                            for( int k = 0; k < group_count; ++k )
                            {
                                bool success = br.ReadUInt32( ) != 0;

                                Debug.Assert( !( k == 0 && !success ) ); // the default group must succeed

                                byte_index = checked((int)br.ReadUInt64( ));
                                byte_length = checked((int)br.ReadUInt64( ));

                                if( !success )
                                {
                                    byte_index = 0;
                                    byte_length = 0;
                                }

                                char_index = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_index );
                                char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_index + byte_length );
                                char_length = char_end - char_index;

                                m.AddGroup( char_index, char_length, success, k.ToString( CultureInfo.InvariantCulture ) );
                            }
                        }

                        matches.Add( m );
                    }

                    return new RegexMatches( matches.Count, matches );

                default:
                    throw new Exception( $"Unknown code: '{code}'" );
                }
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
                    bw.Write( "chv" );
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
            string worker_exe = Path.Combine( assembly_dir, @"HyperscanWorker.bin" );

            return worker_exe;
        }

    }
}
