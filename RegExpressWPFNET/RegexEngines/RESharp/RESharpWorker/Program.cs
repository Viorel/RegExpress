using System.Diagnostics;
using System.Reflection;
using System.Runtime.Versioning;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text;
using Resharp;

namespace RESharpWorker
{
    internal class Program
    {
        sealed class InputArgs
        {
            public string? cmd { get; set; }
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

        }

        sealed class WorkerMatch
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

        private static void GetMatches( InputArgs inputArgs )
        {
            Resharp.ResharpOptions options = ConvertOptions( inputArgs.options );

            var re = new Resharp.Regex( inputArgs.pattern ?? "", options );
            var worker_matches = new List<WorkerMatch>( );

            using var value_matches = re.ValueMatches( inputArgs.text ?? "" );

            foreach( Common.ValueMatch m in value_matches )
            {
                var worker_match = new WorkerMatch { index = m.Index, length = m.Length };

                worker_matches.Add( worker_match );
            }

            string ret_json = JsonSerializer.Serialize( worker_matches );

            Console.Out.WriteLine( ret_json );
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
            }

            return o;
        }
    }
}
