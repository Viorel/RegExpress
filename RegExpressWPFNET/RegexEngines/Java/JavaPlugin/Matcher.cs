using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
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


namespace JavaPlugin
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
            var sb = new StringBuilder( );
            if( Options.CANON_EQ ) sb.Append( ",CANON_EQ" );
            if( Options.CASE_INSENSITIVE ) sb.Append( ",CASE_INSENSITIVE" );
            if( Options.COMMENTS ) sb.Append( ",COMMENTS" );
            if( Options.DOTALL ) sb.Append( ",DOTALL" );
            if( Options.LITERAL ) sb.Append( ",LITERAL" );
            if( Options.MULTILINE ) sb.Append( ",MULTILINE" );
            if( Options.UNICODE_CASE ) sb.Append( ",UNICODE_CASE" );
            if( Options.UNICODE_CHARACTER_CLASS ) sb.Append( ",UNICODE_CHARACTER_CLASS" );
            if( Options.UNIX_LINES ) sb.Append( ",UNIX_LINES" );
            sb.Append( ',' );

            string options_s = sb.ToString( );

            (string? javaExePath, string? workerDir) = GetPaths( );

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( string.IsNullOrWhiteSpace( javaExePath ) ) throw new Exception( "Cannot initialize JRE" );
            if( string.IsNullOrWhiteSpace( workerDir ) ) throw new Exception( "Cannot initialize Java worker" );

            string? stdout_contents;
            string? stderr_contents;

            Action<StreamWriter> stdin_writer = new Action<StreamWriter>( sw =>
            {
                sw.Write( "get-matches" );
                sw.Write( "\x1F" );
                sw.Write( Pattern );
                sw.Write( "\x1F" );
                sw.Write( text );
                sw.Write( "\x1F" );
                sw.Write( options_s );
            } );

            if( !ProcessUtilities.InvokeExe( cnc, javaExePath, new[] { "-cp", workerDir!, "JavaWorker" }, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return RegexMatches.Empty;
            }

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            var matches = new List<SimpleMatch>( );
            var text_getter = new SimpleTextGetter( text );

            using( var sr = new StringReader( stdout_contents ) )
            {
                string? line;

                SimpleMatch? match = null;

                while( ( line = sr.ReadLine( ) ) != null )
                {
                    line = line.Trim( );

                    if( line.Length == 0 ) continue;
                    if( line.StartsWith( "D " ) ) continue; // (for debugging)

                    {
                        var reM = new Regex( @"^M (\d+) (\d+)" );
                        var mM = reM.Match( line );
                        if( mM.Success )
                        {
                            int start = int.Parse( mM.Groups[1].Value, CultureInfo.InvariantCulture );
                            int end = int.Parse( mM.Groups[2].Value, CultureInfo.InvariantCulture );

                            match = SimpleMatch.Create( start, end - start, text_getter );

                            matches.Add( match );

                            continue;
                        }
                    }

                    {
                        var reG = new Regex( @"^G (-?\d+) (-?\d+)" );
                        var mG = reG.Match( line );
                        if( mG.Success )
                        {
                            int start = int.Parse( mG.Groups[1].Value, CultureInfo.InvariantCulture );
                            int end = int.Parse( mG.Groups[2].Value, CultureInfo.InvariantCulture );

                            bool success = start >= 0;

                            if( match == null ) throw new InvalidOperationException( );

                            SimpleGroup group = match.AddGroup( start, success ? end - start : 0, success, match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );

                            continue;
                        }
                    }

                    {
                        var reN = new Regex( @"^N (\d+) (\d+) <(.+)>" );
                        var mN = reN.Match( line );
                        if( mN.Success )
                        {
                            int start = int.Parse( mN.Groups[1].Value, CultureInfo.InvariantCulture );
                            int end = int.Parse( mN.Groups[2].Value, CultureInfo.InvariantCulture );
                            string name = mN.Groups[3].Value;

                            int length = end - start;

                            // try to identify the named group by index and length;
                            // cannot be done univocally in situations like "(?<name1>(?<name2>(.))", where index and length are the same

                            if( match == null ) throw new InvalidOperationException( );

                            var f = match.Groups
                                .Select( ( g, i ) => new { g, i } )
                                .Skip( 1 )
                                .Where( p => p.g.Index == start && p.g.Length == length && Regex.IsMatch( p.g.Name, @"\d+" ) )
                                .FirstOrDefault( );

                            if( f != null ) match.SetGroupName( f.i, name );

                            continue;
                        }
                    }

#if DEBUG
                    if( !string.IsNullOrWhiteSpace( line ) )
                    {
                        // invalid line
                        if( Debugger.IsAttached ) Debugger.Break( );
                    }
#endif
                }
            }

            return new RegexMatches( matches.Count, matches );
        }

        #endregion IMatcher


        public static string? GetVersion( ICancellable cnc )
        {
            string? stdout_contents;
            string? stderr_contents;

            (string? javaExePath, string? workerDir) = GetPaths( );

            if( string.IsNullOrWhiteSpace( javaExePath ) ) throw new Exception( "Cannot initialize JRE" );
            if( string.IsNullOrWhiteSpace( workerDir ) ) throw new Exception( "Cannot initialize Java worker" );

            Action<StreamWriter> stdin_writer = sw =>
            {
                sw.Write( "get-version" );
            };

            if( !ProcessUtilities.InvokeExe( cnc, javaExePath, new[] { "-cp", workerDir!, "JavaWorker" }, stdin_writer, out stdout_contents, out stderr_contents, EncodingEnum.UTF8 ) )
            {
                return null;
            }

            if( cnc.IsCancellationRequested ) return null;

            if( !string.IsNullOrWhiteSpace( stderr_contents ) ) throw new Exception( stderr_contents );

            if( stdout_contents == null ) throw new Exception( "Null response" );

            stdout_contents = stdout_contents.Trim( );

            if( !stdout_contents.StartsWith( "Version=" ) ) throw new InvalidOperationException( );

            return stdout_contents.Substring( "Version=".Length );
        }


        static (string? javaPath, string? workerDir) GetPaths( )
        {
            DecompressJre( );

            if( JrePath == null )
            {
                return (null, null);
            }
            else
            {
                // TODO: do once

                return (
                    Path.Combine( JrePath, @"JRE-min\bin\java.exe" ),
                    Path.Combine( GetPluginDirectory( ) )
                    );
            }
        }


        static string GetPluginDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }


        static string GetTemporaryDirectory( )
        {
            string temp_path = Path.GetTempPath( );
            string dir = Path.Combine( temp_path, Path.GetRandomFileName( ) ); // TODO: exclude almost impossible collisions

            return dir;
        }


        static readonly object Locker = new object( );
        static string? JrePath = null;
        static bool IsJreExtractionDone = false;

        static void DecompressJre( )
        {
            if( IsJreExtractionDone ) return;

            lock( Locker )
            {
                if( IsJreExtractionDone ) return;

                try
                {
                    string plugin_dir = GetPluginDirectory( );
                    string dest_jre_path = GetTemporaryDirectory( );
                    string source_zip = Path.Combine( plugin_dir, @"JRE-min.zip" );

                    ZipFile.ExtractToDirectory( source_zip, dest_jre_path );

                    AppDomain.CurrentDomain.ProcessExit += ( s, a ) =>
                    {
                        try
                        {
                            Directory.Delete( dest_jre_path, recursive: true );
                        }
                        catch( Exception exc )
                        {
                            _ = exc;
                            if( Debugger.IsAttached ) Debugger.Break( );

                            // ignore
                        }
                    };

                    JrePath = dest_jre_path;
                }
                catch( Exception exc )
                {
                    _ = exc;
                    if( Debugger.IsAttached ) Debugger.Break( );

                    JrePath = null;
                }

                IsJreExtractionDone = true;
            }
        }
    }
}
