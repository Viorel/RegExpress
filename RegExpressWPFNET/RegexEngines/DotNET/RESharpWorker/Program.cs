using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace RESharpWorker
{
    internal class Program
    {
        sealed class InputArgs
        {
            public string? pattern { get; set; }
            public string? text { get; set; }
            public Options? options { get; set; }
        }

        sealed class Options
        {
            public bool IgnoreCase { get; set; }
            public bool UseDotnetUnicode { get; set; }
            public bool MinimizePattern { get; set; }
            public bool FindLookaroundPrefix { get; set; }

            public int? InitialDfaCapacity { get; set; }
            public int? MaxDfaCapacity { get; set; }
            public int? MaxPrefixLength { get; set; }
            public int? FindPotentialStartSizeLimit { get; set; }
            public int? StartsetInferenceLimit { get; set; }
            public int? DfaThreshold { get; set; }
        }

        sealed class OutputMatches
        {
            public required List<List<int>> Matches { get; init; }
        }

        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

                Resharp.ResharpOptions options = ConvertOptions( input_args.options );
                Resharp.Regex re = new( input_args.pattern ?? "", options );

                List<List<int>> output_matches = [];

                using var value_matches = re.ValueMatches( input_args.text ?? "" );

                foreach( Resharp.Common.ValueMatch m in value_matches )
                {
                    output_matches.Add( [m.Index, m.Length] );
                }

                string output_json = JsonSerializer.Serialize( new OutputMatches { Matches = [.. output_matches] } );

                Console.Out.WriteLine( output_json );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( $"{exc.GetType( ).Name}: {exc.Message}" );
            }
        }

        private static Resharp.ResharpOptions ConvertOptions( Options? options )
        {
            Resharp.ResharpOptions o = new( );

            if( options != null )
            {
                o.IgnoreCase = options.IgnoreCase;
                o.UseDotnetUnicode = options.UseDotnetUnicode;
                o.MinimizePattern = options.MinimizePattern;
                o.FindLookaroundPrefix = options.FindLookaroundPrefix;

                if( options.InitialDfaCapacity != null ) o.InitialDfaCapacity = options.InitialDfaCapacity.Value;
                if( options.MaxDfaCapacity != null ) o.MaxDfaCapacity = options.MaxDfaCapacity.Value;
                if( options.MaxPrefixLength != null ) o.MaxPrefixLength = options.MaxPrefixLength.Value;
                if( options.FindPotentialStartSizeLimit != null ) o.FindPotentialStartSizeLimit = options.FindPotentialStartSizeLimit.Value;
                if( options.StartsetInferenceLimit != null ) o.StartsetInferenceLimit = options.StartsetInferenceLimit.Value;
                if( options.DfaThreshold != null ) o.DfaThreshold = options.DfaThreshold.Value;
            }

            return o;
        }
    }
}
