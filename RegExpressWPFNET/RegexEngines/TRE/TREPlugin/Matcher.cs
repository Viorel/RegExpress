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
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace TREPlugin
{
    class Matcher
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

                bw.Write( Convert.ToByte( options.REG_EXTENDED ) );
                bw.Write( Convert.ToByte( options.REG_ICASE ) );
                bw.Write( Convert.ToByte( options.REG_NOSUB ) );
                bw.Write( Convert.ToByte( options.REG_NEWLINE ) );
                bw.Write( Convert.ToByte( options.REG_LITERAL ) );
                bw.Write( Convert.ToByte( options.REG_RIGHT_ASSOC ) );
                bw.Write( Convert.ToByte( options.REG_UNGREEDY ) );

                bw.Write( Convert.ToByte( options.REG_NOTBOL ) );
                bw.Write( Convert.ToByte( options.REG_NOTEOL ) );

                bw.Write( Convert.ToByte( options.MatchAll ) );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            List<IMatch> matches = new( );
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
                    Int32 start = br.ReadInt32( );
                    Int32 end = br.ReadInt32( );
                    var length = end - start;
                    current_match = SimpleMatch.Create( (int)start, (int)length, stg );
                    matches.Add( current_match );
                    // default group
                    current_match.AddGroup( (int)start, length, true, "0" );
                }
                break;
                case (byte)'g':
                {
                    if( current_match == null ) throw new Exception( "Invalid response." );
                    Int32 start = br.ReadInt32( );
                    Int32 end = br.ReadInt32( );
                    var length = end - start;
                    bool success = start >= 0;
                    string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );
                    current_match.AddGroup( success ? (int)start : 0, success ? (int)length : 0, success, name );
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


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"TREWorker.bin" );

            return worker_exe;
        }

    }
}
