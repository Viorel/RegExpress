using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
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


namespace PythonPlugin
{
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            const string script = @"
import sys
import re
import json
import regex

input_json = sys.stdin.read()

#print( input_json, file = sys.stderr )

input_obj = json.loads(input_json)

package = input_obj['package']
pattern = input_obj['pattern']
text = input_obj['text']
flags_obj = input_obj['flags']

flags = 0
if flags_obj['ASCII']       : flags |= re.ASCII
if flags_obj['DOTALL']      : flags |= re.DOTALL
if flags_obj['IGNORECASE']  : flags |= re.IGNORECASE
if flags_obj['LOCALE']      : flags |= re.LOCALE
if flags_obj['MULTILINE']   : flags |= re.MULTILINE
if flags_obj['VERBOSE']     : flags |= re.VERBOSE

if package == 'regex':
    if flags_obj['BESTMATCH']       : flags |= regex.BESTMATCH
    if flags_obj['ENHANCEMATCH']    : flags |= regex.ENHANCEMATCH
    if flags_obj['FULLCASE']        : flags |= regex.FULLCASE
    if flags_obj['POSIX']           : flags |= regex.POSIX
    if flags_obj['REVERSE']         : flags |= regex.REVERSE
    if flags_obj['UNICODE']         : flags |= regex.UNICODE
    if flags_obj['WORD']            : flags |= regex.WORD
    if flags_obj['VERSION0']        : flags |= regex.VERSION0
    if flags_obj['VERSION1']        : flags |= regex.VERSION1 

try:
    regex_obj = None

    if package == 'regex':
        regex_obj = regex.compile( pattern, flags)
    else:
        regex_obj = re.compile( pattern, flags)

    #print( f'# {regex_obj.groups}')
    #print( f'# {regex_obj.groupindex}')

    for key, value in regex_obj.groupindex.items():
        print( f'N {value} <{key}>')

    matches = None

    if package == 'regex':
        matches = regex_obj.finditer( text, overlapped = flags_obj['overlapped'], partial = flags_obj['partial'] )
    else:
        matches = regex_obj.finditer( text )

    for match in matches :
        print( f'M {match.start()}, {match.end()}')
        for g in range(0, regex_obj.groups + 1):
            print( f'G {match.start(g)}, {match.end(g)}' )

except:
    ex_type, ex, tb = sys.exc_info()

    print( ex, file = sys.stderr )
";

            string? stdout_contents;
            string? stderr_contents;

            Action<StreamWriter> stdin_writer = new Action<StreamWriter>( sw =>
            {
                var obj = new
                {
                    package = Enum.GetName( options.Module ),
                    pattern = pattern,
                    text,
                    flags = new
                    {
                        options.ASCII,
                        options.DOTALL,
                        options.IGNORECASE,
                        options.LOCALE,
                        options.MULTILINE,
                        options.VERBOSE,
                        //
                        options.BESTMATCH,
                        options.ENHANCEMATCH,
                        options.FULLCASE,
                        options.POSIX,
                        options.REVERSE,
                        options.UNICODE,
                        options.WORD,
                        options.VERSION0,
                        options.VERSION1,
                        //
                        options.overlapped,
                        options.partial,
                    }
                };
                var json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );
                sw.WriteLine( json );
            } );


            if( !ProcessUtilities.InvokeExe( cnc, GetPythonExePath( ), new[] { "-I", "-E", "-s", "-S", "-X", "utf8", "-c", script }, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );


            var matches = new List<IMatch>( );
            ISimpleTextGetter? stg = null;

            SimpleMatch? match = null;
            int group_i = 0;
            var names = new Dictionary<int, string>( );
            var sph = new SurrogatePairsHelper( text, processSurrogatePairs: true );

            using( var sr = new StringReader( stdout_contents ) )
            {
                string? line;

                while( ( line = sr.ReadLine( ) ) != null )
                {
                    if( line.Length == 0 || line.StartsWith( "#" ) ) continue;

                    var m = NMGRegex( ).Match( line );

                    if( !m.Success )
                    {
                        if( Debugger.IsAttached ) Debugger.Break( );

                        throw new Exception( "Internal error in Python engine." );
                    }
                    else
                    {
                        switch( m.Groups["t"].Value )
                        {
                        case "N":
                        {
                            int index = int.Parse( m.Groups["i"].Value, CultureInfo.InvariantCulture );
                            string name = m.Groups["n"].Value;

                            Debug.Assert( !names.ContainsKey( index ) );

                            names[index] = name;
                        }
                        break;
                        case "M":
                        {
                            int index = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                            int end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                            int length = end - index;

                            Debug.Assert( index >= 0 && end >= 0 );

                            var (text_index, text_length) = sph.ToTextIndexAndLength( index, length );

                            stg ??= new SimpleTextGetter( text );

                            match = SimpleMatch.Create( index, length, text_index, text_length, stg );
                            matches.Add( match );

                            group_i = 0;
                        }
                        break;
                        case "G":
                        {
                            int index = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                            int end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                            int length = end - index;
                            bool success = index >= 0;

                            Debug.Assert( match != null );

                            var (text_index, text_length) = sph.ToTextIndexAndLength( index, length );

                            string? name;
                            if( !names.TryGetValue( group_i, out name ) ) name = group_i.ToString( CultureInfo.InvariantCulture );

                            match.AddGroup( index, length, text_index, text_length, success, name );

                            ++group_i;
                        }
                        break;
                        default:
                            if( Debugger.IsAttached ) Debugger.Break( );

                            throw new Exception( "Internal error in Python engine." );
                        }
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            string? stdout_contents;
            string? stderr_contents;

            Action<StreamWriter> stdin_writer = sw =>
            {
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetPythonExePath( ), new[] { "-V" }, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }

            if( stdout_contents == null )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }

            stdout_contents = stdout_contents.Trim( );

            string v = GetVersionRegex( ).Match( stdout_contents ).Groups[1].Value;

            if( string.IsNullOrWhiteSpace( v ) )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }

            return v;
        }


        static string GetPythonExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string python_exe = Path.Combine( assembly_dir, @"python-embed-amd64", @"python.exe" );

            return python_exe;
        }


        [GeneratedRegex( @"^Python (\d+(\.\d+)*)" )]
        private static partial Regex GetVersionRegex( );

        [GeneratedRegex( @"^(?'t'[MG]) (?'s'-?\d+), (?'e'-?\d+)|(?'t'N) (?'i'\d+) <(?'n'.*)>$", RegexOptions.ExplicitCapture )]
        private static partial Regex NMGRegex( );
    }
}
