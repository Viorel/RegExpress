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


namespace QtPlugin
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
                if( options.CaseInsensitiveOption ) flags += 'i';
                if( options.DotMatchesEverythingOption ) flags += 's';
                if( options.MultilineOption ) flags += 'm';
                if( options.ExtendedPatternSyntaxOption ) flags += 'x';
                if( options.InvertedGreedinessOption ) flags += 'G';
                if( options.DontCaptureOption ) flags += 'n';
                if( options.UseUnicodePropertiesOption ) flags += 'u';

                var obj = new { pattern = pattern, text = text, flags = flags };

                sw.WriteLine( JsonSerializer.Serialize( obj ) );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            List<string?> names = [];
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
                    if( line.StartsWith( "n " ) )
                    {
                        string j = line.Substring( 2 );
                        string? name = JsonSerializer.Deserialize<string>( j );
                        if( string.IsNullOrWhiteSpace( name ) ) name = null;

                        names.Add( name );

                        continue;
                    }
                }
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
                }
                {
                    Match g = ParseGroupRegex( ).Match( line );
                    if( g.Success )
                    {
                        if( current_match == null ) throw new Exception( "Invalid response." );

                        int index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture );
                        int length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                        bool success = index >= 0;

                        string? name = null;
                        int group_number = current_match.Groups.Count( ) - 1;
                        Debug.Assert( group_number >= 0 );

                        if( group_number >= 0 && group_number < names.Count ) name = names[group_number];

                        name ??= current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                        current_match.AddGroup( success ? index : 0, success ? length : 0, success, name );

                        continue;
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            return "6.9.3"; // TODO: determine the version programmatically
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"QtWorker.bin" );

            return worker_exe;
        }


        [GeneratedRegex( @"(?ix)^\s* M \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );

        [GeneratedRegex( @"(?ix)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
        private static partial Regex ParseGroupRegex( );
    }
}
