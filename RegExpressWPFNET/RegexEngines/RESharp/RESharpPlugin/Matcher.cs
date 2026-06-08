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

namespace RESharpPlugin
{
    static class Matcher
    {

        sealed class WorkerMatch
        {
            public int index { get; init; }
            public int length { get; init; }
        }

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            var data = new
            {
                command = "match",
                text,
                pattern,
                options = new
                {
                    IgnoreCase = options.IgnoreCase,
                    UseDotnetUnicode = options.UseDotnetUnicode,
                    MinimizePattern = options.MinimizePattern,
                    FindLookaroundPrefix = options.FindLookaroundPrefix,
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

            WorkerMatch[]? worker_matches = JsonSerializer.Deserialize<WorkerMatch[]>( ph.OutputStream );

            SimpleMatch[] matches = new SimpleMatch[worker_matches!.Length];
            SimpleTextGetter text_getter = new( text );

            for( int i = 0; i < worker_matches.Length; i++ )
            {
                WorkerMatch m = worker_matches[i];
                SimpleMatch sm = SimpleMatch.Create( m.index, m.length, text_getter );

                var sg = sm.AddGroup( m.index, m.length, true, "" ); // default group

                matches[i] = sm;
            }

            return new RegexMatches( matches.Length, matches );
        }

        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, "Worker", @"RESharpWorker.exe" );

            return worker_exe;
        }
    }
}
