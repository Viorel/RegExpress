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


namespace DotStdPlugin
{
    class Matcher : IMatcher
    {
        class VersionResponse
        {
            public Version version { get; set; }
        }


        class ClientMatch
        {
            public int index { get; set; }
            public int length { get; set; }
            public List<ClientGroup> groups { get; set; } = new List<ClientGroup>( );
        }


        class ClientGroup
        {
            public bool success { get; set; }
            public int index { get; set; }
            public int length { get; set; }
            public string name { get; set; }
            public List<ClientCapture> captures { get; set; } = new List<ClientCapture>( );
        }


        class ClientCapture
        {
            public int index { get; set; }
            public int length { get; set; }
        }


        readonly string Pattern;
        readonly Options Options;


        public Matcher( string pattern, Options options )
        {
            Pattern = pattern;
            Options = options;
        }


        #region IMatcher

        public RegexMatches Matches( string text, ICancellable cnc )
        {
            bool eREGEX_MAX_COMPLEXITY_COUNT = string.IsNullOrWhiteSpace( Options.REGEX_MAX_COMPLEXITY_COUNT );
            Int32 REGEX_MAX_COMPLEXITY_COUNT = 0;
            if( !eREGEX_MAX_COMPLEXITY_COUNT )
            {
                if( !Int32.TryParse( Options.REGEX_MAX_COMPLEXITY_COUNT, out REGEX_MAX_COMPLEXITY_COUNT ) )
                {
                    throw new Exception( "Invalid option: '_REGEX_MAX_COMPLEXITY_COUNT'." );
                }
            }

            bool eREGEX_MAX_STACK_COUNT = string.IsNullOrWhiteSpace( Options.REGEX_MAX_STACK_COUNT );
            Int32 REGEX_MAX_STACK_COUNT = 0;
            if( !eREGEX_MAX_STACK_COUNT )
            {
                if( !Int32.TryParse( Options.REGEX_MAX_STACK_COUNT, out REGEX_MAX_STACK_COUNT ) )
                {
                    throw new Exception( "Invalid option: '_REGEX_MAX_STACK_COUNT'." );
                }
            }


            MemoryStream stdout_contents;
            string stderr_contents;


            Action<Stream> stdin_writer = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.Unicode, leaveOpen: false ) )
                {
                    bw.Write( "m" );
                    //bw.Write( (byte)0 ); // "version"
                    bw.Write( Pattern );
                    bw.Write( text );

                    bw.Write( Enum.GetName( Options.Grammar )! );

                    bw.Write( Convert.ToByte( Options.icase ) );
                    bw.Write( Convert.ToByte( Options.nosubs ) );
                    bw.Write( Convert.ToByte( Options.optimize ) );
                    bw.Write( Convert.ToByte( Options.collate ) );

                    bw.Write( Convert.ToByte( Options.match_not_bol ) );
                    bw.Write( Convert.ToByte( Options.match_not_eol ) );
                    bw.Write( Convert.ToByte( Options.match_not_bow ) );
                    bw.Write( Convert.ToByte( Options.match_not_eow ) );
                    bw.Write( Convert.ToByte( Options.match_any ) );
                    bw.Write( Convert.ToByte( Options.match_not_null ) );
                    bw.Write( Convert.ToByte( Options.match_continuous ) );
                    bw.Write( Convert.ToByte( Options.match_prev_avail ) );


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
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetClientExePath( ), null, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.Unicode ) )
            {
                return RegexMatches.Empty;
            }

            if( !string.IsNullOrWhiteSpace( stderr_contents ) )
            {
                throw new Exception( stderr_contents );
            }

            using( var br = new BinaryReader( stdout_contents, Encoding.Unicode ) )
            {

                List<IMatch> matches = new List<IMatch>( );
                ISimpleTextGetter stg = new SimpleTextGetter( text );
                SimpleMatch current_match = null;

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

        #endregion IMatcher


        public static Version GetVersion( ICancellable cnc )
        {
            MemoryStream stdout_contents;
            string stderr_contents;

            Action<Stream> stdinWriter = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.Unicode, leaveOpen: false ) )
                {
                    bw.Write( "v" );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetClientExePath( ), null, stdinWriter, out stdout_contents, out stderr_contents, EncodingEnum.Unicode ) )
            {
                return new Version( 0, 0 );
            }

            using( var br = new BinaryReader( stdout_contents, Encoding.Unicode ) )
            {
                string version_s = br.ReadString( );

                return Version.TryParse( version_s, out Version? version ) ? version : new Version( 0, 0 );
            }
        }


        static string GetClientExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string client_exe = Path.Combine( assembly_dir, @"StdClient.bin" );

            return client_exe;
        }

    }
}
