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


namespace StdPlugin
{
    static partial class MatcherGCC
    {
        static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.WriteLine( "\"m\"" ); // (command)
                sw.WriteLine( JsonSerializer.Serialize( pattern ) );
                sw.WriteLine( JsonSerializer.Serialize( text ) );

                string grammar = Enum.GetName( options.Grammar )!;
                sw.WriteLine( JsonSerializer.Serialize( grammar ) );

                sw.WriteLine( JsonSerializer.Serialize( options.Locale ) );

                string flags = "";
                if( options.icase ) flags += "icase ";
                if( options.nosubs ) flags += "nosubs ";
                if( options.optimize ) flags += "optimize ";
                if( options.collate ) flags += "collate ";
                if( options.multiline ) flags += "multiline ";

                if( options.match_not_bol ) flags += "match_not_bol ";
                if( options.match_not_eol ) flags += "match_not_eol ";
                if( options.match_not_bow ) flags += "match_not_bow ";
                if( options.match_not_eow ) flags += "match_not_eow ";
                if( options.match_any ) flags += "match_any ";
                if( options.match_not_null ) flags += "match_not_null ";
                if( options.match_continuous ) flags += "match_continuous ";
                if( options.match_prev_avail ) flags += "match_prev_avail ";

                sw.WriteLine( JsonSerializer.Serialize( flags ) );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

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
                    else
                    {
                        Match g = ParseGroupRegex( ).Match( line );
                        if( g.Success )
                        {
                            if( current_match == null ) throw new Exception( "Invalid response." );

                            int index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture );
                            int length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                            bool success = index >= 0;

                            current_match.AddGroup( success ? (int)index : 0, success ? (int)length : 0, success, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );

                            continue;
                        }
                    }
                }



                /*
                    var sr = ph.StreamReader;

                    List<IMatch> matches = new( );
                    SimpleTextGetter stg = new( text );
                    SimpleMatch? current_match = null;

                    if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response." );

                    bool done = false;

                    while( !done )
                    {
                        switch( br.ReadByte( ) )
                        {
                        case (byte)'m':
                        {
                            Int64 index = br.ReadInt64( );
                            Int64 length = br.ReadInt64( );
                            current_match = SimpleMatch.Create( (int)index, (int)length, stg );
                            matches.Add( current_match );
                        }
                        break;
                        case (byte)'g':
                        {
                            if( current_match == null ) throw new Exception( "Invalid response." );
                            Int64 index = br.ReadInt64( );
                            Int64 length = br.ReadInt64( );
                            bool success = index >= 0;
                            current_match.AddGroup( success ? (int)index : 0, success ? (int)length : 0, success, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );
                        }
                        break;
                        case (byte)'e':
                            done = true;
                            break;
                        default:
                            throw new Exception( "Invalid response." );
                        }
                */
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.WriteLine( "\"v\"" ); // (command)
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string version_s = ph.StreamReader.ReadToEnd( ).Trim( );

            return version_s;
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"GccWorker.bin" );

            return worker_exe;
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

        [GeneratedRegex( @"(?x)^\s* m \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );

        [GeneratedRegex( @"(?x)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
        private static partial Regex ParseGroupRegex( );
    }
}
