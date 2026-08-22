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
            public char? LineTerminator { get; set; }
            public bool? Utf8 { get; set; }
            public bool? UnicodeClasses { get; set; }
            //?public bool? MatchInvalidUtf8 { get; set; }

            public string? EngineMode { get; set; }
            public ulong? DfaSizeLimit { get; set; }
        }

        //

        public sealed class OutputMatch
        {
            public int[][]? g { get; set; }
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

                    if( input_args.options.LineTerminator != null ) options.LineTerminator = Convert.ToByte( input_args.options.LineTerminator.Value );
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

                UTF8Encoding utf8_validating_encoding = new( encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true );
                int start_at = 0;

                List<OutputMatch> matches = [];

                for(; ; )
                {
                    Scout.Text.Regex.ByteRegexCaptures? c = re.FindCaptures( text_bytes, start_at );

                    if( c == null ) break;

                    List<int[]> groups = [];

                    for( var i = 0; i < c.GroupCount; i++ )
                    {
                        Scout.Text.Regex.ByteRegexMatch? g = c.GetGroup( i );

                        if( g == null )
                        {
                            groups.Add( [-1, -1] );
                        }
                        else
                        {
                            groups.Add( [g.Value.Start, g.Value.End] );
                        }
                    }

                    OutputMatch one_match = new( ) { g = groups.ToArray( ) };

                    matches.Add( one_match );

                    int new_start = c.Match.End;

                    if( start_at < new_start )
                    {
                        start_at = new_start;
                    }
                    else
                    {
                        Debug.Assert( start_at == new_start );

                        for( int utf8_len = 1; utf8_len <= 4; ++utf8_len )
                        {
                            try
                            {
                                utf8_validating_encoding.GetCharCount( text_bytes, start_at, utf8_len );
                                new_start = start_at + utf8_len;

                                break;
                            }
                            catch
                            {
                                // ignore
                            }
                        }

                        if( new_start <= start_at )
                        {
                            //
                            break;
                        }

                        start_at = new_start;
                    }
                }

                string ret_json = JsonSerializer.Serialize( new OutputMatches { matches = matches.ToArray( ) } );

                Console.Out.WriteLine( ret_json );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( exc.Message );
            }
        }
    }
}
