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


namespace CppBuilderPlugin
{
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;

            ph.StreamWriter = sw =>
            {
                string flags = "";
                if( options.roIgnoreCase ) flags += 'i';
                if( options.roMultiLine ) flags += 'm';
                if( options.roExplicitCapture ) flags += 'n';
                if( options.roCompiled ) flags += 'C';
                if( options.roSingleLine ) flags += 's';
                if( options.roIgnorePatternSpace ) flags += 'x';
                if( options.roNotEmpty ) flags += 'N';

                var obj = new { pattern = pattern, text = text, flags = flags };

                sw.WriteLine( JsonSerializer.Serialize( obj ) );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );
            SimpleMatch? current_match = null;
            string? line;

            while( ( line = ph.StreamReader.ReadLine( ) ) != null )
            {
                line = line.Trim( );

                if( line.Length == 0 ) continue;
                if( line.StartsWith( "d " ) ) continue; // (for debugging)

                {
                    Match m = ParseMatchRegex( ).Match( line );
                    if( m.Success )
                    {
                        int index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture ); // (starting at 1)
                        Debug.Assert( index > 0 );

                        if( index > 0 )
                        {
                            --index; // make it starting at 0
                            int length = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture );

                            current_match = SimpleMatch.Create( index, length, stg );
                            current_match.AddGroup( current_match.Index, current_match.Length, true, "" ); // default group

                            matches.Add( current_match );
                        }

                        continue;
                    }
                    else
                    {
                        Match g = ParseGroupRegex( ).Match( line );
                        if( g.Success )
                        {
                            if( current_match == null ) throw new Exception( "Invalid response." );

                            int index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture ); // (starting at 1)
                            int length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                            bool success = index > 0;

                            string? name = null;
                            Group gn = g.Groups["n"];
                            if( gn.Success )
                            {
                                string name_js = gn.Value;

                                try
                                {
                                    name = JsonSerializer.Deserialize<string>( name_js );
                                }
                                catch
                                {
                                    name = null;
                                    // ignore?
                                }
                            }

                            name ??= current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                            current_match.AddGroup( success ? (int)index - 1 : 0, success ? (int)length : 0, success, name );

                            continue;
                        }
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            return "29.0"; // TODO: determine the RTL version programmatically
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"CppBuilderWorker.bin" );

            return worker_exe;
        }


        [GeneratedRegex( @"(?ix)^\s* M \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );

        [GeneratedRegex( @"(?ix)^\s* g \s+ (-?\d+) \s+ (-?\d+) (\s+(?<n>"".*?""))?" )]
        private static partial Regex ParseGroupRegex( );
    }
}
