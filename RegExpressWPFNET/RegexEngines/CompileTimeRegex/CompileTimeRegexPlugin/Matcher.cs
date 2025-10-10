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
using System.Windows;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace CompileTimeRegexPlugin
{
    static partial class Matcher
    {
        static readonly Encoding StrictAsciiEncoding = Encoding.GetEncoding( "ASCII", EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback );


        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string temp_dir = Path.Combine( Path.GetTempPath( ), Path.GetRandomFileName( ) );

            try
            {
                new DirectoryInfo( temp_dir ).Create( );

                string worker_dir = GetWorkerDirectory( );

                // copy files

                CopyDirectory(
                    Path.Combine( worker_dir, "compile-time-regular-expressions" ),
                    Path.Combine( temp_dir, "compile-time-regular-expressions" ),
                    recursive: true );

                string build_cmd_full_path = Path.Combine( temp_dir, "build.cmd" );

                File.Copy(
                    Path.Combine( worker_dir, "build.cmd" ),
                    build_cmd_full_path );

                // create CPP file
                {
                    string cpp_contents = File.ReadAllText( Path.Combine( worker_dir, "CompileTimeRegexSample.cpp" ) );

                    cpp_contents = ReplaceRegex( ).Replace( cpp_contents, m =>
                    {
                        return m.Groups["p"].Success ? "L" + ToCString( pattern ) : m.Groups["t"].Success ? "L" + ToCString( text ) : "";
                    } );

                    File.WriteAllText( Path.Combine( temp_dir, "CompileTimeRegexSample.cpp" ), cpp_contents );
                }

                // build
                string built_exe_full_path = Path.Combine( temp_dir, "CompileTimeRegexSample.exe" );
                {
                    ProcessHelper ph = new( build_cmd_full_path )
                    {
                        AllEncoding = EncodingEnum.ASCII
                    };

                    if( !ph.Start( cnc ) ) return RegexMatches.Empty;

                    if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

                    // file is not created in case of errors

                    if( !File.Exists( built_exe_full_path ) )
                    {
                        using StreamReader sr = new( ph.OutputStream );
                        string output = sr.ReadToEnd( );

                        // get error messages
                        string filtered_output = string.Join( Environment.NewLine, ErrorMessageRegex( ).Matches( output ).Cast<Match>( ).Select( m => m.Value ) );

                        throw new Exception( $"The code failed to compile.{Environment.NewLine}{Environment.NewLine}{filtered_output}" );
                    }
                }

                // execute
                {
                    ProcessHelper ph = new( built_exe_full_path )
                    {
                        AllEncoding = EncodingEnum.ASCII
                    };

                    if( !ph.Start( cnc ) ) return RegexMatches.Empty;

                    if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

                    //string[] possible_group_names =
                    //    PossibleNamesRegex( )
                    //        .Matches( pattern )
                    //        .Select( m => m.Groups["n"] )
                    //        .Where( g => g.Success )
                    //        .Select( g => g.Value )
                    //        .ToArray( );
                    //Application.Current.Exit += (o,a ) => { } ;

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
                        }
                        {
                            Match g = ParseGroupRegex( ).Match( line );
                            if( g.Success )
                            {
                                if( current_match == null ) throw new Exception( "Invalid response." );

                                int index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture );
                                int length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                                bool success = index >= 0;

                                string? name = null;
                                // TODO: find name

                                name ??= current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                                current_match.AddGroup( success ? (int)index : 0, success ? (int)length : 0, success, name );

                                continue;
                            }
                        }
                    }

                    return new RegexMatches( matches.Count, matches );
                }
            }
            catch( Exception exc )
            {
                _ = exc;

                throw;
            }
            finally
            {
                try
                {
                    new DirectoryInfo( temp_dir ).Delete( recursive: true );
                }
                catch
                {
                    // ignore?
                }
            }
        }

        public static string? GetVersion( ICancellable cnc )
        {
            return "3.10.0"; // TODO: get from sources
        }

        static string GetWorkerDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }

        static void CopyDirectory( string sourceDir, string destinationDir, bool recursive )
        {
            // From https://learn.microsoft.com/en-us/dotnet/standard/io/how-to-copy-directories

            // Get information about the source directory
            var dir = new DirectoryInfo( sourceDir );

            // Check if the source directory exists
            if( !dir.Exists )
                throw new DirectoryNotFoundException( $"Source directory not found: {dir.FullName}" );

            // Cache directories before we start copying
            DirectoryInfo[] dirs = dir.GetDirectories( );

            // Create the destination directory
            Directory.CreateDirectory( destinationDir );

            // Get the files in the source directory and copy to the destination directory
            foreach( FileInfo file in dir.GetFiles( ) )
            {
                string targetFilePath = Path.Combine( destinationDir, file.Name );
                file.CopyTo( targetFilePath );
            }

            // If recursive and copying subdirectories, recursively call this method
            if( recursive )
            {
                foreach( DirectoryInfo subDir in dirs )
                {
                    string newDestinationDir = Path.Combine( destinationDir, subDir.Name );
                    CopyDirectory( subDir.FullName, newDestinationDir, true );
                }
            }
        }

        static string ToCString( string text )
        {
            if( text.Length == 0 ) return "\"\"";

            StringBuilder sb = new( "\"" );

            for( int i = 0; i < text.Length; )
            {
                Rune rune = Rune.GetRuneAt( text, i );

                int value = rune.Value;

                if( value >= '0' && value <= '9' ||
                    value >= 'A' && value <= 'Z' ||
                    value >= 'a' && value <= 'z' ) // TODO: add more
                {
                    sb.Append( unchecked((char)value) );
                    ++i;
                }
                else if( value <= 0xFF )
                {
                    sb.Append( $"\\u{value:X4}" );
                    ++i;
                }
                else if( value <= 0xFFFF )
                {
                    sb.Append( $"\\u{value:X4}" );
                    Debug.Assert( rune.Utf16SequenceLength == 1 );
                    i += 1;
                }
                else
                {
                    sb.Append( $"\\U{value:X8}" );
                    Debug.Assert( rune.Utf16SequenceLength == 2 );
                    i += 2;
                }
            }

            return sb.Append( '"' ).ToString( );
        }

        [GeneratedRegex( @"(?<p>/\*START-PATTERN\*/.*?/\*END-PATTERN\*/) | (?<t>/\*START-TEXT\*/.*?/\*END-TEXT\*/)", RegexOptions.IgnorePatternWhitespace )]
        private static partial Regex ReplaceRegex( );

        [GeneratedRegex( @"(?<=\\.*?\(\d+\):\s*)error.*?:.*?(?=\r|\n|$)", RegexOptions.IgnorePatternWhitespace )]
        private static partial Regex ErrorMessageRegex( );

        [GeneratedRegex( "\\(\\? ((?'a'')|<) (?'n'.*?) (?(a)'|>)", RegexOptions.ExplicitCapture | RegexOptions.IgnorePatternWhitespace )]
        private static partial Regex PossibleNamesRegex( );

        [GeneratedRegex( @"(?ix)^\s* M \s+ (\d+) \s+ (\d+)" )]
        private static partial Regex ParseMatchRegex( );

        [GeneratedRegex( @"(?ix)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
        private static partial Regex ParseGroupRegex( );
    }
}
