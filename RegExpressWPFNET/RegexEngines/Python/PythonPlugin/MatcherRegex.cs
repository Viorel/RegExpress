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
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;


namespace PythonPlugin
{
    static partial class MatcherRegex
    {
        static Lazy<string> LazyPythonWorker = new( LoadPythonWorker );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.Module == ModuleEnum.regex );

            string script = LazyPythonWorker.Value;

            double? timeout = ValidationUtilities.ParseDouble( "timeout", options.timeout );

            using ProcessHelper ph = new( GetPythonExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;
            ph.Arguments = [
                "-I",           // isolate Python from the user's environment (implies -E, -P and -s)
                "-E",           // ignore PYTHON* environment variables (such as PYTHONPATH)
                "-P",           // don't prepend a potentially unsafe path to sys.path; also PYTHONSAFEPATH
                "-s",           // don't add user site directory to sys.path; also PYTHONNOUSERSITE=x
                "-S",           // don't imply 'import site' on initialization
                "-X", "utf8",   // set implementation-specific option
                "-c", script    // program passed in as string (terminates option list)
                ];

            ph.StreamWriter = sw =>
            {
                var obj = new
                {
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
                    },
                    timeout = timeout
                };
                var json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );
                sw.WriteLine( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );
            CodepointIndexConverter index_converter = new( text );
            SimpleMatch? match = null;
            Dictionary<int, string> names = [];
            string? line;

            while( ( line = ph.StreamReader.ReadLine( ) ) != null )
            {
                if( line.Length == 0 || line.StartsWith( "#" ) ) continue;

                var m = NMgcRegex( ).Match( line );

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
                        int native_start = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                        int native_end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                        int native_length = native_end - native_start;

                        Debug.Assert( native_start >= 0 && native_end >= native_start );

                        (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                        match = SimpleMatch.Create( native_start, native_length, char_index, char_length, stg );
                        matches.Add( match );
                    }
                    break;
                    case "g":
                    {
                        int native_start = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                        int native_end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                        int native_length = native_end - native_start;
                        bool success = native_start >= 0;

                        Debug.Assert( match != null );

                        int group_i = match.Groups.Count( );

                        string? name;
                        if( !names.TryGetValue( group_i, out name ) ) name = group_i.ToString( CultureInfo.InvariantCulture );

                        if( !success )
                        {
                            match.AddFailedGroup( name );
                        }
                        else
                        {
                            (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                            match.AddSucceededGroup( native_start, native_length, char_index, char_length, name );
                        }

                        ++group_i;
                    }
                    break;
                    case "c":
                    {
                        int native_start = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                        int native_end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                        int native_length = native_end - native_start;

                        Debug.Assert( match != null );

                        (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                        SimpleGroup group = (SimpleGroup)match.Groups.Last( );

                        group.AddCapture( native_start, native_length, char_index, char_length );
                    }
                    break;
                    default:
                        if( Debugger.IsAttached ) Debugger.Break( );

                        throw new Exception( "Internal error in Python engine." );
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetPythonExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string python_exe = Path.Combine( assembly_dir, @"python-embed-amd64", @"python.exe" );

            return python_exe;
        }

        static string GetPythonWorkerPath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_path = Path.Combine( assembly_dir, @"PythonWorkerRegex.py" );

            return worker_path;
        }

        static string LoadPythonWorker( )
        {
            string worker_path = GetPythonWorkerPath( );
            string worker = File.ReadAllText( worker_path );

            return worker;
        }


        [GeneratedRegex( @"^(?'t'[Mgc]) (?'s'-?\d+), (?'e'-?\d+)|(?'t'N) (?'i'\d+) <(?'n'.*)>$", RegexOptions.ExplicitCapture )]
        private static partial Regex NMgcRegex( );
    }
}
