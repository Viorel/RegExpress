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


namespace PCRE2Plugin
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

        public RegexMatches Matches( string text, ICancellable cnc )
        {
            MemoryStream? stdout_contents;
            string? stderr_contents;

            Action<Stream> stdin_writer = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.Unicode, leaveOpen: false ) )
                {
                    bw.Write( "m" );
                    bw.Write( (byte)'b' );

                    bw.Write( Pattern );
                    bw.Write( text );

                    bw.Write( Enum.GetName( Options.Algorithm )! );

                    // Compile options

                    bw.Write( Convert.ToByte( Options.PCRE2_ANCHORED ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_ALLOW_EMPTY_CLASS ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_ALT_BSUX ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_ALT_CIRCUMFLEX ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_ALT_VERBNAMES ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_CASELESS ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_DOLLAR_ENDONLY ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_DOTALL ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_DUPNAMES ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_ENDANCHORED ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTENDED ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTENDED_MORE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_FIRSTLINE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_LITERAL ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_MATCH_UNSET_BACKREF ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_MULTILINE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NEVER_BACKSLASH_C ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NEVER_UCP ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NEVER_UTF ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NO_AUTO_CAPTURE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NO_AUTO_POSSESS ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NO_DOTSTAR_ANCHOR ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NO_START_OPTIMIZE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_UCP ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_UNGREEDY ) );

                    // Extra compile options

                    bw.Write( Convert.ToByte( Options.PCRE2_EXTRA_ALLOW_SURROGATE_ESCAPES ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTRA_ALT_BSUX ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTRA_ESCAPED_CR_IS_LF ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTRA_MATCH_LINE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_EXTRA_MATCH_WORD ) );

                    // Match options

                    bw.Write( Convert.ToByte( Options.PCRE2_ANCHORED_mo ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_COPY_MATCHED_SUBJECT ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_ENDANCHORED_mo ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NOTBOL ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NOTEOL ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NOTEMPTY ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NOTEMPTY_ATSTART ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_NO_JIT ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_PARTIAL_HARD ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_PARTIAL_SOFT ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_DFA_SHORTEST ) );

                    // JIT Options

                    bw.Write( Convert.ToByte( Options.PCRE2_JIT_COMPLETE ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_JIT_PARTIAL_SOFT ) );
                    bw.Write( Convert.ToByte( Options.PCRE2_JIT_PARTIAL_HARD ) );

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
                    }
                    break;
                    case (byte)'g':
                    {
                        if( current_match == null ) throw new Exception( "Invalid response [2]." );
                        bool success = br.ReadByte( ) != 0;
                        Int32 index = br.ReadInt32( );
                        Int32 length = br.ReadInt32( );
                        string name = br.ReadString( );
                        current_match.AddGroup( success ? index : 0, success ? length : 0, success, name );
                    }
                    break;
                    case (byte)'e':
                        done = true;
                        break;
                    default:
                        throw new Exception( "Invalid response [3]." );
                    }
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
            string worker_exe = Path.Combine( assembly_dir, @"PCRE2Worker.bin" );

            return worker_exe;
        }

    }
}
