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
using System.Web;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using static System.Net.Mime.MediaTypeNames;


namespace VBScriptPlugin
{
    class Matcher : IMatcher
    {

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
            string? stdout_contents;
            string? stderr_contents;

            string options = "";
            if( Options.IgnoreCase ) options += "i";
            if( Options.Global ) options += "g";

            if( !ProcessUtilities.InvokeExe( cnc, "cscript.exe", new[] { "/nologo", GetWorkerPath( ), "m", Pattern, text, options }, "", out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) )
            {
                // Possible: <path>\VBScriptWorker.vbs(31, 1) Microsoft VBScript runtime error: <text of error>

                var m = Regex.Match( stderr_contents, @"\\VBScriptWorker\.vbs\s*\(\d+,\s*\d+\)\s*(?<err>Microsoft VBScript runtime error:.*)" );
                if( m.Success )
                {
                    throw new Exception( m.Groups["err"].Value );
                }

                throw new Exception( stderr_contents );
            }

            if( stdout_contents == null ) throw new Exception( "Null response" );

            var lines = stdout_contents.Split( new[] { "\r\n", "\r", "\n" }, StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries );

            var matches = new List<SimpleMatch>( );
            var stg = new SimpleTextGetter( text );

            foreach( var line in lines )
            {
                var m = Regex.Match( line, @"^m\s+(\d+)\s+(\d+)" );
                if( !m.Success )
                {
#if DEBUG
                    throw new Exception( $"Bad response: '{line}'." );
#else
                    throw new Exception( "Bad response." );
#endif
                }

                var sm = SimpleMatch.Create( int.Parse( m.Groups[1].Value ), int.Parse( m.Groups[2].Value ), stg );

                sm.AddGroup( sm.Index, sm.Length, true, "" ); // default group

                matches.Add( sm );
            }

            return new RegexMatches( matches.Count, matches );
        }

        #endregion IMatcher


        public static string? GetVersion( ICancellable cnc )
        {
            string? stdout_contents;
            string? stderr_contents;

            if( !ProcessUtilities.InvokeExe( cnc, "cscript.exe", new[] { "/nologo", GetWorkerPath( ), "v" }, "", out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            return stdout_contents.Trim();
        }


        static string GetWorkerPath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_path = Path.Combine( assembly_dir, @"VBScriptWorker.vbs" );

            return worker_path;
        }

    }
}
