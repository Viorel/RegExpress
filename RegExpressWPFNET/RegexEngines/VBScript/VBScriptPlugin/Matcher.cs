using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Printing;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
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
    static partial class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            string options_str = "";
            if( options.IgnoreCase ) options_str += "i";
            if( options.Global ) options_str += "g";

            using ProcessHelper ph = new ProcessHelper( "cscript.exe" );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = new[] { "/nologo", GetWorkerPath( ), "x" };

            ph.StreamWriter = sw =>
            {
                sw.Write( ToArg( pattern ) );
                sw.Write( "\u001F" );
                sw.Write( ToArg( text ) );
                sw.Write( "\u001F" );
                sw.Write( options_str );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) )
            {
                // Possible: <path>\VBScriptWorker.vbs(31, 1) Microsoft VBScript runtime error: <text of error>

                var m = ErrorRegex( ).Match( ph.Error );
                if( m.Success )
                {
                    throw new Exception( m.Groups["err"].Value );
                }

                throw new Exception( ph.Error );
            }

            List<SimpleMatch> matches = new( );
            SimpleTextGetter stg = new( text );
            SimpleMatch? current_match = null;

            string? line;

            while( ( line = ph.StreamReader.ReadLine( ) ) != null )
            {
                var m = MatchRegex( ).Match( line );
                if( m.Success )
                {
                    current_match = SimpleMatch.Create( int.Parse( m.Groups[1].Value ), int.Parse( m.Groups[2].Value ), stg );

                    current_match.AddGroup( current_match.Index, current_match.Length, true, "" ); // default group

                    matches.Add( current_match );
                }
                else
                {
                    var sm = SubmatchRegex( ).Match( line );
                    if( sm.Success )
                    {
                        if( current_match == null ) throw new InvalidOperationException( );

                        //int index = current_match.Index + int.Parse( sm.Groups[1].Value ) - 1;
                        //int length = int.Parse( sm.Groups[2].Value );

                        //current_match.AddGroup( index, length, true, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ) );

                        string value = sm.Groups[1].Value;

                        //value = JsonNode.Parse( value )!.GetValue<string>( ); // does not work with incomplete surrogate pairs
                        //value = CSharpScript.EvaluateAsync<string>( value ).Result; // too large dependencies

                        Debug.Assert( value.StartsWith( '"' ) );
                        Debug.Assert( value.EndsWith( '"' ) );

                        value = value[1..^1];

                        value = UCodeRegex( ).Replace( value, m => ( (char)Convert.ToUInt16( m.Groups[1].Value, 16 ) ).ToString( ) );

                        current_match.AddGroup( current_match.Index, value.Length, true, current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture ), new SimpleTextGetterWithOffset( current_match.Index, value ) );
                    }
                    else
                    {
#if DEBUG
                        throw new Exception( $"Bad response: '{line}'." );
#else
                        throw new Exception( "Bad response." );
#endif
                    }
                }
            }

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new( "cscript.exe" );

            ph.AllEncoding = EncodingEnum.ASCII;
            ph.Arguments = new[] { "/nologo", GetWorkerPath( ), "v" };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string version_s = ph.StreamReader.ReadToEnd( ).Trim( );

            return version_s;
        }


        static string ToArg( string text )
        {
            StringBuilder sb = new( );
            bool is_open_segment = false;

            foreach( char c in text )
            {
                if( char.IsAsciiLetterOrDigit( c ) )
                {
                    if( is_open_segment )
                    {
                        sb.Append( c );
                    }
                    else
                    {
                        if( sb.Length == 0 )
                        {
                            sb.Append( '\'' ).Append( c );
                        }
                        else
                        {
                            sb.Append( "&'" ).Append( c );
                        }

                        is_open_segment = true;
                    }
                }
                else
                {
                    if( is_open_segment )
                    {
                        sb.Append( '\'' );
                    }

                    if( sb.Length != 0 )
                    {
                        sb.Append( '&' );
                    }

                    sb.Append( "ChrW(&H" ).Append( ( (uint)c ).ToString( "X" ) ).Append( ')' );

                    is_open_segment = false;
                }
            }

            if( is_open_segment )
            {
                sb.Append( '\'' );
            }

            string r = sb.ToString( );

            if( r.Length == 0 ) r = "''";

            return r;
        }


        static string GetWorkerPath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_path = Path.Combine( assembly_dir, @"VBScriptWorker.vbs" );

            return worker_path;
        }


        [GeneratedRegex( @"\\VBScriptWorker\.vbs\s*\(\d+,\s*\d+\)\s*(?<err>Microsoft VBScript runtime error:.*)" )]
        private static partial Regex ErrorRegex( );

        [GeneratedRegex( @"^m\s+(\d+)\s+(\d+)" )]
        private static partial Regex MatchRegex( );

        [GeneratedRegex( @"^s\s+("".*"")" )]
        private static partial Regex SubmatchRegex( );

        [GeneratedRegex( @"\\u([0-9A-Fa-f]{4})" )]
        private static partial Regex UCodeRegex( );
    }
}
