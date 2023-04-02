using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace DotNETFrameworkConsole
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


        class RetMatch
        {
            public int index { get; set; }
            public int length { get; set; }
            public List<RetGroup> groups { get; set; } = new List<RetGroup>( );
        }


        class RetGroup
        {
            public bool success { get; set; }
            public int index { get; set; }
            public int length { get; set; }
            public string name { get; set; }
            public List<RetCapture> captures { get; set; } = new List<RetCapture>( );
        }


        class RetCapture
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
                Console.Error.WriteLine( exc.Message );
            }
        }


        private static void GetVersion( )
        {
            var response = new { version = Environment.Version };
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
            var ret_matches = new List<RetMatch>( );

            foreach( Match m in re.Matches( inputArgs.text ) )
            {
                Debug.Assert( m.Success );

                var ret_match = new RetMatch { index = m.Index, length = m.Length };

                foreach( Group g in m.Groups )
                {
                    var ret_group = new RetGroup { success = g.Success, index = g.Index, length = g.Length, name = g.Name };

                    foreach( Capture c in g.Captures )
                    {
                        ret_group.captures.Add( new RetCapture { index = c.Index, length = c.Length } );
                    }

                    ret_match.groups.Add( ret_group );
                }

                ret_matches.Add( ret_match );
            }

            string ret_json = JsonSerializer.Serialize( ret_matches );

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
