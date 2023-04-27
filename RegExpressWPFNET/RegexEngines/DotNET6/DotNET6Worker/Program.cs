using System.Diagnostics;
using System.Reflection;
using System.Runtime.Versioning;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text;


namespace DotNET6Worker
{
    internal class Program
    {
        class InputArgs
        {
            public string cmd { get; set; }
            public string pattern { get; set; }
            public string text { get; set; }
            public Options options { get; set; }
        }


        class Options
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

            public long TimeoutMs { get; set; } = 10_000;
        }


        class WorkerMatch
        {
            public int index { get; set; }
            public int length { get; set; }
            public List<WorkerGroup> groups { get; set; } = new List<WorkerGroup>( );
        }


        class WorkerGroup
        {
            public bool success { get; set; }
            public int index { get; set; }
            public int length { get; set; }
            public string name { get; set; }
            public List<WorkerCapture> captures { get; set; } = new List<WorkerCapture>( );
        }


        class WorkerCapture
        {
            public int index { get; set; }
            public int length { get; set; }
        }


        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

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
                Console.Error.WriteLine( exc.Message );
            }
        }


        private static void GetVersion( )
        {
            TargetFrameworkAttribute? targetFrameworkAttribute = Assembly
                .GetExecutingAssembly( )
                .GetCustomAttributes<TargetFrameworkAttribute>( )
                .SingleOrDefault( );

            string? version = null;

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
                version += " (" + new Version( Environment.Version.Major, Environment.Version.Minor, Environment.Version.Build ) + ")";
            }

            var response = new { version };
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
            TimeSpan timeout = inputArgs.options == null ? Regex.InfiniteMatchTimeout : TimeSpan.FromMilliseconds( inputArgs.options.TimeoutMs );

            var re = new Regex( inputArgs.pattern, options, timeout );
            var worker_matches = new List<WorkerMatch>( );

            foreach( Match m in re.Matches( inputArgs.text ) )
            {
                Debug.Assert( m.Success );

                var worker_match = new WorkerMatch { index = m.Index, length = m.Length };

                foreach( Group g in m.Groups )
                {
                    var worker_group = new WorkerGroup { success = g.Success, index = g.Index, length = g.Length, name = g.Name };

                    foreach( Capture c in g.Captures )
                    {
                        worker_group.captures.Add( new WorkerCapture { index = c.Index, length = c.Length } );
                    }

                    worker_match.groups.Add( worker_group );
                }

                worker_matches.Add( worker_match );
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
                if( options.CultureInvariant ) o |= RegexOptions.CultureInvariant;
            }

            return o;
        }
    }
}