using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;


namespace DotNET10Worker;

internal class Program
{
    sealed class Input
    {
        public required string pattern { get; set; }
        public required string text { get; set; }
        public Options? options { get; set; }
    }

    sealed class Options
    {
        public bool Compiled { get; set; }
        public bool CultureInvariant { get; set; }
        public bool ECMAScript { get; set; }
        public bool ExplicitCapture { get; set; }
        public bool IgnoreCase { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool Multiline { get; set; }
        public bool NonBacktracking { get; set; }
        public bool RightToLeft { get; set; }
        public bool Singleline { get; set; }

        public long? Timeout { get; set; }
    }

    public sealed class Output
    {
        public required string[] Names { get; init; }
        public required List<List<List<int>>> Matches { get; init; }
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
                        string version = new Version( Environment.Version.Major, Environment.Version.Minor, Environment.Version.Build ).ToString( ); // not interested in revision
                        var response = new { Version = version };
                        var response_string = JsonSerializer.Serialize( response );

                        Console.Out.WriteLine( response_string );

                        return;
                    }
                }

                throw new NotSupportedException( "Incorrect arguments" );
            }

            string input_string = Console.In.ReadToEnd( );

            Input input = JsonSerializer.Deserialize<Input>( input_string )!;

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
                if( o.NonBacktracking ) options |= RegexOptions.NonBacktracking;
            }

            Regex regex = input.options?.Timeout == null ?
                new( input.pattern, options ) :
                new( input.pattern, options, TimeSpan.FromMilliseconds( input.options.Timeout.Value ) );

            List<List<List<int>>> matches = [];

            foreach( Match match in regex.Matches( input.text ) )
            {
                Debug.Assert( match.Success );
                if( !match.Success ) break;

                List<List<int>> groups = [[match.Index, match.Length]];

                foreach( Group group in match.Groups.Cast<Group>( ).Skip( 1 ) ) // skip default group, already added
                {
                    List<int> captures = [];

                    if( !group.Success )
                    {
                        // nothing added
                    }
                    else
                    {
                        captures.Add( group.Index );
                        captures.Add( group.Length );

                        foreach( Capture capture in group.Captures )
                        {
                            captures.Add( capture.Index );
                            captures.Add( capture.Length );
                        }
                    }

                    groups.Add( captures );
                }

                matches.Add( groups );
            }

            Output output = new( )
            {
                Names = regex.GetGroupNames( ),
                Matches = matches
            };
            string json = JsonSerializer.Serialize( output );

            Console.Out.WriteLine( json );
        }
        catch( Exception exc )
        {
            Console.Error.WriteLine( $"{exc.GetType( ).Name}: {exc.Message}" );
        }
    }
}
