﻿using System;
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


namespace RustPlugin
{
    static class Matcher
    {
        class VersionResponse
        {
            public string version { get; set; }
        }

        class MatchesResponse
        {
            public string[] names { get; set; }
            public int[][][] matches { get; set; }
        }


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            if( options.@struct == StructEnum.None )
            {
                throw new ApplicationException( "Invalid struct." );
            }

            //UInt64 size_limit = 0;
            //UInt64 dfa_size_limit = 0;
            //UInt32 nest_limit = 0;

            //if( !string.IsNullOrWhiteSpace( Options.size_limit ) && !UInt64.TryParse( Options.size_limit, out size_limit ) )
            //{
            //    throw new ApplicationException( "Invalid size_limit." );
            //}

            //if( !string.IsNullOrWhiteSpace( Options.dfa_size_limit ) && !UInt64.TryParse( Options.dfa_size_limit, out dfa_size_limit ) )
            //{
            //    throw new ApplicationException( "Invalid dfa_size_limit." );
            //}

            //if( !string.IsNullOrWhiteSpace( Options.nest_limit ) && !UInt32.TryParse( Options.nest_limit, out nest_limit ) )
            //{
            //    throw new ApplicationException( "Invalid nest_limit." );
            //}

            byte[] text_utf8_bytes = Encoding.UTF8.GetBytes( text );

            var o = new StringBuilder( );

            if( options.case_insensitive ) o.Append( "i" );
            if( options.multi_line ) o.Append( "m" );
            if( options.dot_matches_new_line ) o.Append( "s" );
            if( options.swap_greed ) o.Append( "U" );
            if( options.ignore_whitespace ) o.Append( "x" );
            if( options.unicode ) o.Append( "u" );
            if( options.octal ) o.Append( "O" );

            var obj = new
            {
                s = options.@struct,
                p = pattern,
                t = text,
                o = o.ToString( ),
                sl = options.size_limit?.Trim( ) ?? "",
                dsl = options.dfa_size_limit?.Trim( ) ?? "",
                nl = options.nest_limit?.Trim( ) ?? "",
            };

            string json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );

            string? stdout_contents;
            string? stderr_contents;

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, json, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            MatchesResponse? response = JsonSerializer.Deserialize<MatchesResponse>( stdout_contents );

            if( response == null ) throw new Exception( "Null response" );

            List<IMatch> matches = new( );
            ISimpleTextGetter? stg = null;

            foreach( var m in response.matches )
            {
                SimpleMatch? match = null;

                for( int group_index = 0; group_index < m.Length; group_index++ )
                {
                    int[] g = m[group_index];
                    bool success = g.Length == 2;

                    int byte_start = success ? g[0] : 0;
                    int byte_end = success ? g[1] : 0;

                    int char_start = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_start );
                    int char_end = Encoding.UTF8.GetCharCount( text_utf8_bytes, 0, byte_end );
                    int char_length = char_end - char_start;

                    if( group_index == 0 )
                    {
                        Debug.Assert( match == null );
                        Debug.Assert( success );

                        stg ??= new SimpleTextGetter( text );

                        match = SimpleMatch.Create( char_start, char_end - char_start, stg );
                    }

                    Debug.Assert( match != null );

                    string name = response.names[group_index];
                    if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                    if( success )
                    {
                        match.AddGroup( char_start, char_length, true, name );
                    }
                    else
                    {
                        match.AddGroup( 0, 0, false, name );
                    }
                }

                Debug.Assert( match != null );

                matches.Add( match );
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            MemoryStream? stdout_contents;
            string? stderr_contents;

            Action<Stream> stdinWriter = s =>
            {
                using( var bw = new BinaryWriter( s, Encoding.UTF8, leaveOpen: false ) )
                {
                    bw.Write( "{\"c\":\"v\"}" );
                }
            };

            if( !ProcessUtilities.InvokeExe( cnc, GetWorkerExePath( ), null, stdinWriter, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

#if DEBUG
            {
                var text = Encoding.UTF8.GetString( stdout_contents.GetBuffer( ), 0, checked((int)stdout_contents.Length) );
            }
#endif

            VersionResponse? r = JsonSerializer.Deserialize<VersionResponse>( stdout_contents );

            if( r == null ) throw new Exception( "Null response" );

            return r!.version;
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"RustWorker.bin" );

            return worker_exe;
        }

    }
}