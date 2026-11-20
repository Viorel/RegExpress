using Jint;
using Jint.Runtime;
using System.Text;
using System.Text.Json;

namespace JintWorker;


sealed class Input
{
    public required string pattern { get; set; }
    public required string text { get; set; }
    public required string func { get; set; } // "exec" or "matchAll" 
    public string? flags { get; set; }
    public Int64? timeout { get; set; } // ms
}

internal class Program
{
    static void Main( string[] args )
    {
        try
        {
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;

            string input_string = Console.In.ReadToEnd( );

            Input input = JsonSerializer.Deserialize<Input>( input_string )!;

            string flags = input?.flags ?? "";
            if( !flags.Contains( 'd' ) ) flags += 'd';  // "d" -- generate indices ('match.indices')

            // NOTE: "g" flag is needed for 'matchAll'

            string script = input?.func switch
            {
                "exec" => """
                    function Script( pattern, text, flags)
                    {
                        const re = new RegExp( pattern, flags);
                        const matches = [ ]; 
                        let match; 
                        let lastIndex = -2;
                        while( ( match = re.exec( text)) !== null)
                        {
                            if( lastIndex == match.index ) 
                            {
                                if( flags.includes( "g")) throw new Error( "No advance");
                                break;
                            }
                            lastIndex = match.index;
                            matches.push( { i: match.indices, g: match.indices.groups });
                        } 
                
                        return JSON.stringify( { Matches: matches });
                    }
                    """,

                "matchAll" => """
                    function Script( pattern, text, flags)
                    {
                        const re = new RegExp( pattern, flags);
                        const matches = [ ];
                        let lastIndex = -2;
                        for( const match of text.matchAll( re))
                        {
                            if( lastIndex == match.index) throw new Error( "No advance");
                            lastIndex = match.index;
                            matches.push( { i: match.indices, g: match.indices.groups });
                        }
                
                        return JSON.stringify( { Matches: matches });
                    }
                    """,

                _ => throw new InvalidOperationException( $"Unknown function: '{input?.func}'" )
            };


            Options options = new( )
            {
                Strict = true
            };

            if( input.timeout != null ) options.Constraints.RegexTimeout = TimeSpan.FromMilliseconds( input.timeout.Value );

            Engine engine = new( options );

            ScriptParsingOptions script_parsing_options = ScriptParsingOptions.Default; // (timeout is not set and is got from 'options')

            engine.Execute( script, script_parsing_options );

            string result = engine.Invoke( "Script", input.pattern, input.text, flags ).AsString( );

            Console.Out.WriteLine( result );
        }
        //catch( JavaScriptException exc )
        //{
        //    // (nothing special found yet in 'JavaScriptException' and 'InnerException'

        //    Console.Error.WriteLine( $"{exc.GetType( ).Name}: {exc.Message}" );
        //}
        catch( Exception exc )
        {
            Console.Error.WriteLine( $"{exc.GetType( ).Name}: {exc.Message}" );

            //// to find issues of the script:
            //if( JintException.TryGetJavaScriptLocation( exc, out var location ) )
            //{
            //    Console.Error.WriteLine( $"Line: {location.Start.Line}, Column: {location.Start.Column}" );
            //}
        }
    }
}
