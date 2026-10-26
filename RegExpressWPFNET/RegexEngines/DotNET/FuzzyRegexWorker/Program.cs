using System.Text;
using System.Text.Json;

namespace FuzzyRegexWorker;

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
        public bool IgnoreCase { get; set; }
        public bool Multiline { get; set; }
        public bool Singleline { get; set; }
        public bool Unicode { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool Ascii { get; set; }
        public bool RightToLeft { get; set; }
        public bool Word { get; set; }
        public bool BestMatch { get; set; }
        public bool EnhanceMatch { get; set; }
        public bool Posix { get; set; }
        public bool FullCase { get; set; }
        public bool Version0 { get; set; }
        public bool Version1 { get; set; }
        public bool Overlapped { get; set; }
        public bool Partial { get; set; }

        public long? TimeoutMs { get; set; }
        public long? StepTimeoutMs { get; set; }
        public int? MaxCompiledNodes { get; set; }
    }

    sealed class Output
    {
        public required string[] Names { get; set; }
        public required int[][][] Matches { get; set; }
    }

    static void Main( string[] args )
    {
        try
        {
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;

            string input_string = Console.In.ReadToEnd( );

            Input input = JsonSerializer.Deserialize<Input>( input_string ) ?? throw new ArgumentException( "Null input" );

            TimeSpan timeout = input.options?.TimeoutMs == null ? Fuzzy.Text.RegularExpressions.FuzzyRegex.InfiniteMatchTimeout : TimeSpan.FromMilliseconds( input.options.TimeoutMs.Value );
            TimeSpan step_timeout = input.options?.StepTimeoutMs == null ? Fuzzy.Text.RegularExpressions.FuzzyRegex.InfiniteMatchTimeout : TimeSpan.FromMilliseconds( input.options.StepTimeoutMs.Value );
            int max_compiled_nodes = input.options?.MaxCompiledNodes ?? Fuzzy.Text.RegularExpressions.FuzzyRegex.DefaultMaxCompiledNodes;

            bool partial = false;
            bool overlapped = false;

            Fuzzy.Text.RegularExpressions.FuzzyRegexOptions options = Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.None;
            if( input.options != null )
            {
                if( input.options.IgnoreCase ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.IgnoreCase;
                if( input.options.Multiline ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Multiline;
                if( input.options.Singleline ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Singleline;
                if( input.options.Unicode ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Unicode;
                if( input.options.IgnorePatternWhitespace ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.IgnorePatternWhitespace;
                if( input.options.Ascii ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Ascii;
                if( input.options.RightToLeft ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.RightToLeft;
                if( input.options.Word ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Word;
                if( input.options.BestMatch ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.BestMatch;
                if( input.options.EnhanceMatch ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.EnhanceMatch;
                if( input.options.Posix ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Posix;
                if( input.options.FullCase ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.FullCase;
                if( input.options.Version0 ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Version0;
                if( input.options.Version1 ) options |= Fuzzy.Text.RegularExpressions.FuzzyRegexOptions.Version1;

                overlapped = input.options.Overlapped;
                partial = input.options.Partial;
            }

            Fuzzy.Text.RegularExpressions.FuzzyRegex re = new( input.pattern, options, timeout, maxCompiledNodes: max_compiled_nodes );

            List<int[][]> matches = [];
            List<int[]> match = [];
            List<int> group_and_captures = [];

            foreach( var m in re.EnumerateMatches( input.text, overlapped: overlapped, partial: partial, timeout: null ) )
            {
                match.Clear( );

                foreach( var g in m.Groups )
                {
                    group_and_captures.Clear( );

                    if( g.Success )
                    {
                        group_and_captures.Add( g.Index );
                        group_and_captures.Add( g.Length );

                        foreach( var c in g.Captures )
                        {
                            group_and_captures.Add( c.Index );
                            group_and_captures.Add( c.Length );
                        }
                    }

                    match.Add( [.. group_and_captures] );
                }

                matches.Add( [.. match] );
            }

            Output output = new( )
            {
                Names = [.. re.GroupNames],
                Matches = [.. matches]
            };
            string output_json = JsonSerializer.Serialize( output );

            Console.Out.WriteLine( output_json );

        }
        catch( Exception exc )
        {
            Console.Error.WriteLine( exc.Message );
        }
    }
}
