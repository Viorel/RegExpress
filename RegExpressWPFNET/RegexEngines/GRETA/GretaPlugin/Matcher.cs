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


namespace GretaPlugin
{
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.WriteLine( JsonSerializer.Serialize( pattern ) );
                sw.WriteLine( JsonSerializer.Serialize( text ) );

                string flags = "";

                if( options.NOCASE ) flags += "i";
                if( options.MULTILINE ) flags += "m";
                if( options.SINGLELINE ) flags += "s";
                if( options.EXTENDED ) flags += "x";
                if( options.RIGHTMOST ) flags += "R";
                if( options.NORMALIZE ) flags += "N";

                switch( options.Mode )
                {
                case ModeEnum.MODE_FAST: flags += "F"; break;
                case ModeEnum.MODE_SAFE: flags += "S"; break;
                case ModeEnum.MODE_MIXED: flags += "M"; break;
                }

                sw.WriteLine( JsonSerializer.Serialize( flags ) );
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
                        int index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                        Debug.Assert( index >= 0 );

                        if( index >= 0 )
                        {
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

                            int index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture );
                            int length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                            bool success = index >= 0;

                            current_match.AddGroup( success ? (int)index : 0, success ? (int)length : 0, success, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );

                            continue;
                        }
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            return "2.6.4";
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"GretaWorker.bin" );

            return worker_exe;
        }

        [GeneratedRegex( @"(?x)^\s* M \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );

        [GeneratedRegex( @"(?x)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
        private static partial Regex ParseGroupRegex( );
    }
}
