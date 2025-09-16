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
    static class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            UInt32? depth_limit = null;

            if( !string.IsNullOrWhiteSpace( options.DepthLimit ) )
            {
                if( !UInt32.TryParse( options.DepthLimit, out var depth_limit0 ) )
                {
                    throw new ApplicationException( "Invalid depth_limit." );
                }
                else
                {
                    depth_limit = depth_limit0;
                }
            }

            UInt32? heap_limit = null;

            if( !string.IsNullOrWhiteSpace( options.HeapLimit ) )
            {
                if( !UInt32.TryParse( options.HeapLimit, out var heap_limit0 ) )
                {
                    throw new ApplicationException( "Invalid heap_limit." );
                }
                else
                {
                    heap_limit = heap_limit0;
                }
            }

            UInt32? match_limit = null;

            if( !string.IsNullOrWhiteSpace( options.MatchLimit ) )
            {
                if( !UInt32.TryParse( options.MatchLimit, out var match_limit0 ) )
                {
                    throw new ApplicationException( "Invalid match_limit." );
                }
                else
                {
                    match_limit = match_limit0;
                }
            }

            UInt64? max_pattern_compiled_length = null;

            if( !string.IsNullOrWhiteSpace( options.MaxPatternCompiledLength ) )
            {
                if( !UInt64.TryParse( options.MaxPatternCompiledLength, out var max_pattern_compiled_length0 ) )
                {
                    throw new ApplicationException( "Invalid max_pattern_compiled_length." );
                }
                else
                {
                    max_pattern_compiled_length = max_pattern_compiled_length0;
                }
            }

            UInt64? offset_limit = null;

            if( !string.IsNullOrWhiteSpace( options.OffsetLimit ) )
            {
                if( !UInt64.TryParse( options.OffsetLimit, out var offset_limit0 ) )
                {
                    throw new ApplicationException( "Invalid offset_limit." );
                }
                else
                {
                    offset_limit = offset_limit0;
                }
            }

            UInt32? parens_nest_limit = null;

            if( !string.IsNullOrWhiteSpace( options.ParensNestLimit ) )
            {
                if( !UInt32.TryParse( options.ParensNestLimit, out var parens_nest_limit0 ) )
                {
                    throw new ApplicationException( "Invalid parens_nest_limit." );
                }
                else
                {
                    parens_nest_limit = parens_nest_limit0;
                }
            }


            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "m" );
                bw.Write( (byte)'b' );

                bw.Write( pattern );
                bw.Write( text );

                bw.Write( Enum.GetName( options.Algorithm )! );
                bw.Write( options.Locale );

                // Compile options

                bw.Write( Convert.ToByte( options.PCRE2_ANCHORED ) );
                bw.Write( Convert.ToByte( options.PCRE2_ALLOW_EMPTY_CLASS ) );
                bw.Write( Convert.ToByte( options.PCRE2_ALT_BSUX ) );
                bw.Write( Convert.ToByte( options.PCRE2_ALT_CIRCUMFLEX ) );
                bw.Write( Convert.ToByte( options.PCRE2_ALT_EXTENDED_CLASS ) );
                bw.Write( Convert.ToByte( options.PCRE2_ALT_VERBNAMES ) );
                bw.Write( Convert.ToByte( options.PCRE2_CASELESS ) );
                bw.Write( Convert.ToByte( options.PCRE2_DOLLAR_ENDONLY ) );
                bw.Write( Convert.ToByte( options.PCRE2_DOTALL ) );
                bw.Write( Convert.ToByte( options.PCRE2_DUPNAMES ) );
                bw.Write( Convert.ToByte( options.PCRE2_ENDANCHORED ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTENDED ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTENDED_MORE ) );
                bw.Write( Convert.ToByte( options.PCRE2_FIRSTLINE ) );
                bw.Write( Convert.ToByte( options.PCRE2_LITERAL ) );
                bw.Write( Convert.ToByte( options.PCRE2_MATCH_UNSET_BACKREF ) );
                bw.Write( Convert.ToByte( options.PCRE2_MULTILINE ) );
                bw.Write( Convert.ToByte( options.PCRE2_NEVER_BACKSLASH_C ) );
                bw.Write( Convert.ToByte( options.PCRE2_NEVER_UCP ) );
                bw.Write( Convert.ToByte( options.PCRE2_NEVER_UTF ) );
                bw.Write( Convert.ToByte( options.PCRE2_NO_AUTO_CAPTURE ) );
                bw.Write( Convert.ToByte( options.PCRE2_NO_AUTO_POSSESS ) );
                bw.Write( Convert.ToByte( options.PCRE2_NO_DOTSTAR_ANCHOR ) );
                bw.Write( Convert.ToByte( options.PCRE2_NO_START_OPTIMIZE ) );
                bw.Write( Convert.ToByte( options.PCRE2_UCP ) );
                bw.Write( Convert.ToByte( options.PCRE2_UNGREEDY ) );
                bw.Write( Convert.ToByte( options.PCRE2_USE_OFFSET_LIMIT ) );

                // Extra compile options

                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ALLOW_SURROGATE_ESCAPES ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ALT_BSUX ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_BSD ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_BSS ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_BSW ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_DIGIT ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ASCII_POSIX ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_CASELESS_RESTRICT ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_ESCAPED_CR_IS_LF ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_MATCH_LINE ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_MATCH_WORD ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_NEVER_CALLOUT ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_NO_BS0 ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_PYTHON_OCTAL ) );
                bw.Write( Convert.ToByte( options.PCRE2_EXTRA_TURKISH_CASING ) );

                // Match options

                bw.Write( Convert.ToByte( options.PCRE2_ANCHORED_mo ) );
                bw.Write( Convert.ToByte( options.PCRE2_COPY_MATCHED_SUBJECT ) );
                bw.Write( Convert.ToByte( options.PCRE2_DISABLE_RECURSELOOP_CHECK ) );
                bw.Write( Convert.ToByte( options.PCRE2_ENDANCHORED_mo ) );
                bw.Write( Convert.ToByte( options.PCRE2_NOTBOL ) );
                bw.Write( Convert.ToByte( options.PCRE2_NOTEOL ) );
                bw.Write( Convert.ToByte( options.PCRE2_NOTEMPTY ) );
                bw.Write( Convert.ToByte( options.PCRE2_NOTEMPTY_ATSTART ) );
                bw.Write( Convert.ToByte( options.PCRE2_NO_JIT ) );
                bw.Write( Convert.ToByte( options.PCRE2_PARTIAL_HARD ) );
                bw.Write( Convert.ToByte( options.PCRE2_PARTIAL_SOFT ) );
                bw.Write( Convert.ToByte( options.PCRE2_DFA_SHORTEST ) );

                // JIT Options

                bw.Write( Convert.ToByte( options.PCRE2_JIT_COMPLETE ) );
                bw.Write( Convert.ToByte( options.PCRE2_JIT_PARTIAL_SOFT ) );
                bw.Write( Convert.ToByte( options.PCRE2_JIT_PARTIAL_HARD ) );
                bw.Write( Convert.ToByte( options.PCRE2_JIT_TEST_ALLOC ) );

                // Limits

                bw.WriteOptional( depth_limit  );
                bw.WriteOptional( heap_limit );
                bw.WriteOptional( match_limit );
                bw.WriteOptional( max_pattern_compiled_length );
                bw.WriteOptional( offset_limit );
                bw.WriteOptional( parens_nest_limit );


                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            List<IMatch> matches = new( );
            SimpleTextGetter stg = new( text );
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
            string worker_exe = Path.Combine( assembly_dir, @"PCRE2Worker.bin" );

            return worker_exe;
        }

    }
}
