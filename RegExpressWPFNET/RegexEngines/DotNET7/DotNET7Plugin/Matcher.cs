using System;
using System.Collections.Generic;
using System.Diagnostics;
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


namespace DotNET7Plugin
{
    static class Matcher
    {
        class VersionResponse
        {
            public string? version { get; init; }
        }


        class WorkerMatch
        {
            public int index { get; init; }
            public int length { get; init; }
            public List<WorkerGroup> groups { get; init; } = new List<WorkerGroup>( );
        }


        class WorkerGroup
        {
            public bool success { get; init; }
            public int index { get; init; }
            public int length { get; init; }
            public string? name { get; init; }
            public List<WorkerCapture> captures { get; init; } = new List<WorkerCapture>( );
        }


        class WorkerCapture
        {
            public int index { get; init; }
            public int length { get; init; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            var data = new { cmd = "m", text, pattern, options };
            string json = JsonSerializer.Serialize( data );

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;

            ph.StreamWriter = sw =>
            {
                sw.Write( json );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            WorkerMatch[]? worker_matches = JsonSerializer.Deserialize<WorkerMatch[]>( ph.OutputStream );

            SimpleMatch[] matches = new SimpleMatch[worker_matches!.Length];
            SimpleTextGetter text_getter = new( text );

            for( int i = 0; i < worker_matches.Length; i++ )
            {
                WorkerMatch m = worker_matches[i];
                SimpleMatch sm = SimpleMatch.Create( m.index, m.length, text_getter );

                foreach( var g in m.groups )
                {
                    var sg = sm.AddGroup( g.index, g.length, g.success, g.name ?? string.Empty );

                    foreach( var c in g.captures )
                    {
                        sg.AddCapture( c.index, c.length );
                    }
                }

                matches[i] = sm;
            }

            return new RegexMatches( matches.Length, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.UTF8;
            ph.StreamWriter = sw =>
            {
                sw.Write( @"{""cmd"":""v""}" );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            VersionResponse response = JsonSerializer.Deserialize<VersionResponse>( ph.OutputStream )!;

            return response.version;
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, "Worker", @"DotNET7Worker.bin" );

            return worker_exe;
        }

    }
}
