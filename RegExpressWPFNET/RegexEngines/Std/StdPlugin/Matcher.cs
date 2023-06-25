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
    static class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            bool eREGEX_MAX_COMPLEXITY_COUNT = string.IsNullOrWhiteSpace( options.REGEX_MAX_COMPLEXITY_COUNT );
            Int32 REGEX_MAX_COMPLEXITY_COUNT = 0;
            if( !eREGEX_MAX_COMPLEXITY_COUNT )
            {
                if( !Int32.TryParse( options.REGEX_MAX_COMPLEXITY_COUNT, out REGEX_MAX_COMPLEXITY_COUNT ) )
                {
                    throw new Exception( "Invalid option: '_REGEX_MAX_COMPLEXITY_COUNT'." );
                }
            }

            bool eREGEX_MAX_STACK_COUNT = string.IsNullOrWhiteSpace( options.REGEX_MAX_STACK_COUNT );
            Int32 REGEX_MAX_STACK_COUNT = 0;
            if( !eREGEX_MAX_STACK_COUNT )
            {
                if( !Int32.TryParse( options.REGEX_MAX_STACK_COUNT, out REGEX_MAX_STACK_COUNT ) )
                {
                    throw new Exception( "Invalid option: '_REGEX_MAX_STACK_COUNT'." );
                }
            }


            MemoryStream? stdout_contents;
            string? stderr_contents;


            Action<Stream> stdin_writer = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.Unicode, leaveOpen: false ) )
                {
                    bw.Write( "m" );
                    //bw.Write( (byte)0 ); // "version"
                    bw.Write( (byte)'b' );

                    bw.Write( pattern );
                    bw.Write( text );

                    bw.Write( Enum.GetName( options.Grammar )! );

                    bw.Write( Convert.ToByte( options.icase ) );
                    bw.Write( Convert.ToByte( options.nosubs ) );
                    bw.Write( Convert.ToByte( options.optimize ) );
                    bw.Write( Convert.ToByte( options.collate ) );

                    bw.Write( Convert.ToByte( options.match_not_bol ) );
                    bw.Write( Convert.ToByte( options.match_not_eol ) );
                    bw.Write( Convert.ToByte( options.match_not_bow ) );
                    bw.Write( Convert.ToByte( options.match_not_eow ) );
                    bw.Write( Convert.ToByte( options.match_any ) );
                    bw.Write( Convert.ToByte( options.match_not_null ) );
                    bw.Write( Convert.ToByte( options.match_continuous ) );
                    bw.Write( Convert.ToByte( options.match_prev_avail ) );


                    if( eREGEX_MAX_COMPLEXITY_COUNT )
                    {
                        bw.Write( (byte)0 );
                    }
                    else
                    {
                        bw.Write( (byte)1 );
                        bw.Write( REGEX_MAX_COMPLEXITY_COUNT );
                    }

                    if( eREGEX_MAX_COMPLEXITY_COUNT )
                    {
                        bw.Write( (byte)0 );
                    }
                    else
                    {
                        bw.Write( (byte)1 );
                        bw.Write( REGEX_MAX_STACK_COUNT );
                    }

                    bw.Write( (byte)'e' );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.Unicode ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            using( var br = new BinaryReader( stdout_contents, Encoding.Unicode ) )
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
                        bool success = index >= 0;
                        current_match.AddGroup( success ? (int)index : 0, success ? (int)length : 0, success, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );
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
                using( var bw = new BinaryWriter( s, Encoding.Unicode, leaveOpen: false ) )
                {
                    bw.Write( "v" );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdinWriter, out stdout_contents, out stderr_contents, EncodingEnum.Unicode ) )
            {
                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            using( var br = new BinaryReader( stdout_contents, Encoding.Unicode ) )
            {
                string version_s = br.ReadString( );

                return version_s;
            }
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"StdWorker.bin" );

            return worker_exe;
        }

    }
}
