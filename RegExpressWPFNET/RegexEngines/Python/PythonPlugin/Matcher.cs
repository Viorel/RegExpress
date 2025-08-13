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
        static Lazy<string> LazyPythonWorker = new( LoadPythonWorker );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string script = LazyPythonWorker.Value;

            double? timeout = null;

            if( options.Module == ModuleEnum.regex )
            {
                if( !string.IsNullOrWhiteSpace( options.timeout ) )
                {
                    if( !double.TryParse( options.timeout, out var timeout0 ) )
                    {
                        throw new ApplicationException( "Invalid timeout. Enter a floating-point value." );
                    }
                    else
                    {
                        timeout = timeout0;
                    }
                }
            }

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
                    module = Enum.GetName( options.Module ),
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

            List<IMatch> matches = new( );
            SimpleTextGetter? stg = null;
            SimpleMatch? match = null;
            int group_i = 0;
            Dictionary<int, string> names = new( );
            SurrogatePairsHelper sph = new( text, processSurrogatePairs: true );
            string? line;

            while( ( line = ph.StreamReader.ReadLine( ) ) != null )
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

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new ProcessHelper( GetPythonExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;
            ph.Arguments = new[] { "-V" };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string? response_s = ph.StreamReader.ReadToEnd( )?.Trim( ) ?? "";

            string version = GetVersionRegex( ).Match( response_s ).Groups[1].Value;

            return version;
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
            string worker_path = Path.Combine( assembly_dir, @"PythonWorker.py" );

            return worker_path;
        }

        static string LoadPythonWorker( )
        {
            string worker_path = GetPythonWorkerPath( );
            string worker = File.ReadAllText( worker_path );

            return worker;
        }


        [GeneratedRegex( @"^Python (\d+(\.\d+)*)" )]
        private static partial Regex GetVersionRegex( );

        [GeneratedRegex( @"^(?'t'[MG]) (?'s'-?\d+), (?'e'-?\d+)|(?'t'N) (?'i'\d+) <(?'n'.*)>$", RegexOptions.ExplicitCapture )]
        private static partial Regex NMGRegex( );
    }
}
