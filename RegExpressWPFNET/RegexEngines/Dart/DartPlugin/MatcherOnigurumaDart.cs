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


namespace DartPlugin
{
    class MatcherOnigurumaDart
    {
        public class RootObject
        {
            public Match[]? Matches { get; set; }
        }

        public class Match
        {
            public int s { get; set; }
            public int e { get; set; }
            public G[]? g { get; set; }
            public NV[]? nv { get; set; }
        }

        public class G
        {
            public int s { get; set; }
            public int e { get; set; }
        }

        public class NV
        {
            public string n { get; set; }
            public string v { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            var data = new
            {
                syntax = Enum.GetName( options.OnigurumaSyntax ),
                pattern,
                text,
                options = new
                {
                    options.ignoreCase,
                    options.extend,
                    options.multiLine,
                    options.singleLine,
                    options.findLongest,
                    options.findNotEmpty,
                    options.negateSingleLine,
                    options.dontCaptureGroup,
                    options.captureGroup,
                    options.notBol,
                    options.notEol,
                    options.posixRegion,
                    options.checkValidityOfString,
                    options.ignoreCaseIsAscii,
                    options.wordIsAscii,
                    options.digitIsAscii,
                    options.spaceIsAscii,
                    options.posixIsAscii,
                    options.textSegmentExtendedGraphemeCluster,
                    options.textSegmentWord,
                    options.notBeginString,
                    options.notEndString,
                    options.notBeginPosition,
                    options.callbackEachMatch,
                    options.matchWholeString,
                }
            };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

#if DEBUG
            using StreamReader sr = new( ph.OutputStream );
            string output = sr.ReadToEnd( );
            RootObject? root_object = JsonSerializer.Deserialize<RootObject>( output );
#else
            RootObject? root_object = JsonSerializer.Deserialize<RootObject>( ph.OutputStream );
#endif

            if( root_object == null ) throw new Exception( "Invalid response." );

            List<IMatch> matches = [];

            if( root_object.Matches != null )
            {
                SimpleTextGetter stg = new( text );

                foreach( Match m in root_object.Matches )
                {
                    SimpleMatch match;

                    {
                        int native_start = m.s;
                        int native_end = m.e;
                        Debug.Assert( native_end >= native_start );
                        int native_length = native_end - native_start;

                        match = SimpleMatch.Create( native_start, native_length, stg );

                        match.AddDefaultGroup( );
                    }

                    {
                        // groups

                        for( int i = 0; i < m.g!.Length; ++i )
                        {
                            int group_index = i + 1;
                            //..................
                            string? name = null; // try to determine the name?
                            if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                            var g = m.g[i];
                            bool success = g.s >= 0 && g.e >= 0;

                            if( !success )
                            {
                                match.AddFailedGroup( name );
                            }
                            else
                            {
                                int native_start = g.s;
                                int native_end = g.e;
                                Debug.Assert( native_end >= native_start );
                                int native_length = native_end - native_start;

                                match.AddSucceededGroup( native_start, native_length, name );
                            }
                        }

                        // named values

                        foreach( var nv in m.nv! )
                        {
                            match.AddSucceededNoDetailsGroup( nv.n, nv.v );
                        }
                    }

                    matches.Add( match );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, "onigurumadartworker.bin" );

            return worker_exe;
        }
    }
}
