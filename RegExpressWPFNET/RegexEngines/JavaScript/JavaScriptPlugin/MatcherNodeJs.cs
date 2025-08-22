using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Formats.Tar;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace JavaScriptPlugin
{
    static partial class MatcherNodeJs
    {
        public class ResponseVersion
        {
            public string? node { get; set; }
            public string? v8 { get; set; }
        }

        public class ResponseMatch
        {
            [JsonPropertyName( "g" )]
            public Dictionary<string, int[]>? Groups { get; set; }

            [JsonPropertyName( "i" )]
            public List<int[]>? Indices { get; set; }
        }

        public class ResponseMatches
        {
            public List<ResponseMatch>? Matches { get; set; }

            public string? Error { get; set; }
        }

        static readonly Lazy<string?> LazyVersion = new( ( ) => GetVersion( ICancellable.NonCancellable ) );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string flags = string.Concat(
                options.i ? "i" : "",
                options.m ? "m" : "",
                options.s ? "s" : "",
                options.u ? "u" : "",
                options.v ? "v" : "",
                options.y ? "y" : "",
                options.g ? "g" : ""
                );

            string func = options.Function switch { FunctionEnum.MatchAll => "matchAll", FunctionEnum.Exec => "exec", _ => throw new InvalidOperationException( ) };

            var data = new { cmd = "match", pattern, text, flags, func };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
                sw.Flush( );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            ResponseMatches? response = JsonSerializer.Deserialize<ResponseMatches>( ph.OutputStream );

            if( response == null ) throw new Exception( "JavaScript failed." );
            if( !string.IsNullOrWhiteSpace( response.Error ) ) throw new Exception( response.Error );

            string[] distributed_names = FigureOutGroupNames( response );
            Debug.Assert( distributed_names[0] == null );

            List<IMatch> matches = [];
            SimpleTextGetter stg = new( text );

            foreach( var cm in response.Matches! )
            {
                if( cm.Indices!.Any( ) )
                {
                    var start = cm.Indices![0][0];
                    var end = cm.Indices[0][1];

                    var sm = SimpleMatch.Create( start, end - start, stg );

                    sm.AddGroup( sm.Index, sm.Length, true, "0" ); // (default group)

                    for( int i = 1; i < cm.Indices.Count; ++i )
                    {
                        string name;
                        if( i < distributed_names.Length && distributed_names[i] != null )
                        {
                            name = distributed_names[i];
                        }
                        else
                        {
                            name = i.ToString( CultureInfo.InvariantCulture );
                        }

                        var g = cm.Indices[i];

                        if( g == null )
                        {
                            sm.AddGroup( -1, 0, false, name );
                        }
                        else
                        {
                            start = cm.Indices[i][0];
                            end = cm.Indices[i][1];

                            sm.AddGroup( start, end - start, true, name );
                        }
                    }

                    matches.Add( sm );
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        public static string? GetVersion( ICancellable cnc )
        {
            var data = new { cmd = "version" };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.ASCII;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
                sw.Flush( );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            ResponseVersion? response = JsonSerializer.Deserialize<ResponseVersion>( ph.OutputStream );

            string? version = response?.node;

            return version;
        }


        static string[] FigureOutGroupNames( ResponseMatches clientResponse )
        {
            if( clientResponse.Matches == null ) return new string[0];

            var possible_indices = new Dictionary<string, HashSet<int>>( );

            foreach( var m in clientResponse.Matches )
            {
                if( m.Groups == null ) continue;

                foreach( var g in m.Groups )
                {
                    string group_name = g.Key;
                    int[] group_index = g.Value;

                    var possible_indices_this_group = new List<int>( );

                    for( var i = 1; i < m.Indices!.Count; ++i )
                    {
                        if( m.Indices[i] == null ) continue;

                        if( group_index[0] == m.Indices[i][0] && group_index[1] == m.Indices[i][1] )
                        {
                            possible_indices_this_group.Add( i );
                        }
                    }

                    HashSet<int>? existing_possible_indices;

                    if( !possible_indices.TryGetValue( group_name, out existing_possible_indices ) )
                    {
                        possible_indices.Add( group_name, possible_indices_this_group.ToHashSet( ) );
                    }
                    else
                    {
                        possible_indices[group_name].UnionWith( possible_indices_this_group );
                    }
                }
            }

            // order by number of possibilities

            var ordered = possible_indices.OrderBy( kv => kv.Value.Count ).ToArray( );

            //// exclude previous, more probable possibilities

            //for( int i = 1; i < ordered.Length; ++i )
            //{
            //	for( int j = 0; j < i; ++j )
            //	{
            //		ordered[i].Value.ExceptWith( ordered[j].Value );
            //	}
            //}

            int max_group_number = ordered.Any( ) ? ordered.SelectMany( kv => kv.Value ).Max( ) : 0;

            string[] distributed_names = new string[max_group_number + 1];

            // keep one (first) possibility, which is not used yet

            for( int i = 0; i < ordered.Length; ++i )
            {
                foreach( int k in ordered[i].Value )
                {
                    if( distributed_names[k] == null )
                    {
                        distributed_names[k] = ordered[i].Key;
                        break;
                    }
                }
            }

            return distributed_names;
        }

        static string GetPluginDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = System.IO.Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }

        static string GetWorkerExePath( )
        {
            Decompress( );

            Debug.Assert( TempFolder != null );

            return Path.Combine( TempFolder!, "NodeJsWorker.exe" );
        }

        static string GetTemporaryDirectory( )
        {
            string temp_path = System.IO.Path.GetTempPath( );
            string dir = System.IO.Path.Combine( temp_path, System.IO.Path.GetRandomFileName( ) ); // TODO: exclude almost impossible collisions

            return dir;
        }

        static readonly object Locker = new object( );
        static string? TempFolder = null;
        static bool IsExtractionDone = false;

        static void Decompress( )
        {
            if( IsExtractionDone ) return;

            lock( Locker )
            {
                if( IsExtractionDone ) return;

                try
                {
                    string plugin_dir = GetPluginDirectory( );
                    string dest_folder = GetTemporaryDirectory( );
                    string source_zip = System.IO.Path.Combine( plugin_dir, @"NodeJsWorker.zip" );

                    ZipFile.ExtractToDirectory( source_zip, dest_folder );

                    AppDomain.CurrentDomain.ProcessExit += ( s, a ) =>
                    {
                        try
                        {
                            Directory.Delete( dest_folder, recursive: true );
                        }
                        catch( Exception exc )
                        {
                            _ = exc;
                            if( Debugger.IsAttached ) Debugger.Break( );

                            // ignore
                        }
                    };

                    TempFolder = dest_folder;
                }
                catch( Exception exc )
                {
                    _ = exc;
                    if( Debugger.IsAttached ) Debugger.Break( );

                    TempFolder = null;
                }

                IsExtractionDone = true;
            }
        }

        internal static void StartGetVersion( Action<string?> setNodeJsVersion )
        {
            if( LazyVersion.IsValueCreated )
            {
                setNodeJsVersion( LazyVersion.Value );

                return;
            }

            Thread t = new( ( ) =>
            {
                setNodeJsVersion( LazyVersion.Value );
            } )
            {
                IsBackground = true
            };

            t.Start( );
        }
    }
}
