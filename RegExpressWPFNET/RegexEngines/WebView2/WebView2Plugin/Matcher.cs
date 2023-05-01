using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
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


namespace WebView2Plugin
{
    class Matcher : IMatcher
    {

        public class ResponseVersion
        {
            public string? Version { get; set; }
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


        readonly string Pattern;
        readonly Options Options;


        public Matcher( string pattern, Options options )
        {
            Pattern = pattern;
            Options = options;
        }


        #region IMatcher

        public RegexMatches Matches( string text, ICancellable cnc )
        {
            string flags = string.Concat(
                Options.i ? "i" : "",
                Options.m ? "m" : "",
                Options.s ? "s" : "",
                Options.u ? "u" : ""
                );

            string? stdout_contents;
            string? stderr_contents;

            Action<StreamWriter> stdin_writer = new Action<StreamWriter>( sw =>
            {
                sw.Write( "m \"" );
                WriteJavaScriptString( sw, Pattern );
                sw.Write( "\" \"" );
                sw.Write( flags );
                sw.Write( "\" \"" );
                WriteJavaScriptString( sw, text );
                sw.Write( "\"" );
            } );

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), "i", stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty; // (cancelled)
            }

            if( !string.IsNullOrWhiteSpace( stderr_contents ) )
            {
                throw new Exception( stderr_contents );
            }

            ResponseMatches? client_response = JsonSerializer.Deserialize<ResponseMatches>( stdout_contents );

            if( client_response == null )
            {
                throw new Exception( "JavaScript failed." );
            }

            if( !string.IsNullOrWhiteSpace( client_response.Error ) )
            {
                throw new Exception( client_response.Error );
            }

            string[] distributed_names = FigureOutGroupNames( client_response );
            Debug.Assert( distributed_names[0] == null );

            SimpleTextGetter stg = new SimpleTextGetter( text );
            List<IMatch> matches = new List<IMatch>( );

            foreach( var cm in client_response.Matches! )
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

        #endregion IMatcher


        public static string? GetVersion( ICancellable cnc )
        {
            string? stdout_contents;
            string? stderr_contents;

            string? version;

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), "v", "", out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) ||
                !string.IsNullOrWhiteSpace( stderr_contents ) ||
                string.IsNullOrWhiteSpace( stdout_contents ) )
            {
                version = "Unknown version";
            }
            else
            {
                try
                {
                    var v = JsonSerializer.Deserialize<ResponseVersion>( stdout_contents );

                    version = v!.Version;
                }
                catch( Exception )
                {
                    if( Debugger.IsAttached ) Debugger.Break( );

                    version = "Unknown version";
                }
            }

            return version;
        }


        private string[] FigureOutGroupNames( ResponseMatches clientResponse )
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

                    for( var i = 1; i < m.Indices.Count; ++i )
                    {
                        if( m.Indices[i] == null ) continue;

                        if( group_index[0] == m.Indices[i][0] && group_index[1] == m.Indices[i][1] )
                        {
                            possible_indices_this_group.Add( i );
                        }
                    }

                    HashSet<int> existing_possible_indices;

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


        private static void WriteJavaScriptString( TextWriter sw, string text )
        {
            foreach( var c in text )
            {
                if( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
                    "`~!@#$%*()-_=+[]{};:',./?".Contains( c ) )
                {
                    sw.Write( c );
                }
                else
                {
                    sw.Write( "\\u{0:X4}", unchecked((uint)c) );
                }
            }
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"WebView2Worker.bin" );

            return worker_exe;
        }
    }
}
