using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace GregsExWorker;

internal class Program
{

    sealed class Input
    {
        public required string pattern { get; set; }
        public required string text { get; set; }
        public InputOptions? options { get; set; }
    }

    sealed class InputOptions
    {
        public bool HasIndices { get; set; }
        public bool Global { get; set; }
        public bool IgnoreCase { get; set; }
        public bool Multiline { get; set; }
        public bool DotAll { get; set; }
        public bool Unicode { get; set; }
        public bool UnicodeSets { get; set; }
        public bool Sticky { get; set; }
    }

    sealed class Output
    {
        public required OutputMatch[] matches { get; set; }
    }

    sealed class OutputMatch
    {
        public required int[][] g { get; set; }
    }


    static void Main( string[] args )
    {
        try
        {
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;

            string input_string = Console.In.ReadToEnd( );

            Input input = JsonSerializer.Deserialize<Input>( input_string )!;

            GregsEx.EcmaScriptFlags input_flags = GregsEx.EcmaScriptFlags.None;
            if( input.options != null )
            {
                if( input.options.HasIndices ) input_flags |= GregsEx.EcmaScriptFlags.HasIndices;
                if( input.options.Global ) input_flags |= GregsEx.EcmaScriptFlags.Global;
                if( input.options.IgnoreCase ) input_flags |= GregsEx.EcmaScriptFlags.IgnoreCase;
                if( input.options.Multiline ) input_flags |= GregsEx.EcmaScriptFlags.Multiline;
                if( input.options.DotAll ) input_flags |= GregsEx.EcmaScriptFlags.DotAll;
                if( input.options.Unicode ) input_flags |= GregsEx.EcmaScriptFlags.Unicode;
                if( input.options.UnicodeSets ) input_flags |= GregsEx.EcmaScriptFlags.UnicodeSets;
                if( input.options.Sticky ) input_flags |= GregsEx.EcmaScriptFlags.Sticky;
            }

            GregsEx.RegexProfile profile = new GregsEx.EcmaScriptProfile( input_flags );

            GregsEx.RegexCompileOptions? compile_options = null;
            GregsEx.RegexMatchOptions match_options = default;

            GregsEx.RegexPattern pattern = GregsEx.RegexPattern.Parse( input.pattern, profile, compile_options );

            using GregsEx.EcmaScriptMatcher matcher = GregsEx.EcmaScriptMatcher.Bind( pattern );

            GregsEx.RegexCapture[] groups = new GregsEx.RegexCapture[pattern.GroupCount];
            GregsEx.RegexMatchRange range;

            List<OutputMatch> output_matches = [];

            for(; ; )
            {
                GregsEx.RegexSearchStatus status = matcher.ExecNext( input.text, groups, out range, match_options );

                if( status == GregsEx.RegexSearchStatus.NoMatch ) break;

                switch( status )
                {
                case GregsEx.RegexSearchStatus.Match:
                {
                    List<int[]> output_groups = [];

                    for( int i = 0; i < pattern.GroupCount; ++i ) // including main group
                    {
                        var g = groups[i];

                        if( !g.Participated )
                        {
                            output_groups.Add( [-1, -1] );
                        }
                        else
                        {
                            output_groups.Add( [g.Index, g.Length] );
                        }
                    }

                    OutputMatch output_match = new( ) { g = [.. output_groups] };

                    output_matches.Add( output_match );
                }
                break;
                case GregsEx.RegexSearchStatus.InvalidInput:
                case GregsEx.RegexSearchStatus.DestinationTooSmall:
                case GregsEx.RegexSearchStatus.LimitExceeded:
                case GregsEx.RegexSearchStatus.Cancelled:
                case GregsEx.RegexSearchStatus.TimedOut:
                    throw new Exception( $"GregsEx status: {status}" );
                default:
                    throw new Exception( $"Unknown GregsEx status: {status}" );
                }
            }

            string output_json = JsonSerializer.Serialize( new Output { matches = [.. output_matches] } );

            Console.Out.WriteLine( output_json );
        }
        catch( Exception exc )
        {
            Console.Error.WriteLine( exc.Message );
        }
    }
}
