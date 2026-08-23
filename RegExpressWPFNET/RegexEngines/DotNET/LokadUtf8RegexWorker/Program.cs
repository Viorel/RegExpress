using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace LokadUtf8RegexWorker
{
    internal class Program
    {
        sealed class InputArgs
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

            public long Timeout { get; set; }
        }

        public sealed class OutputMatch
        {
            public int[][]? g { get; set; } // UTF-16
        }

        public sealed class OutputMatches
        {
            public OutputMatch[]? matches { get; set; }
        }

        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

                string pattern = input_args.pattern;
                string text = input_args.text;

                System.Text.RegularExpressions.RegexOptions options = System.Text.RegularExpressions.RegexOptions.None;
                TimeSpan timeout = System.Text.RegularExpressions.Regex.InfiniteMatchTimeout;

                if( input_args.options != null )
                {
                    if( input_args.options.Compiled ) options |= System.Text.RegularExpressions.RegexOptions.Compiled;
                    if( input_args.options.CultureInvariant ) options |= System.Text.RegularExpressions.RegexOptions.CultureInvariant;
                    if( input_args.options.ECMAScript ) options |= System.Text.RegularExpressions.RegexOptions.ECMAScript;
                    if( input_args.options.ExplicitCapture ) options |= System.Text.RegularExpressions.RegexOptions.ExplicitCapture;
                    if( input_args.options.IgnoreCase ) options |= System.Text.RegularExpressions.RegexOptions.IgnoreCase;
                    if( input_args.options.IgnorePatternWhitespace ) options |= System.Text.RegularExpressions.RegexOptions.IgnorePatternWhitespace;
                    if( input_args.options.Multiline ) options |= System.Text.RegularExpressions.RegexOptions.Multiline;
                    if( input_args.options.NonBacktracking ) options |= System.Text.RegularExpressions.RegexOptions.NonBacktracking;
                    if( input_args.options.RightToLeft ) options |= System.Text.RegularExpressions.RegexOptions.RightToLeft;
                    if( input_args.options.Singleline ) options |= System.Text.RegularExpressions.RegexOptions.Singleline;

                    timeout = TimeSpan.FromMilliseconds( input_args.options.Timeout );
                }

                byte[] text_bytes = Encoding.UTF8.GetBytes( text );

                List<OutputMatch> output_matches = [];

                foreach( Lokad.Utf8Regex.Utf8ValueMatch m in Lokad.Utf8Regex.Utf8Regex.EnumerateMatches( text_bytes, pattern, options, timeout ) )
                {
                    Debug.Assert( m.Success );

                    OutputMatch output_match = new( ) { g = [[m.IndexInUtf16, m.LengthInUtf16]] }; // ('IndexInBytes', 'LengthInBytes' give error in case of surrogate pairs)

                    // Groups not available yet.

                    output_matches.Add( output_match );
                }

                string json = JsonSerializer.Serialize( new OutputMatches { matches = [.. output_matches] } );

                Console.Out.WriteLine( json );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( exc.Message );
            }
        }
    }
}
