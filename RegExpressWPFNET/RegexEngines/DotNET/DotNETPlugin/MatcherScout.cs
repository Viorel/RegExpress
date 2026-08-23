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

internal partial class MatcherScout
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
        Debug.Assert( options.Class == ClassEnum.Scout );

        var data = new
        {
            pattern,
            text,
            options = new
            {
                AsciiCaseInsensitive = options.IgnoreCase,
                MultiLine = options.Multiline,
                DotMatchesNewline = options.Singleline,
                Crlf = options.Crlf,
                //LineTerminator = 
                Utf8 = options.Utf8,
                UnicodeClasses = options.UnicodeClasses,
                //MatchInvalidUtf8 = 

                EngineMode = Enum.GetName( options.ByteRegexEngineMode ),
                DfaSizeLimit = ValidationUtilities.ParseInt32( "DfaSizeLimit", options.DfaSizeLimit ),
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

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( AdjustErrorMessage( ph.Error, pattern ) );

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
        Utf8IndexConverter index_converter = new( text );

        foreach( var m in root_object.matches )
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

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }

    private static string? AdjustErrorMessage( string error, string pattern )
    {
        // try to show character offset based on byte offset, which appears in error messages

        System.Text.RegularExpressions.Match m = RegexExtractByteOffset( ).Match( error );

        if( m.Success && int.TryParse( m.Groups[1].Value, out int byte_offset ) )
        {
            try
            {
                byte[] utf8_bytes = Encoding.UTF8.GetBytes( pattern );
                int char_offset = Encoding.UTF8.GetCharCount( utf8_bytes, 0, byte_offset );

                string new_message = $"{error.TrimEnd( )}{Environment.NewLine}at character index {char_offset}";

                return new_message;
            }
            catch
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                // ignore
            }
        }

        return error;
    }

    static string GetWorkerExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_exe = Path.Combine( assembly_dir, "ScoutWorker", @"ScoutWorker.bin" );

        return worker_exe;
    }


    [GeneratedRegex( @" at byte offset (\d+)" )]
    private static partial Regex RegexExtractByteOffset( );
}
