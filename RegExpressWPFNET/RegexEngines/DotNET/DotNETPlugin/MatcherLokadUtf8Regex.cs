using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace DotNETPlugin;

internal class MatcherLokadUtf8Regex
{
    public class Rootobject
    {
        public required Match[] matches { get; set; }
    }

    public class Match
    {
        public required int[][] g { get; set; }
    }


    public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
    {
        Debug.Assert( options.Class == ClassEnum.LokadUtf8Regex );

        var data = new
        {
            pattern,
            text,
            options = new
            {
                Compiled = options.Compiled,
                CultureInvariant = options.CultureInvariant,
                ECMAScript = options.ECMAScript,
                ExplicitCapture = options.ExplicitCapture,
                IgnoreCase = options.IgnoreCase,
                IgnorePatternWhitespace = options.IgnorePatternWhitespace,
                Multiline = options.Multiline,
                NonBacktracking = options.NonBacktracking,
                RightToLeft = options.RightToLeft,
                Singleline = options.Singleline,

                Timeout = options.TimeoutMs,
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
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( output );
#else
        Rootobject? root_object = JsonSerializer.Deserialize<Rootobject>( ph.OutputStream );
#endif

        if( root_object == null ) throw new Exception( "Invalid response" );

        List<SimpleMatch> matches = [];
        SimpleTextGetter stg = new( text );
        IdentityIndexConverter index_converter = new( );

        foreach( var m in root_object.matches )
        {
            SimpleMatch match;

            {
                int native_start = m.g[0][0];
                int native_length = m.g[0][1];

                Debug.Assert( native_start >= 0 && native_length >= 0 );

                int native_end = native_start + native_length;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );

                match.AddDefaultGroup( );
            }

            // Groups are not available.

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, "LokadUtf8RegexWorker", @"LokadUtf8RegexWorker.bin" );

        return worker_exe;
    }
}
