using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CorvusEcma262Worker;

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

        public long? Timeout { get; set; }
    }

    sealed class OutputMatch
    {
        [JsonPropertyName("i")]
        public int index { get; set; }

        [JsonPropertyName( "l" )]
        public int length { get; set; }

        [JsonPropertyName( "g" )]
        public List<OutputGroup> groups { get; set; } = [];
    }

    sealed class OutputGroup
    {
        [JsonPropertyName( "s" )]
        public bool success { get; set; }
        
        [JsonPropertyName("i")]
        public int index { get; set; }
        
        [JsonPropertyName("l")]
        public int length { get; set; }
        
        [JsonPropertyName("n")]
        public string? name { get; set; }
        
        [JsonPropertyName("c")]
        public List<OutputCapture> captures { get; set; } = [];
    }

    sealed class OutputCapture
    {
        [JsonPropertyName( "i" )]
        public int index { get; set; }

        [JsonPropertyName( "l" )]
        public int length { get; set; }
    }

    static void Main( string[] args )
    {
        try
        {
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;

            string input_json = Console.In.ReadToEnd( );

            Input input = JsonSerializer.Deserialize<Input>( input_json )!;

            Corvus.Ecma262.RegularExpressions.RegexFlags flags = Corvus.Ecma262.RegularExpressions.RegexFlags.None;
            if( input.options != null )
            {
                if( input.options.HasIndices ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.HasIndices;
                if( input.options.Global ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.Global;
                if( input.options.IgnoreCase ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.IgnoreCase;
                if( input.options.Multiline ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.Multiline;
                if( input.options.DotAll ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.DotAll;
                if( input.options.Unicode ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.Unicode;
                if( input.options.UnicodeSets ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.UnicodeSets;
                if( input.options.Sticky ) flags |= Corvus.Ecma262.RegularExpressions.RegexFlags.Sticky;
            }

            Corvus.Ecma262.RegularExpressions.Regex re =
                input.options?.Timeout == null ?
                    new( input.pattern, flags ) :
                    new( input.pattern, flags, TimeSpan.FromMilliseconds( input.options.Timeout.Value ) );

            List<OutputMatch> output_matches = [];

            foreach( Corvus.Ecma262.RegularExpressions.Match match in re.Matches( input.text ) )
            {
                Debug.Assert( match.Success );

                OutputMatch output_match = new( ) { index = match.Index, length = match.Length };

                foreach( Corvus.Ecma262.RegularExpressions.Group group in match.Groups )
                {
                    OutputGroup output_group = new( ) { success = group.Success, index = group.Index, length = group.Length, name = group.Name };

                    foreach( Corvus.Ecma262.RegularExpressions.Capture capture in group.Captures )
                    {
                        output_group.captures.Add( new OutputCapture { index = capture.Index, length = capture.Length } );
                    }

                    output_match.groups.Add( output_group );
                }

                output_matches.Add( output_match );
            }

            string output_json = JsonSerializer.Serialize( output_matches );

            Console.Out.WriteLine( output_json );
        }
        catch( Exception exc )
        {
            Console.Error.WriteLine( exc.Message );
        }
    }
}
