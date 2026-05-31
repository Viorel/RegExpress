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
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Int32? region_start = null;

            if( !string.IsNullOrWhiteSpace( options.regionStart ) )
            {
                if( !Int32.TryParse( options.regionStart, out var region_start0 ) )
                {
                    throw new ApplicationException( "Invalid region start." );
                }
                else
                {
                    region_start = region_start0;
                }
            }

            Int32? region_end = null;

            if( !string.IsNullOrWhiteSpace( options.regionEnd ) )
            {
                if( !Int32.TryParse( options.regionEnd, out var region_end0 ) )
                {
                    throw new ApplicationException( "Invalid region end." );
                }
                else
                {
                    region_end = region_end0;
                }
            }

            if( ( region_start == null ) != ( region_end == null ) )
            {
                throw new ApplicationException( "Both “start” and “end” must be entered or blank." );
            }

            var sb = new StringBuilder( );
            if( options.CANON_EQ ) sb.Append( ",CANON_EQ" );
            if( options.CASE_INSENSITIVE ) sb.Append( ",CASE_INSENSITIVE" );
            if( options.COMMENTS ) sb.Append( ",COMMENTS" );
            if( options.DOTALL ) sb.Append( ",DOTALL" );
            if( options.LITERAL ) sb.Append( ",LITERAL" );
            if( options.MULTILINE ) sb.Append( ",MULTILINE" );
            if( options.UNICODE_CASE ) sb.Append( ",UNICODE_CASE" );
            if( options.UNICODE_CHARACTER_CLASS ) sb.Append( ",UNICODE_CHARACTER_CLASS" );
            if( options.UNIX_LINES ) sb.Append( ",UNIX_LINES" );
            if( options.DISABLE_UNICODE_GROUPS ) sb.Append( ",DISABLE_UNICODE_GROUPS" );
            if( options.LONGEST_MATCH ) sb.Append( ",LONGEST_MATCH" );
            if( options.useAnchoringBounds ) sb.Append( ",useAnchoringBounds" );
            if( options.useTransparentBounds ) sb.Append( ",useTransparentBounds" );
            sb.Append( ',' );

            string options_s = sb.ToString( );

            (string? javaExePath, string? workerDir) = GetPaths( );

            if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

            if( string.IsNullOrWhiteSpace( javaExePath ) ) throw new Exception( "Cannot initialize JRE" );
            if( string.IsNullOrWhiteSpace( workerDir ) ) throw new Exception( "Cannot initialize Java worker" );

            using ProcessHelper ph = new ProcessHelper( javaExePath );

            ph.AllEncoding = EncodingEnum.UTF8;

            switch( options.Package )
            {
            case PackageEnum.regex:
                ph.Arguments = ["-cp", workerDir, "JavaWorker"];
                break;
            case PackageEnum.re2j:
                ph.Arguments = ["-cp", $"{workerDir};{Path.Combine( workerDir, "re2j-1.8.jar" )}", "RE2JWorker"];
                break;
            default:
                throw new InvalidOperationException( );
            }

            ph.StreamWriter = sw =>
            {
                sw.Write( "get-matches" );
                sw.Write( "\x1F" );
                sw.Write( pattern );
                sw.Write( "\x1F" );
                sw.Write( text );
                sw.Write( "\x1F" );
                sw.Write( options_s );
                sw.Write( "\x1F" );
                sw.Write( options.regionStart );
                sw.Write( "\x1F" );
                sw.Write( options.regionEnd );
                sw.Write( "\x1F" );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            List<SimpleMatch> matches = new( );
            SimpleTextGetter text_getter = new( text );
            SimpleMatch? match = null;
            string? line;

            while( ( line = ph.StreamReader.ReadLine( ) ) != null )
            {
                line = line.Trim( );

                if( line.Length == 0 ) continue;
                if( line.StartsWith( "D " ) ) continue; // (for debugging)

                {
                    var mM = MRegex( ).Match( line );
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
                    var mG = GRegex( ).Match( line );
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
                    var mN = NRegex( ).Match( line );
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
                            .Where( p => p.g.Index == start && p.g.Length == length && IsNumberRegex( ).IsMatch( p.g.Name ) )
                            .FirstOrDefault( );

                        if( f != null ) match.SetGroupName( f.i, name );

                        continue;
                    }
                }

#if DEBUG
                if( !string.IsNullOrWhiteSpace( line ) )
                {
                    // invalid line
                    InternalConfig.HandleOtherCriticalError("Invalid Line");
                }
#endif
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            (string? javaExePath, string? workerDir) = GetPaths( );

            using ProcessHelper ph = new ProcessHelper( javaExePath! );

            ph.AllEncoding = EncodingEnum.UTF8;
            ph.Arguments = new[] { "-cp", workerDir!, "JavaWorker" };

            ph.StreamWriter = sw =>
            {
                sw.Write( "get-version" );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string? response_s = ph.StreamReader.ReadToEnd( )?.Trim( );

            if( response_s?.StartsWith( "Version=" ) != true ) throw new InvalidOperationException( );

            return response_s["Version=".Length..];
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


        static readonly Lock Locker = new( );
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
                            if (InternalConfig.HandleException( exc ))
                                throw;

                            // ignore
                        }
                    };

                    JrePath = dest_jre_path;
                }
                catch( Exception exc )
                {
                    _ = exc;
                    if (InternalConfig.HandleException( exc ))
                        throw;

                    JrePath = null;
                }

                IsJreExtractionDone = true;
            }
        }


        [GeneratedRegex( @"^M (\d+) (\d+)" )]
        private static partial Regex MRegex( );

        [GeneratedRegex( @"^G (-?\d+) (-?\d+)" )]
        private static partial Regex GRegex( );

        [GeneratedRegex( @"^N (\d+) (\d+) <(.+)>" )]
        private static partial Regex NRegex( );

        [GeneratedRegex( @"\d+" )]
        private static partial Regex IsNumberRegex( );
    }
}
