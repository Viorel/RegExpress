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
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;


namespace ZigPlugin
{
    /*
     * 
     * 
Example of input:

    { "pattern": "(?<first>\\d)(\\d*)(?<last>QQQ)?", "text": "a1b23c456", "flags": "" }

Example of result:

{
  "matches": [
    {
      "start": 1,
      "length": 1
    },
    {
      "start": 3,
      "length": 2
    }
  ]
}

    */


    static class MatcherMvzr
    {
        internal static RegexEngineCapabilityEnum GetCapabilities( Options options )
        {
            return RegexEngineCapabilityEnum.NoGroups;
        }

        public class RootObject
        {
            public Match[]? matches { get; set; }
        }

        public class Match
        {
            public int start { get; set; }
            public int length { get; set; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.Library == RegexLibraryEnum.Mvzr );

            var json_object = new
            {
                pattern = pattern,
                text = text,
            };

            string json = JsonSerializer.Serialize( json_object );

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            RootObject? response = JsonSerializer.Deserialize<RootObject>( ph.OutputStream );

            if( response == null ) throw new Exception( "Null response" );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );
            Utf8IndexConverter index_converter = new( text );

            foreach( var m in response.matches! )
            {
                SimpleMatch? match = null;

                int native_start = m.start;
                int native_length = m.length;
                int native_end = m.start + native_length;

                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                Debug.Assert( match == null );

                match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                match.AddDefaultGroup( );

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"ZigMvzrWorker.bin" );

            return worker_exe;
        }
    }
}
