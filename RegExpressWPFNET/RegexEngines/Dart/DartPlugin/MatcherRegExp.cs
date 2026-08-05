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
    class MatcherRegExp
    {
        public class RootObject
        {
            public Match[]? Matches { get; set; }
        }

        public class Match
        {
            public int s { get; set; }
            public int e { get; set; }
            public string?[]? g { get; set; }
            public Ng[]? ng { get; set; }
        }

        public class Ng
        {
            public string? n { get; set; }
            public string? v { get; set; }
        }

        public class RelaxedJsonConverter : System.Text.Json.Serialization.JsonConverter<string>
        {
            public override string Read( ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options )
            {
                try
                {
                    return reader.GetString( )!;
                }
                catch( InvalidOperationException )
                {
                    return System.Text.Encoding.UTF8.GetString( reader.ValueSpan );

                    // incomplete surrogate pairs are returned as lowercase hexadecimal codes preceded by "\\u"
                }
            }

            public override void Write( Utf8JsonWriter writer, string value, JsonSerializerOptions options )
            {
                writer.WriteStringValue( value );
            }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Debug.Assert( options.package == PackageEnum.RegExp );

            var data = new
            {
                pattern,
                text,
                options = new
                {
                    options.multiLine,
                    options.caseSensitive,
                    options.unicode,
                    options.dotAll,
                }
            };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            JsonSerializerOptions json_options = new( )
            {
                Converters = { new RelaxedJsonConverter( ) },
            };

#if DEBUG
            using StreamReader sr = new( ph.OutputStream );
            string output = sr.ReadToEnd( );
            RootObject? root_object = JsonSerializer.Deserialize<RootObject>( output, json_options );
#else
            RootObject? root_object = JsonSerializer.Deserialize<RootObject>( ph.OutputStream, json_options );
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
                        // unnamed groups

                        for( int i = 0; i < m.g!.Length; ++i )
                        {
                            int group_index = i + 1;
                            string? name = null; // try to determine the name?
                            if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                            var g = m.g[i];
                            bool success = g != null;

                            if( !success )
                            {
                                match.AddFailedGroup( name );
                            }
                            else
                            {
                                // no details

                                match.AddSucceededNoDetailsGroup( name, g! );
                            }
                        }
                    }

                    {
                        // named groups (also presented in unnamed groups)

                        for( int i = 0; i < m.ng!.Length; ++i )
                        {
                            var ng = m.ng[i];

                            int group_index = i + 1;
                            string name = ng.n!;

                            bool success = ng.v != null;

                            if( !success )
                            {
                                match.AddFailedGroup( name );
                            }
                            else
                            {
                                // no details

                                match.AddSucceededNoDetailsGroup( name, ng.v! );

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
            string worker_exe = Path.Combine( assembly_dir, "regexpworker.bin" );

            return worker_exe;
        }
    }
}
