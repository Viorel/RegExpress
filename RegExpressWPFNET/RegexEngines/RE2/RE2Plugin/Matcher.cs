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


namespace RE2Plugin
{
    class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
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

                    bw.Write( Convert.ToByte( options.posix_syntax ) );
                    bw.Write( Convert.ToByte( options.longest_match ) );
                    bw.Write( Convert.ToByte( options.literal ) );
                    bw.Write( Convert.ToByte( options.never_nl ) );
                    bw.Write( Convert.ToByte( options.dot_nl ) );
                    bw.Write( Convert.ToByte( options.never_capture ) );
                    bw.Write( Convert.ToByte( options.case_sensitive ) );
                    bw.Write( Convert.ToByte( options.perl_classes ) );
                    bw.Write( Convert.ToByte( options.word_boundary ) );
                    bw.Write( Convert.ToByte( options.one_line ) );

                    bw.Write( Enum.GetName( options.anchor )! );

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
                        bool success = br.ReadByte( ) != 0;
                        Int64 index = br.ReadInt64( );
                        Int64 length = br.ReadInt64( );
                        string name = br.ReadString( );
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
            string worker_exe = Path.Combine( assembly_dir, @"RE2Worker.bin" );

            return worker_exe;
        }

    }
}
