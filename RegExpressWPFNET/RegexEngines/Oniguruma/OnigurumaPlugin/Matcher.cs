using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Interop;
using Microsoft.VisualBasic.FileIO;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using static System.Net.Mime.MediaTypeNames;


namespace OnigurumaPlugin
{
    static class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "m" );
                bw.Write( (byte)'b' );

                bw.Write( pattern );
                bw.Write( text );

                WriteOptions( bw, options );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            List<IMatch> matches = new( );
            SimpleTextGetter stg = new( text );
            SimpleMatch? current_match = null;
            SimpleGroup? current_group = null;

            if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response [1]." );

            bool done = false;

            while( !done )
            {
                switch( br.ReadByte( ) )
                {
                case (byte)'m':
                {
                    Int32 index = br.ReadInt32( );
                    Int32 length = br.ReadInt32( );
                    current_match = SimpleMatch.Create( index, length, stg );
                    matches.Add( current_match );
                    current_group = null;
                }
                break;
                case (byte)'g':
                {
                    if( current_match == null ) throw new Exception( "Invalid response [2]." );
                    bool success = br.ReadByte( ) != 0;
                    Int32 index = br.ReadInt32( );
                    Int32 length = br.ReadInt32( );
                    string name = br.ReadString( );
                    current_group = current_match.AddGroup( success ? index : 0, success ? length : 0, success, name );
                }
                break;
                case (byte)'c':
                {
                    if( current_group == null ) throw new Exception( "Invalid response [3]." );
                    Int32 index = br.ReadInt32( );
                    Int32 length = br.ReadInt32( );
                    current_group.AddCapture( index, length );
                }
                break;
                case (byte)'e':
                    done = true;
                    break;
                default:
                    throw new Exception( "Invalid response [4]." );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;
            ph.BinaryWriter = bw =>
            {
                bw.Write( "v" );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string version_s = ph.BinaryReader.ReadString( );

            return version_s;
        }


        static void WriteOptions( BinaryWriter bw, Options options )
        {
            bw.Write( Enum.GetName( options.Syntax )! );

            // Compile-time options

            bw.Write( Convert.ToByte( options.ONIG_OPTION_SINGLELINE ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_MULTILINE ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_IGNORECASE ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_IGNORECASE_IS_ASCII ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_EXTEND ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_FIND_LONGEST ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_FIND_NOT_EMPTY ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_MATCH_WHOLE_STRING ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_NEGATE_SINGLELINE ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_CAPTURE_GROUP ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_DONT_CAPTURE_GROUP ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_WORD_IS_ASCII ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_DIGIT_IS_ASCII ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_SPACE_IS_ASCII ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_POSIX_IS_ASCII ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_TEXT_SEGMENT_WORD ) );

            // Search-time options

            bw.Write( Convert.ToByte( options.ONIG_OPTION_NOTBOL ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_NOTEOL ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_NOT_BEGIN_STRING ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_NOT_END_STRING ) );
            bw.Write( Convert.ToByte( options.ONIG_OPTION_NOT_BEGIN_POSITION ) );

            // Configuration

            bw.Write( Convert.ToByte( options.ONIG_SYN_OP2_ATMARK_CAPTURE_HISTORY ) );
            bw.Write( Convert.ToByte( options.ONIG_SYN_STRICT_CHECK_BACKREF ) );
        }


        public static Details? GetDetails( ICancellable cnc, Options options )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "d" );
                bw.Write( (byte)'b' );

                WriteOptions( bw, options );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            var sz = Marshal.SizeOf( typeof( Details ) );

            if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response [D1]." );

            byte[] bytes = br.ReadBytes( Marshal.SizeOf( typeof( Details ) ) );

            if( br.ReadByte( ) != 'e' ) throw new Exception( "Invalid response [D2]." );

            GCHandle gch = GCHandle.Alloc( bytes, GCHandleType.Pinned );
            try
            {
                nint addr = gch.AddrOfPinnedObject( );
                Details details = Marshal.PtrToStructure<Details>( addr )!;

                return details;
            }
            finally
            {
                gch.Free( );
            }
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"OnigurumaWorker.bin" );

            return worker_exe;
        }

    }
}
