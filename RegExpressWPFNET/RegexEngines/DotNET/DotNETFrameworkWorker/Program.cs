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
        sealed class InputArgs
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

        sealed class WorkerMatches
        {
            public string[] Names { get; set; }
            public List<WorkerMatch> Matches { get; set; } = new List<WorkerMatch>( );
        }

        sealed class WorkerMatch
        {
            [JsonPropertyName( "i" )]
            public int Index { get; set; }

            [JsonPropertyName( "l" )]
            public int Length { get; set; }

            [JsonPropertyName( "g" )]
            public List<WorkerGroup> groups { get; set; } = new List<WorkerGroup>( );
        }

        sealed class WorkerGroup
        {
            [JsonPropertyName( "i" )]
            public int Index { get; set; }

            [JsonPropertyName( "l" )]
            public int Length { get; set; }

            [JsonPropertyName( "c" )]
            public List<WorkerCapture> Captures { get; set; } = new List<WorkerCapture>( );
        }

        sealed class WorkerCapture
        {
            [JsonPropertyName( "i" )]
            public int Index { get; set; }

            [JsonPropertyName( "l" )]
            public int Length { get; set; }
        }


        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string );

                switch( input_args.cmd )
                {
                case "v":
                    GetVersion( );
                    break;
                case "t":
                    GetTextBackTest( input_args );
                    break;
                case "m":
                    GetMatches( input_args );
                    break;
                default:
                    throw new ArgumentException( $"Command not supported: '{input_args.cmd}'" );
                }
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
                Match m = Regex.Match( targetFrameworkAttribute.FrameworkName, @"Version\s*=\s*v?(?<version>\d+(\.\d+)?)", RegexOptions.IgnoreCase | RegexOptions.ExplicitCapture );

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

        private static void GetTextBackTest( InputArgs inputArgs )
        {
            Console.Out.WriteLine( inputArgs.text );
        }

        private static void GetMatches( InputArgs inputArgs )
        {
            RegexOptions options = ConvertOptions( inputArgs.options );

            Regex re = inputArgs.options?.Timeout == null ?
                new Regex( inputArgs.pattern, options ) :
                new Regex( inputArgs.pattern, options, TimeSpan.FromMilliseconds( inputArgs.options.Timeout.Value ) );

            var worker_matches = new WorkerMatches( );

            worker_matches.Names = re.GetGroupNames( );

            foreach( Match m in re.Matches( inputArgs.text ) )
            {
                Debug.Assert( m.Success );

                var worker_match = new WorkerMatch { Index = m.Index, Length = m.Length };

                foreach( Group g in m.Groups )
                {
                    WorkerGroup worker_group;

                    if( !g.Success )
                    {
                        worker_group = new WorkerGroup { Index = -1, Length = -1 };
                    }
                    else
                    {
                        worker_group = new WorkerGroup { Index = g.Index, Length = g.Length };

                        foreach( Capture c in g.Captures )
                        {
                            worker_group.Captures.Add( new WorkerCapture { Index = c.Index, Length = c.Length } );
                        }
                    }

                    worker_match.groups.Add( worker_group );
                }

                worker_matches.Matches.Add( worker_match );
            }

            string ret_json = JsonSerializer.Serialize( worker_matches );

            Console.Out.WriteLine( ret_json );
        }

        private static RegexOptions ConvertOptions( Options options )
        {
            RegexOptions o = RegexOptions.None;

            if( options != null )
            {
                if( options.IgnoreCase ) o |= RegexOptions.IgnoreCase;
                if( options.Multiline ) o |= RegexOptions.Multiline;
                if( options.ExplicitCapture ) o |= RegexOptions.ExplicitCapture;
                if( options.Compiled ) o |= RegexOptions.Compiled;
                if( options.Singleline ) o |= RegexOptions.Singleline;
                if( options.IgnorePatternWhitespace ) o |= RegexOptions.IgnorePatternWhitespace;
                if( options.RightToLeft ) o |= RegexOptions.RightToLeft;
                if( options.ECMAScript ) o |= RegexOptions.ECMAScript;
                if( options.CultureInvariant ) o |= RegexOptions.CultureInvariant;
            }

            return o;
        }
    }
}
