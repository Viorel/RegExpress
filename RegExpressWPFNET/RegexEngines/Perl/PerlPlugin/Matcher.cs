using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using static System.Net.Mime.MediaTypeNames;


namespace PerlPlugin
{
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string? stdout_contents;
            string? stderr_contents;

            string modifiers = "";
            if( options.m ) modifiers += "m";
            if( options.s ) modifiers += "s";
            if( options.i ) modifiers += "i";
            if( options.x && !options.xx ) modifiers += "x";
            if( options.xx ) modifiers += "xx";
            if( options.n ) modifiers += "n";
            if( options.a && !options.aa ) modifiers += "a";
            if( options.aa ) modifiers += "aa";
            if( options.d ) modifiers += "d";
            if( options.u ) modifiers += "u";
            if( options.l ) modifiers += "l";
            if( options.g ) modifiers += "g";
            //if( options.c ) modifiers += "c";

            Action<StreamWriter> stdin_writer = new( sw =>
            {
                var json_obj = new { p = pattern, t = text, m = modifiers };
                string json = JsonSerializer.Serialize( json_obj, JsonUtilities.JsonOptions );

                sw.Write( json );
            } );

            if( !ProcessUtilities.InvokeExe( cnc, GetPerlExePath( ), new[] { "-CS", GetWorkerPath( ) }, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) )
            {
                string error_text = GetErrorRegex( ).Match( stderr_contents ).Groups[1].Value.Trim( );

                if( !string.IsNullOrWhiteSpace( error_text ) )
                {
                    // remove unneeded details about PerlWorker.pl
                    string error_message = RemoveUnneededErrorDetailsRegex( ).Replace( error_text, "" );

                    throw new Exception( error_message );
                }
            }

            // collect group names from Perl debugging details

            string debug_text = stderr_contents == null ? "" : GetDebugDetailsRegex( ).Match( stderr_contents ).Groups[1].Value.Trim( );

            List<string?> numbered_names = new( );

            foreach( Match m in GetGroupRegex( ).Matches( debug_text ) )
            {
                string name = m.Groups[2].Value;
                int number = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );

                // fill gap, reserve
                for( int i = numbered_names.Count; i <= number; ++i ) numbered_names.Add( null );

                Debug.Assert( numbered_names[number] == null || numbered_names[number] == name );

                numbered_names[number] = name;
            }

            if( stdout_contents == null ) throw new Exception( "Null response" );

            List<IMatch> matches = new( );

            using StringReader sr = new( stdout_contents );

            ISimpleTextGetter? stg = null;
            SurrogatePairsHelper? sph = null;

            SimpleMatch? match = null;

            string? line;

            while( ( line = sr.ReadLine( ) ) != null )
            {
                if( line == "\x1FM" )
                {
                    match = null;
                }
                else if( string.IsNullOrWhiteSpace( line ) )
                {
                    continue;
                }
                else
                {
                    Match m = ParseGroupResultRegex( ).Match( line );

                    if( m.Success )
                    {
                        int index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                        int length = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture );

                        int group_index = match == null ? 0 : match.Groups.Count( );
                        string? group_name = group_index < numbered_names.Count ? numbered_names[group_index] : null;
                        group_name ??= group_index.ToString( CultureInfo.InvariantCulture );

                        bool success = index >= 0;

                        if( success )
                        {
                            stg ??= new SimpleTextGetter( text );
                            sph ??= new( text, processSurrogatePairs: true );

                            var (text_index, text_length) = sph.ToTextIndexAndLength( index, length );

                            if( match == null )
                            {
                                match = SimpleMatch.Create( index, length, text_index, text_length, stg );
                                matches.Add( match );
                            }

                            match.AddGroup( index, length, text_index, text_length, true, group_name );
                        }
                        else
                        {
                            if( match == null ) throw new InvalidOperationException( );

                            Debug.Assert( group_index > 0 );

                            match.AddGroup( 0, 0, false, group_name );
                        }
                    }
                    else
                    {
                        if( Debugger.IsAttached ) Debugger.Break( );
                        // ignore
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            try
            {
                string? stdout_contents;
                string? stderr_contents;

                if( !ProcessUtilities.InvokeExe( NonCancellable.Instance, GetPerlExePath( ), new[] { "-e", "print 'V=', $^V" }, "", out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) ||
                    stdout_contents?.StartsWith( "V=" ) != true )
                {
                    if( Debugger.IsAttached ) Debugger.Break( );
                    Debug.WriteLine( "Cannot get Perl version: '{0}', '{1}'", stdout_contents, stderr_contents );

                    return null;
                }
                else
                {
                    stdout_contents = stdout_contents.Trim( );
                    string version = stdout_contents["V=".Length..];
                    if( version.StartsWith( "v" ) ) version = version[1..];

                    return version;
                }
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }
        }


        static string GetPerlExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string? assembly_dir = Path.GetDirectoryName( assembly_location );
            string perl_dir = Path.Combine( assembly_dir!, @"Perl-min\perl" );
            string perl_exe = Path.Combine( perl_dir, @"bin\perl.exe" );

            return perl_exe;
        }


        static string GetWorkerPath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_path = Path.Combine( assembly_dir, @"PerlWorker.pl" );

            return worker_path;
        }


        [GeneratedRegex( @"\u001FERR>(.*?)<\u001FERR", RegexOptions.Singleline )]
        private static partial Regex GetErrorRegex( );

        [GeneratedRegex( @"\s+at\s+.+\\PerlWorker.pl\s+line\s+\d+,\s+<STDIN>\s+line\s+\d+(?=\.\s*$)", RegexOptions.Singleline )]
        private static partial Regex RemoveUnneededErrorDetailsRegex( );

        [GeneratedRegex( @"\u001FDEBUG>(.*?)<\u001FDEBUG", RegexOptions.Singleline )]
        private static partial Regex GetDebugDetailsRegex( );

        [GeneratedRegex( @"^ \s* \d+: \s* CLOSE(\d+) \s+ '(.*?)' \s+ \(\d+\) \s* $", RegexOptions.Multiline | RegexOptions.IgnorePatternWhitespace )]
        private static partial Regex GetGroupRegex( );

        [GeneratedRegex( @"^\u001FG,(-1|\d+),(\d+)$" )]
        private static partial Regex ParseGroupResultRegex( );
    }
}
