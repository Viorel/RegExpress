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
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;


namespace GoPlugin
{
    class MatcherCoregex
    {
        public class RootObject
        {
            public string[]? Names { get; set; }
            public int[][]? Matches { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            var data = new
            {
                pattern,
                text,

                options.EnableDFA,
                options.EnablePrefilter,
                options.EnableASCIIOptimization,
                MaxDFAStates = ValidationUtilities.ParseUInt32( "MaxDFAStates", options.MaxDFAStates ),
                DeterminizationLimit = ValidationUtilities.ParseInt32( "DeterminizationLimit", options.DeterminizationLimit ),
                MinLiteralLen = ValidationUtilities.ParseInt32( "MinLiteralLen", options.MinLiteralLen ),
                MaxLiterals = ValidationUtilities.ParseInt32( "MaxLiterals", options.MaxLiterals ),
                MaxRecursionDepth = ValidationUtilities.ParseInt32( "MaxRecursionDepth", options.MaxRecursionDepth ),

                options.posix,
                options.longest,
                options.literal,
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
                Utf8IndexConverter index_converter = new( text );

                foreach( int[] m in root_object.Matches )
                {
                    if( m.Length < 2 || ( m.Length % 2 ) != 0 ) throw new Exception( $"Invalid length: {m.Length}." );

                    SimpleMatch match;

                    {
                        // main group

                        int native_start = m[0];
                        int native_end = m[1];
                        int native_length = native_end - native_start;

                        Debug.Assert( native_start >= 0 );
                        Debug.Assert( native_end >= native_start );

                        (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                        match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );

                        match.AddDefaultGroup( );
                    }

                    {
                        // other groups

                        for( int i = 2; i < m.Length; i += 2 )
                        {
                            int group_index = i / 2;

                            string? name = root_object.Names?[group_index];
                            if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                            bool success = m[i] >= 0;

                            if( !success )
                            {
                                match.AddFailedGroup( name );
                            }
                            else
                            {
                                int native_start = m[i];
                                int native_end = m[i + 1];
                                int native_length = native_end - native_start;

                                Debug.Assert( native_start >= 0 );
                                Debug.Assert( native_end >= native_start );

                                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                                match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                            }
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
            string worker_exe = Path.Combine( assembly_dir, @"CoregexWorker.bin" );

            return worker_exe;
        }
    }
}
