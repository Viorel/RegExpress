﻿using System;
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


namespace DotNETFrameworkPlugin
{
    class Matcher : IMatcher
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
            var data = new { cmd = "m", text, pattern = Pattern, options = Options };

            string json = JsonSerializer.Serialize( data );

            string? stdout_contents;
            string? stderr_contents;

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, json, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            WorkerMatch[]? worker_matches = JsonSerializer.Deserialize<WorkerMatch[]>( stdout_contents! );

            SimpleMatch[] matches = new SimpleMatch[worker_matches!.Length];
            SimpleTextGetter text_getter = new SimpleTextGetter( text );

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

        #endregion IMatcher


        public static string? GetVersion( ICancellable cnc )
        {
            try
            {
                string? stdout_contents;
                string? stderr_contents;

                if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, @"{""cmd"":""v""}", out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
                {
                    return null;
                }

                if( cnc.IsCancellationRequested ) return null;

                if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

                if( stdout_contents == null ) throw new Exception( "Null response" );

                VersionResponse response = JsonSerializer.Deserialize<VersionResponse>( stdout_contents )!;

                return response.version;
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                return null;
            }
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, "Worker", @"DotNET6Worker.bin" );

            return worker_exe;
        }

    }
}