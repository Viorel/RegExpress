using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

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

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

                string pattern = input_args.pattern;
                string text = input_args.text;

                System.Text.RegularExpressions.RegexOptions options = System.Text.RegularExpressions.RegexOptions.None;

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
                }

                TimeSpan timeout = input_args.options?.Timeout == null ? System.Text.RegularExpressions.Regex.InfiniteMatchTimeout : TimeSpan.FromMilliseconds( input_args.options.Timeout.Value );

                byte[] text_bytes = Encoding.UTF8.GetBytes( text );

                List<List<List<int>>> output_matches = [];

                Lokad.Utf8Regex.Utf8Regex regex = new( pattern, options, timeout );

                for( int start = 0; start <= text.Length; )
                {
                    Lokad.Utf8Regex.Utf8MatchContext match = regex.MatchDetailedFromUtf16Offset( text_bytes, start );

                    if( !match.Success ) break;

                    // ('IndexInBytes', 'LengthInBytes' give error in case of surrogate pairs)

                    List<List<int>> groups = [];

                    for( int group_index = 0; group_index < match.GroupCount; ++group_index )
                    {
                        Lokad.Utf8Regex.Utf8GroupContext group = match.GetGroup( group_index );

                        List<int> captures = [];

                        for( int capture_index = 0; capture_index < group.CaptureCount; ++capture_index )
                        {
                            Lokad.Utf8Regex.Utf8CaptureContext capture = group.GetCapture( capture_index );

                            captures.Add( capture.IndexInUtf16 );
                            captures.Add( capture.LengthInUtf16 );
                        }

                        groups.Add( captures );
                    }

                    output_matches.Add( groups );

                    // next start

                    if( match.LengthInUtf16 == 0 ) // empty match
                    {
                        start = match.IndexInUtf16;

                        if( start < text.Length && char.IsHighSurrogate( text[start] ) )
                        {
                            start += 2;
                        }
                        else
                        {
                            ++start;
                        }
                    }
                    else
                    {
                        Debug.Assert( start < match.IndexInUtf16 + match.LengthInUtf16 );

                        start = match.IndexInUtf16 + match.LengthInUtf16;
                    }
                }

                string json = JsonSerializer.Serialize( new Output { Names = regex.GetGroupNames( ), Matches = [.. output_matches] } );

                Console.Out.WriteLine( json );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( $"{exc.GetType( ).Name}: {exc.Message}" );
            }
        }
    }
}
