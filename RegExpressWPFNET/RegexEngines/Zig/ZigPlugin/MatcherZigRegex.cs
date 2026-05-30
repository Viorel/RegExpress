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


namespace ZigPlugin
{
    /*
     * 
     * 
Example of input:

    { "pattern": "(?<first>\\d)(\\d*)(?<last>QQQ)?", "text": "a1b23c456", "flags": "" }

Example of result:

{
  "names": [
    "first",
    null,
    "last"
  ],
  "matches": [
    {
      "start": 1,
      "length": 1,
      "groups": [
        {
          "value": "1"
        },
        {
          "value": ""
        },
        {
          "value": null
        }
      ]
    },
    {
      "start": 3,
      "length": 2,
      "groups": [
        {
          "value": "2"
        },
        {
          "value": "3"
        },
        {
          "value": null
        }
      ]
    }
  ]
}

    */


    namespace ResponseClasses
    {
        public class RootObject
        {
            public string[]? names { get; set; }
            public Match[]? matches { get; set; }
        }

        public class Match
        {
            public int start { get; set; }
            public int length { get; set; }
            public Group[]? groups { get; set; }
        }

        public class Group
        {
            public string? value { get; set; }
        }
    }


    static class MatcherZigRegex
    {
        static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            var json_object = new
            {
                pattern = pattern,
                text = text,
                flags =
                    ( options.case_insensitive ? "i" : "" ) +
                    ( options.multiline ? "m" : "" ) +
                    ( options.dot_all ? "s" : "" ) +
                    ( options.extended ? "x" : "" ) +
                    ( options.unicode ? "U" : "" ),
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

            ResponseClasses.RootObject? response = JsonSerializer.Deserialize<ResponseClasses.RootObject>( ph.OutputStream );

            if( response == null ) throw new Exception( "Null response" );

            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            List<IMatch> matches = [];
            SimpleTextGetter? stg = new( text );

            foreach( var m in response.matches! )
            {
                SimpleMatch? match = null;

                int byte_start = m.start;
                int byte_end = m.start + m.length;

                int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end );
                int char_length = char_end - char_start;

                Debug.Assert( match == null );

                match = SimpleMatch.Create( char_start, char_length, stg );
                match.AddGroup( char_start, char_length, true, "0" ); // default group

                for( int group_index = 0; group_index < m.groups!.Length; group_index++ )
                {
                    string? value = m.groups[group_index].value;
                    bool success = value != null;

                    string? name = response.names?[group_index];
                    name ??= ( group_index + 1 ).ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        match.AddGroup( 0, 0, false, name );
                    }
                    else
                    {
                        match.AddGroup( char_start, value!.Length, true, name, new SimpleTextGetterWithOffset( char_start, value ) );
                    }
                }

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            return "0.2.0"; // TODO: get from worker
        }

        internal static void StartGetVersion( Action<string?> setVersion )
        {
            if( LazyVersion.IsValueCreated )
            {
                setVersion( LazyVersion.Value );

                return;
            }

            Thread t = new( ( ) =>
            {
                setVersion( LazyVersion.Value );
            } )
            {
                IsBackground = true
            };

            t.Start( );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"ZigRegexWorker.bin" );

            return worker_exe;
        }

    }
}
