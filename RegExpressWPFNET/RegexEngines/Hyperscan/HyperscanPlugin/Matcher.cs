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
    class Matcher : IMatcher
    {

        readonly string Pattern;
        readonly Options Options;


        public Matcher( string pattern, Options options )
        {
            Pattern = pattern;
            Options = options;
        }


        #region IMatcher


        static readonly Encoding AsciiEncodingWithExceptionFallback = Encoding.GetEncoding( Encoding.ASCII.WebName, new EncoderExceptionFallback( ), new DecoderExceptionFallback( ) );


        public RegexMatches Matches( string text, ICancellable cnc )
        {
            if( !string.IsNullOrWhiteSpace( Options.LevenshteinDistance ) && !UInt32.TryParse( Options.LevenshteinDistance, out var _ ) )
            {
                throw new ApplicationException( "Invalid Levenshtein Distance." );
            }

            if( !string.IsNullOrWhiteSpace( Options.HammingDistance ) && !UInt32.TryParse( Options.HammingDistance, out var _ ) )
            {
                throw new ApplicationException( "Invalid Hamming Distance." );
            }

            if( !string.IsNullOrWhiteSpace( Options.MinOffset ) && !UInt32.TryParse( Options.MinOffset, out var _ ) )
            {
                throw new ApplicationException( "Invalid Min Offset." );
            }

            if( !string.IsNullOrWhiteSpace( Options.MaxOffset ) && !UInt32.TryParse( Options.MaxOffset, out var _ ) )
            {
                throw new ApplicationException( "Invalid Max Offset." );
            }

            if( !string.IsNullOrWhiteSpace( Options.MinLength ) && !UInt32.TryParse( Options.MinLength, out var _ ) )
            {
                throw new ApplicationException( "Invalid Min Length." );
            }

            if( !Options.HS_FLAG_UTF8 )
            {
                bool is_bad_pattern = false;
                try
                {
                    AsciiEncodingWithExceptionFallback.GetByteCount( Pattern );
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
                    throw new Exception( "The pattern and text contain non-ascii characters. (The 'HS_FLAG_UTF8' flag is required)." );
                }
                if( is_bad_pattern || is_bad_text )
                {
                    throw new Exception( $"The {( is_bad_pattern ? "pattern" : "text" )} contains non-ascii characters. (The 'HS_FLAG_UTF8' flag is required)." );
                }
            }

            UInt32 flags = 0;

            if( Options.HS_FLAG_CASELESS ) flags |= 1 << 0;
            if( Options.HS_FLAG_DOTALL ) flags |= 1 << 1;
            if( Options.HS_FLAG_MULTILINE ) flags |= 1 << 2;
            if( Options.HS_FLAG_SINGLEMATCH ) flags |= 1 << 3;
            if( Options.HS_FLAG_ALLOWEMPTY ) flags |= 1 << 4;
            if( Options.HS_FLAG_UTF8 ) flags |= 1 << 5;
            if( Options.HS_FLAG_UCP ) flags |= 1 << 6;
            if( Options.HS_FLAG_PREFILTER ) flags |= 1 << 7;
            if( Options.HS_FLAG_SOM_LEFTMOST ) flags |= 1 << 8;
            //if( Options.HS_FLAG_COMBINATION ) flags |= 1 << 9;
            if( Options.HS_FLAG_QUIET ) flags |= 1 << 10;

            MemoryStream? stdout_contents;
            string? stderr_contents;

            Action<Stream> stdin_writer = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.UTF8, leaveOpen: false ) )
                {
                    bw.Write( "m" );
                    bw.Write( (byte)'b' );
                    bw.Write( Pattern );
                    bw.Write( text );
                    bw.Write( flags );
                    bw.Write( string.IsNullOrWhiteSpace( Options.LevenshteinDistance ) ? UInt32.MaxValue : UInt32.Parse( Options.LevenshteinDistance ) );
                    bw.Write( string.IsNullOrWhiteSpace( Options.HammingDistance ) ? UInt32.MaxValue : UInt32.Parse( Options.HammingDistance ) );
                    bw.Write( string.IsNullOrWhiteSpace( Options.MinOffset ) ? UInt32.MaxValue : UInt32.Parse( Options.MinOffset ) );
                    bw.Write( string.IsNullOrWhiteSpace( Options.MaxOffset ) ? UInt32.MaxValue : UInt32.Parse( Options.MaxOffset ) );
                    bw.Write( string.IsNullOrWhiteSpace( Options.MinLength ) ? UInt32.MaxValue : UInt32.Parse( Options.MinLength ) );
                    bw.Write( checked((byte)Options.Mode) );
                    bw.Write( checked((byte)Options.ModeSom) );
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

                List<IMatch> matches = new List<IMatch>( );
                ISimpleTextGetter stg = new SimpleTextGetter( text );

                //matches.Add( SimpleMatch.Create( 0, text.Length, stg ) );

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
                    m.AddGroup( char_index, char_length, true, "0" );

                    matches.Add( m );
                }

                return new RegexMatches( matches.Count, matches );
            }
        }

        #endregion IMatcher


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
            string worker_exe = Path.Combine( assembly_dir, @"HyperscanWorker.bin" );

            return worker_exe;
        }

    }
}
