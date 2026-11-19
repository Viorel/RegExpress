using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;


namespace DotNETFrameworkConsole
{
    internal class Program
    {
        sealed class Input
        {
            public string cmd { get; set; }
            public string pattern { get; set; }
            public string text { get; set; }
            public Options options { get; set; }
        }

        sealed class Options
        {
            public bool IgnoreCase { get; set; }
            public bool Multiline { get; set; }
            public bool ExplicitCapture { get; set; }
            public bool Compiled { get; set; }
            public bool Singleline { get; set; }
            public bool IgnorePatternWhitespace { get; set; }
            public bool RightToLeft { get; set; }
            public bool ECMAScript { get; set; }
            public bool CultureInvariant { get; set; }

            public long? Timeout { get; set; }
        }

        public sealed class Output
        {
            public string[] Names { get; set; }
            public List<List<List<int>>> Matches { get; set; }
        }


        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                if( args.Length > 0 )
                {
                    if( args.Length == 1 )
                    {
                        if( args[0] == "v" )
                        {
                            GetVersion( );

                            return;
                        }
                    }

                    throw new NotSupportedException( "Incorrect arguments" );
                }

                string input_string = Console.In.ReadToEnd( );

                Input input = JsonSerializer.Deserialize<Input>( input_string );

                RegexOptions options = RegexOptions.None;
                if( input.options != null )
                {
                    Options o = input.options;

                    if( o.IgnoreCase ) options |= RegexOptions.IgnoreCase;
                    if( o.Multiline ) options |= RegexOptions.Multiline;
                    if( o.ExplicitCapture ) options |= RegexOptions.ExplicitCapture;
                    if( o.Compiled ) options |= RegexOptions.Compiled;
                    if( o.Singleline ) options |= RegexOptions.Singleline;
                    if( o.IgnorePatternWhitespace ) options |= RegexOptions.IgnorePatternWhitespace;
                    if( o.RightToLeft ) options |= RegexOptions.RightToLeft;
                    if( o.ECMAScript ) options |= RegexOptions.ECMAScript;
                    if( o.CultureInvariant ) options |= RegexOptions.CultureInvariant;
                }

                Regex regex = input.options?.Timeout == null ?
                    new Regex( input.pattern, options ) :
                    new Regex( input.pattern, options, TimeSpan.FromMilliseconds( input.options.Timeout.Value ) );

                List<List<List<int>>> output_matches = new List<List<List<int>>>( );

                foreach( Match match in regex.Matches( input.text ) )
                {
                    Debug.Assert( match.Success );
                    if( !match.Success ) break;

                    List<List<int>> groups = new List<List<int>>( );

                    foreach( Group group in match.Groups.Cast<Group>( ) )
                    {
                        List<int> captures = new List<int>( );

                        if( !group.Success )
                        {
                            // nothing added
                        }
                        else
                        {
                            foreach( Capture capture in group.Captures )
                            {
                                captures.Add( capture.Index );
                                captures.Add( capture.Length );
                            }
                        }

                        groups.Add( captures );
                    }

                    output_matches.Add( groups );
                }

                string json = JsonSerializer.Serialize( new Output { Names = regex.GetGroupNames( ), Matches = output_matches } );

                Console.Out.WriteLine( json );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( $"{exc.GetType( ).Name}: {exc.Message}" );
            }
        }

        private static void GetVersion( )
        {
            TargetFrameworkAttribute targetFrameworkAttribute = Assembly
                .GetExecutingAssembly( )
                .GetCustomAttributes<TargetFrameworkAttribute>( )
                .SingleOrDefault( );

            string version = null;

            if( targetFrameworkAttribute != null )
            {
                Match m = Regex.Match( targetFrameworkAttribute.FrameworkName, @"Version\s*=\s*v?(?<version>\d+(\.\d+){0,2})", RegexOptions.IgnoreCase | RegexOptions.ExplicitCapture );

                if( m.Success )
                {
                    version = m.Groups["version"].Value;
                }
            }

            if( version == null )
            {
                version = new Version( Environment.Version.Major, Environment.Version.Minor, Environment.Version.Build ).ToString( ); // not interested in revision
            }
            else
            {
                //version += " (" + new Version( Environment.Version.Major, Environment.Version.Minor, Environment.Version.Build ) + ")";
            }

            var response = new { Version = version };
            var response_string = JsonSerializer.Serialize( response );

            Console.Out.WriteLine( response_string );
        }

    }
}
