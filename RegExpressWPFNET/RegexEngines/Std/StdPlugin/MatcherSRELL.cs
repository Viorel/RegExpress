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


namespace StdPlugin
{
    static class MatcherSRELL
    {
        static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

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

                bw.Write( Enum.GetName( options.Grammar )! );
                bw.Write( options.Locale ?? "" );

                bw.Write( Convert.ToByte( options.icase ) );
                bw.Write( Convert.ToByte( options.nosubs ) );
                bw.Write( Convert.ToByte( options.optimize ) );
                bw.Write( Convert.ToByte( options.collate ) );
                bw.Write( Convert.ToByte( options.multiline ) );
                bw.Write( Convert.ToByte( options.dotall ) );
                bw.Write( Convert.ToByte( options.unicodesets ) );
                bw.Write( Convert.ToByte( options.vmode ) );

                bw.Write( Convert.ToByte( options.match_not_bol ) );
                bw.Write( Convert.ToByte( options.match_not_eol ) );
                bw.Write( Convert.ToByte( options.match_not_bow ) );
                bw.Write( Convert.ToByte( options.match_not_eow ) );
                bw.Write( Convert.ToByte( options.match_any ) );
                bw.Write( Convert.ToByte( options.match_not_null ) );
                bw.Write( Convert.ToByte( options.match_continuous ) );
                bw.Write( Convert.ToByte( options.match_prev_avail ) );

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
                }
                break;
                case (byte)'g':
                {
                    if( current_match == null ) throw new Exception( "Invalid response." );
                    Int64 index = br.ReadInt64( );
                    Int64 length = br.ReadInt64( );
                    bool success = index >= 0;

                    string? name = null;
                    if( success ) name = br.ReadString( );

                    if( string.IsNullOrWhiteSpace( name ) ) name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                    current_match.AddGroup( success ? (int)index : 0, success ? (int)length : 0, success, name );
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
            string worker_exe = Path.Combine( assembly_dir, @"SrellWorker.bin" );

            return worker_exe;
        }

        internal static void StartGetVersion( Action<string?> setVersion )
        {
            if( LazyVersion.IsValueCreated )
            {
                setVersion( LazyVersion.Value );

                return;
            }

            Thread t = new( ( ) =>
            {
                setVersion( LazyVersion.Value );
            } )
            {
                IsBackground = true
            };

            t.Start( );
        }
    }
}
