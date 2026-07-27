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


namespace JavaPlugin
{
    static partial class MatcherRE2J
    {

        public class Rootobject
        {
            public required Match[] matches { get; set; }
        }

        public class Match
        {
            public int s { get; set; }
            public int e { get; set; }
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
            Debug.Assert( options.Package == PackageEnum.re2j );

            (string? javaExePath, string? workerDir) = MatcherRegex.GetPaths( );

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( string.IsNullOrWhiteSpace( javaExePath ) ) throw new Exception( "Cannot initialize JRE" );
            if( string.IsNullOrWhiteSpace( workerDir ) ) throw new Exception( "Cannot initialize Java worker" );

            using ProcessHelper ph = new( javaExePath );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.Arguments = ["-cp", $"{workerDir};{Path.Combine( workerDir, "re2j-1.8.jar" )};{Path.Combine( workerDir, "json-simple-1.1.1.jar" )}", "RE2JWorker"];

            var obj = new
            {
                command = "get-matches",
                pattern = pattern,
                text = text,
                options = new
                {
                    options.CASE_INSENSITIVE,
                    options.DOTALL,
                    options.MULTILINE,
                    options.DISABLE_UNICODE_GROUPS,
                    options.LONGEST_MATCH,
                },
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

            foreach( Match m in root_object.matches )
            {
                SimpleMatch match;

                {
                    int native_start = m.s;
                    int native_end = m.e;
                    int native_length = native_end - native_start;

                    Debug.Assert( native_start >= 0 && native_end >= native_start );

                    match = SimpleMatch.Create( native_start, native_length, stg );

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

                        match.AddSucceededGroup( native_start, native_length, name );
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
                        try
                        {
                            SimpleGroup? sg = (SimpleGroup?)match.Groups.Skip( 1 ).SingleOrDefault( g => !g.Success );

                            if( sg == null )
                            {
                                Debug.Fail( "Orphan named group" );

                                match.AddFailedGroup( name );
                            }
                            else
                            {
                                sg.SetName( name );
                            }
                        }
                        catch( InvalidOperationException )
                        {
                            // more than one

                            match.AddFailedGroup( name );
                        }
                    }
                    else
                    {
                        int native_length = native_end - native_start;

                        try
                        {
                            SimpleGroup? sg = (SimpleGroup?)match.Groups.Skip( 1 ).SingleOrDefault( g => g.Success && g.NativeIndex == native_start && g.NativeLength == native_length );

                            if( sg == null )
                            {
                                Debug.Fail( "Orphan named group" );

                                match.AddSucceededGroup( native_start, native_length, name );
                            }
                            else
                            {
                                sg.SetName( name );
                            }
                        }
                        catch( InvalidOperationException )
                        {
                            // more than one

                            match.AddSucceededGroup( native_start, native_length, name );
                        }
                    }
                }

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }
    }
}
