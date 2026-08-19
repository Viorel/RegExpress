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


namespace JavaPlugin
{
    static partial class MatcherJoni
    {

        public class Rootobject
        {
            public required Match[] matches { get; set; }
        }

        public class Match
        {
            public required int[][] g { get; set; }
            public required Ng[] ng { get; set; }
        }

        public class Ng
        {
            public int s { get; set; }
            public int e { get; set; }
            public required string n { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.Package == PackageEnum.joni );

            (string? javaExePath, string? workerDir) = MatcherRegex.GetPaths( );

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( string.IsNullOrWhiteSpace( javaExePath ) ) throw new Exception( "Cannot initialize JRE" );
            if( string.IsNullOrWhiteSpace( workerDir ) ) throw new Exception( "Cannot initialize Java worker" );

            using ProcessHelper ph = new( javaExePath );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.Arguments =
            [
                "-cp",
                $"{workerDir};{Path.Combine( workerDir, $"joni-{Versions.Joni}.jar" )};{Path.Combine( workerDir, "json-simple-1.1.1.jar" )};{Path.Combine( workerDir, "jcodings-1.0.64.jar" )}",
                "JoniWorker"
            ];

            var obj = new
            {
                pattern = pattern,
                text = text,
                options = new
                {
                    IGNORECASE = options.CASE_INSENSITIVE,
                    EXTEND = options.COMMENTS,
                    MULTILINE = options.MULTILINE,
                    SINGLELINE = options.DOTALL,
                    FIND_LONGEST = options.LONGEST_MATCH,
                    FIND_NOT_EMPTY = options.FIND_NOT_EMPTY,
                    NEGATE_SINGLELINE = options.NEGATE_SINGLELINE,
                    DONT_CAPTURE_GROUP = options.DONT_CAPTURE_GROUP,
                    CAPTURE_GROUP = options.CAPTURE_GROUP,
                    NOTBOL = options.NOTBOL,
                    NOTEOL = options.NOTEOL,
                    /* Does not seem to be implemented
                    NEWLINE_CRLF = options.NEWLINE_CRLF,
                    NOTBOS = options.NOTBOS,
                    NOTEOS = options.NOTEOS,
                    */
                }
            };

            ph.StreamWriter = sw =>
            {
                var json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );
                sw.WriteLine( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

#if DEBUG
            using StreamReader sr = new( ph.OutputStream );
            string output = sr.ReadToEnd( );
            Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( output );
#else
            Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

            if( root_object == null ) throw new Exception( "Invalid response." );

            List<SimpleMatch> matches = [];
            SimpleTextGetter stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( Match m in root_object.matches )
            {
                SimpleMatch match;

                {
                    int native_start = m.g[0][0];
                    int native_end = m.g[0][1];
                    int native_length = native_end - native_start;

                    Debug.Assert( native_start >= 0 && native_end >= native_start );

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );

                    match.AddDefaultGroup( );
                }

                for( int i = 1; i < m.g.Length; ++i ) // skip default group
                {
                    int[] g = m.g[i];

                    int native_start = g[0];
                    int native_end = g[1];

                    bool success = native_start >= 0 && native_end >= 0;

                    string name = i.ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        int native_length = native_end - native_start;

                        Debug.Assert( native_start >= 0 && native_end >= native_start );

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                    }
                }

                foreach( var ng in m.ng )
                {
                    int native_start = ng.s;
                    int native_end = ng.e;
                    string name = ng.n;

                    bool success = native_start >= 0 && native_end >= 0;

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        int native_length = native_end - native_start;

                        Debug.Assert( native_start >= 0 && native_end >= native_start );

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                    }
                }

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }
    }
}
