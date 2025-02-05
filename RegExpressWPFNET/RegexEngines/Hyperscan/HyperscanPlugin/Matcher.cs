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
    static class Matcher
    {
        static readonly Encoding AsciiEncodingWithExceptionFallback = Encoding.GetEncoding( Encoding.ASCII.WebName, new EncoderExceptionFallback( ), new DecoderExceptionFallback( ) );


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            if( !string.IsNullOrWhiteSpace( options.LevenshteinDistance ) && !UInt32.TryParse( options.LevenshteinDistance, out var _ ) )
            {
                throw new ApplicationException( "Invalid Levenshtein Distance." );
            }

            if( !string.IsNullOrWhiteSpace( options.HammingDistance ) && !UInt32.TryParse( options.HammingDistance, out var _ ) )
            {
                throw new ApplicationException( "Invalid Hamming Distance." );
            }

            if( !string.IsNullOrWhiteSpace( options.MinOffset ) && !UInt32.TryParse( options.MinOffset, out var _ ) )
            {
                throw new ApplicationException( "Invalid Min Offset." );
            }

            if( !string.IsNullOrWhiteSpace( options.MaxOffset ) && !UInt32.TryParse( options.MaxOffset, out var _ ) )
            {
                throw new ApplicationException( "Invalid Max Offset." );
            }

            if( !string.IsNullOrWhiteSpace( options.MinLength ) && !UInt32.TryParse( options.MinLength, out var _ ) )
            {
                throw new ApplicationException( "Invalid Min Length." );
            }

            if( !options.HS_FLAG_UTF8 )
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
                    throw new Exception( "The pattern and text contain non-ascii characters. (The 'HS_FLAG_UTF8' flag is required)." );
                }
                if( is_bad_pattern || is_bad_text )
                {
                    throw new Exception( $"The {( is_bad_pattern ? "pattern" : "text" )} contains non-ascii characters. (The 'HS_FLAG_UTF8' flag is required)." );
                }
            }

            UInt32 flags = 0;

            if( options.HS_FLAG_CASELESS ) flags |= 1 << 0;
            if( options.HS_FLAG_DOTALL ) flags |= 1 << 1;
            if( options.HS_FLAG_MULTILINE ) flags |= 1 << 2;
            if( options.HS_FLAG_SINGLEMATCH ) flags |= 1 << 3;
            if( options.HS_FLAG_ALLOWEMPTY ) flags |= 1 << 4;
            if( options.HS_FLAG_UTF8 ) flags |= 1 << 5;
            if( options.HS_FLAG_UCP ) flags |= 1 << 6;
            if( options.HS_FLAG_PREFILTER ) flags |= 1 << 7;
            if( options.HS_FLAG_SOM_LEFTMOST ) flags |= 1 << 8;
            //if( options.HS_FLAG_COMBINATION ) flags |= 1 << 9;
            if( options.HS_FLAG_QUIET ) flags |= 1 << 10;


            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "m" );
                bw.Write( (byte)'b' );
                bw.Write( pattern );
                bw.Write( text );
                bw.Write( flags );
                bw.Write( string.IsNullOrWhiteSpace( options.LevenshteinDistance ) ? UInt32.MaxValue : UInt32.Parse( options.LevenshteinDistance ) );
                bw.Write( string.IsNullOrWhiteSpace( options.HammingDistance ) ? UInt32.MaxValue : UInt32.Parse( options.HammingDistance ) );
                bw.Write( string.IsNullOrWhiteSpace( options.MinOffset ) ? UInt32.MaxValue : UInt32.Parse( options.MinOffset ) );
                bw.Write( string.IsNullOrWhiteSpace( options.MaxOffset ) ? UInt32.MaxValue : UInt32.Parse( options.MaxOffset ) );
                bw.Write( string.IsNullOrWhiteSpace( options.MinLength ) ? UInt32.MaxValue : UInt32.Parse( options.MinLength ) );
                bw.Write( checked((byte)options.Mode) );
                bw.Write( checked((byte)options.ModeSom) );
                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            string r = br.ReadString( );

            if( r != "r" )
            {
                throw new Exception( "Unknown result" );
            }

            List<IMatch> matches = new( );
            SimpleTextGetter stg = new( text );

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


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

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
            string worker_exe = Path.Combine( assembly_dir, @"HyperscanWorker.bin" );

            return worker_exe;
        }

    }
}
