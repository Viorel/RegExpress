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


namespace BoostPlugin
{
    static partial class Matcher
    {
        internal static RegexEngineCapabilityEnum GetCapabilities( Options options )
        {
            return options.Grammar switch
            {
                GrammarEnum.normal => RegexEngineCapabilityEnum.HasCaptures,
                GrammarEnum.ECMAScript => RegexEngineCapabilityEnum.HasCaptures,
                GrammarEnum.JavaScript => RegexEngineCapabilityEnum.HasCaptures,
                GrammarEnum.JScript => RegexEngineCapabilityEnum.HasCaptures,
                GrammarEnum.perl => RegexEngineCapabilityEnum.HasCaptures,
                GrammarEnum.extended => RegexEngineCapabilityEnum.None,
                GrammarEnum.egrep => RegexEngineCapabilityEnum.None,
                GrammarEnum.awk => RegexEngineCapabilityEnum.None,
                GrammarEnum.basic => RegexEngineCapabilityEnum.None,
                GrammarEnum.sed => RegexEngineCapabilityEnum.None,
                GrammarEnum.grep => RegexEngineCapabilityEnum.None,
                GrammarEnum.emacs =>   RegexEngineCapabilityEnum.HasCaptures,
                GrammarEnum.literal => RegexEngineCapabilityEnum.NoGroups | RegexEngineCapabilityEnum.NoGroupIndex | RegexEngineCapabilityEnum.NoGroupSuccessFlag,
                _ => throw new NotImplementedException( ),
            };
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            // try identifying group names

            var regex_group_names = FindGroupsRegex( );

            string[] possible_group_names =
                regex_group_names
                    .Matches( pattern )
                    .Select( m => m.Groups["n"] )
                    .Where( g => g.Success )
                    .Select( g => g.Value )
                    .ToArray( );


            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "m" );
                bw.Write( (byte)'b' );

                bw.Write( pattern );
                bw.Write( text );

                bw.Write( Enum.GetName( options.Grammar )! );

                // Syntax options

                bw.Write( Convert.ToByte( options.icase ) );
                bw.Write( Convert.ToByte( options.nosubs ) );
                bw.Write( Convert.ToByte( options.optimize ) );
                bw.Write( Convert.ToByte( options.collate ) );
                bw.Write( Convert.ToByte( options.no_except ) );
                bw.Write( Convert.ToByte( options.no_mod_m ) );
                bw.Write( Convert.ToByte( options.no_mod_s ) );
                bw.Write( Convert.ToByte( options.mod_s ) );
                bw.Write( Convert.ToByte( options.mod_x ) );
                bw.Write( Convert.ToByte( options.no_empty_expressions ) );

                // Match options

                bw.Write( Convert.ToByte( options.match_not_bob ) );
                bw.Write( Convert.ToByte( options.match_not_eob ) );
                bw.Write( Convert.ToByte( options.match_not_bol ) );
                bw.Write( Convert.ToByte( options.match_not_eol ) );
                bw.Write( Convert.ToByte( options.match_not_bow ) );
                bw.Write( Convert.ToByte( options.match_not_eow ) );
                bw.Write( Convert.ToByte( options.match_any ) );
                bw.Write( Convert.ToByte( options.match_not_null ) );
                bw.Write( Convert.ToByte( options.match_continuous ) );
                bw.Write( Convert.ToByte( options.match_partial ) );
                bw.Write( Convert.ToByte( options.match_extra ) );
                bw.Write( Convert.ToByte( options.match_single_line ) );
                bw.Write( Convert.ToByte( options.match_prev_avail ) );
                bw.Write( Convert.ToByte( options.match_not_dot_newline ) );
                bw.Write( Convert.ToByte( options.match_not_dot_null ) );
                bw.Write( Convert.ToByte( options.match_posix ) );
                bw.Write( Convert.ToByte( options.match_perl ) );
                bw.Write( Convert.ToByte( options.match_nosubs ) );

                bw.Write( Convert.ToInt16( possible_group_names.Length ) );
                foreach( var n in possible_group_names ) bw.Write( n );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            List<IMatch> matches = [];
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
                    Int32 native_index = br.ReadInt32( );
                    Int32 native_length = br.ReadInt32( );
                    current_match = SimpleMatch.Create( native_index, native_length, stg );
                    matches.Add( current_match );
                    current_group = null;
                }
                break;
                case (byte)'g':
                {
                    if( current_match == null ) throw new Exception( "Invalid response [2]." );
                    bool success = br.ReadByte( ) != 0;
                    Int32 native_index = br.ReadInt32( );
                    Int32 native_length = br.ReadInt32( );
                    string name = br.ReadString( );

                    if( !success )
                    {
                        current_match.AddFailedGroup( name );
                        current_group = null;
                    }
                    else
                    {
                        current_group = current_match.AddSucceededGroup( native_index, native_length, name );
                    }
                }
                break;
                case (byte)'c':
                {
                    if( current_group == null ) throw new Exception( "Invalid response [3]." );
                    Int32 native_index = br.ReadInt32( );
                    Int32 native_length = br.ReadInt32( );
                    current_group.AddCapture( native_index, native_length );
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

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"BoostWorker.bin" );

            return worker_exe;
        }


        [GeneratedRegex( "\\(\\? ((?'a'')|<) (?'n'.*?) (?(a)'|>)", RegexOptions.ExplicitCapture | RegexOptions.IgnorePatternWhitespace )]
        private static partial Regex FindGroupsRegex( );
    }
}
