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
        internal static RegexEngineCapabilityEnum GetCapabilities( Options options )
        {
            return RegexEngineCapabilityEnum.None;
        }

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
                    Int32 native_start = br.ReadInt32( );
                    Int32 native_end = br.ReadInt32( );
                    int native_length = native_end - native_start;
                    current_match = SimpleMatch.Create( (int)native_start, (int)native_length, stg );
                    current_match.AddDefaultGroup( );
                    matches.Add( current_match );
                }
                break;
                case (byte)'g':
                {
                    if( current_match == null ) throw new Exception( "Invalid response." );
                    Int32 native_start = br.ReadInt32( );
                    Int32 native_end = br.ReadInt32( );
                    int native_length = native_end - native_start;
                    bool success = native_start >= 0;
                    string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );
                    if( !success )
                    {
                        current_match.AddFailedGroup( name );
                    }
                    else
                    {
                        current_match.AddSucceededGroup( native_start, native_length, name );
                    }
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

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"TREWorker.bin" );

            return worker_exe;
        }
    }
}
