using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace ScoutWorker
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
            public bool? AsciiCaseInsensitive { get; set; }
            public bool? MultiLine { get; set; }
            public bool? DotMatchesNewline { get; set; }
            public bool? Crlf { get; set; }
            public byte? LineTerminator { get; set; }
            public bool? Utf8 { get; set; }
            public bool? UnicodeClasses { get; set; }
            //?public bool? MatchInvalidUtf8 { get; set; } // Not supported here

            public string? EngineMode { get; set; }
            public ulong? DfaSizeLimit { get; set; }
        }

        //

        sealed class OutputMatches
        {
            public required OutputMatch[] Matches { get; init; }
        }

        sealed class OutputMatch
        {
            [JsonPropertyName( "g" )]
            public required int[][] Groups { get; set; }
        }


        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

                byte[] pattern_bytes = Encoding.UTF8.GetBytes( input_args.pattern! );
                byte[] text_bytes = Encoding.UTF8.GetBytes( input_args.text! );
                Scout.Text.Regex.ByteRegexOptions options = new( );

                if( input_args.options != null )
                {
                    if( input_args.options.AsciiCaseInsensitive != null ) options.AsciiCaseInsensitive = input_args.options.AsciiCaseInsensitive.Value;
                    if( input_args.options.MultiLine != null ) options.MultiLine = input_args.options.MultiLine.Value;
                    if( input_args.options.DotMatchesNewline != null ) options.DotMatchesNewline = input_args.options.DotMatchesNewline.Value;
                    if( input_args.options.Crlf != null ) options.Crlf = input_args.options.Crlf.Value;
                    if( input_args.options.Utf8 != null ) options.Utf8 = input_args.options.Utf8.Value;
                    if( input_args.options.UnicodeClasses != null ) options.UnicodeClasses = input_args.options.UnicodeClasses.Value;

                    if( input_args.options.LineTerminator != null ) options.LineTerminator = input_args.options.LineTerminator.Value;
                    if( input_args.options.DfaSizeLimit != null ) options.DfaSizeLimit = input_args.options.DfaSizeLimit.Value;

                    switch( input_args.options.EngineMode )
                    {
                    case null:
                    case "":
                    case "None":
                        break;
                    case "Optimized":
                        options.EngineMode = Scout.Text.Regex.ByteRegexEngineMode.Optimized;
                        break;
                    case "General":
                        options.EngineMode = Scout.Text.Regex.ByteRegexEngineMode.General;
                        break;
                    case "AutomataOnly":
                        options.EngineMode = Scout.Text.Regex.ByteRegexEngineMode.AutomataOnly;
                        break;
                    default:
                        throw new Exception( $"Invalid EngineMode: '{input_args.options.EngineMode}'" );
                    }
                }

                Scout.Text.Regex.ByteRegex re = Scout.Text.Regex.ByteRegex.Compile( pattern_bytes, options );

                List<OutputMatch> matches = [];

                foreach( Scout.Text.Regex.ByteRegexCaptures scout_match in re.EnumerateCaptures( text_bytes ) )
                {
                    List<int[]> groups = [];

                    for( int i = 0; i < scout_match.GroupCount; ++i )
                    {
                        Scout.Text.Regex.ByteRegexMatch? scout_group = scout_match.GetGroup( i );

                        if( scout_group == null )
                        {
                            groups.Add( [-1, -1] );
                        }
                        else
                        {
                            groups.Add( [scout_group.Value.Start, scout_group.Value.End] );
                        }
                    }

                    OutputMatch match = new( ) { Groups = [.. groups] };

                    matches.Add( match );
                }

                string ret_json = JsonSerializer.Serialize( new OutputMatches { Matches = [.. matches] } );

                Console.Out.WriteLine( ret_json );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( exc.Message );
            }
        }
    }
}
