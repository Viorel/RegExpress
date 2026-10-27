using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace GregsExWorker;

internal class Program
{
    sealed class BasicInput
    {
        public string? profile { get; set; }
    }

    sealed class EcmaScriptInput
    {
        public required string pattern { get; set; }
        public required string text { get; set; }
        public EcmaScriptInputOptions? options { get; set; }
    }

    sealed class EcmaScriptInputOptions
    {
        public string? UnicodeVersion { get; set; }
        public bool HasIndices { get; set; }
        public bool Global { get; set; }
        public bool IgnoreCase { get; set; }
        public bool Multiline { get; set; }
        public bool DotAll { get; set; }
        public bool Unicode { get; set; }
        public bool UnicodeSets { get; set; }
        public bool Sticky { get; set; }
    }

    sealed class XsdInput
    {
        public required string pattern { get; set; }
        public required string text { get; set; }
        public XsdInputOptions? options { get; set; }
    }

    sealed class XsdInputOptions
    {
        public string? UnicodeVersion { get; set; }
    }

    sealed class XPathInput
    {
        public required string pattern { get; set; }
        public required string text { get; set; }
        public XPathInputOptions? options { get; set; }
    }

    sealed class XPathInputOptions
    {
        public string? UnicodeVersion { get; set; }
        public bool IgnoreCase { get; set; }
        public bool Multiline { get; set; }
        public bool DotAll { get; set; }
        public bool IgnoreWhitespace { get; set; }
        public bool Quote { get; set; }
    }

    sealed class Output
    {
        public required OutputMatch[] Matches { get; set; }
    }

    sealed class OutputMatch
    {
        [JsonPropertyName( "g" )]
        public required int[][] Groups { get; set; }
    }

    static GregsEx.UnicodeVersion ParseUnicodeVersion( string? unicodeVersion )
    {
        return unicodeVersion switch
        {
            null or "" or "Unicode16_0" => GregsEx.UnicodeVersion.Unicode16_0,
            "Unicode17_0" => GregsEx.UnicodeVersion.Unicode17_0,
            _ => throw new NotSupportedException( $"UnicodeVersion not supported: '{unicodeVersion}'" )
        };
    }

    static void MatchEcmaScript( string inputString )
    {
        EcmaScriptInput input = JsonSerializer.Deserialize<EcmaScriptInput>( inputString )!;

        GregsEx.UnicodeVersion unicode_version = ParseUnicodeVersion( input.options?.UnicodeVersion );

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

        GregsEx.RegexProfile profile = new GregsEx.EcmaScriptProfile( input_flags, unicode_version );

        GregsEx.RegexCompileOptions? compile_options = null;
        GregsEx.RegexMatchOptions match_options = default;

        GregsEx.RegexPattern pattern = GregsEx.RegexPattern.Parse( input.pattern, profile, compile_options );

        // TODO: determine group names

        using GregsEx.EcmaScriptMatcher matcher = GregsEx.EcmaScriptMatcher.Bind( pattern );

        GregsEx.RegexCapture[] groups = new GregsEx.RegexCapture[pattern.GroupCount];
        List<OutputMatch> output_matches = [];

        for(; ; )
        {
            GregsEx.RegexSearchStatus status = matcher.ExecNext( input.text, groups, out GregsEx.RegexMatchRange range, match_options );

            if( status == GregsEx.RegexSearchStatus.NoMatch ) break;

            switch( status )
            {
            case GregsEx.RegexSearchStatus.Match:
            {
                List<int[]> output_groups = [];

                for( int i = 0; i < pattern.GroupCount; ++i ) // including main group
                {
                    GregsEx.RegexCapture g = groups[i];

                    if( !g.Participated )
                    {
                        output_groups.Add( [-1, -1] );
                    }
                    else
                    {
                        output_groups.Add( [g.Index, g.Length] );
                    }
                }

                OutputMatch output_match = new( ) { Groups = [.. output_groups] };

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

        string output_json = JsonSerializer.Serialize( new Output { Matches = [.. output_matches] } );

        Console.Out.WriteLine( output_json );
    }

    static void MatchXsd( string inputString )
    {
        XsdInput input = JsonSerializer.Deserialize<XsdInput>( inputString )!;

        GregsEx.UnicodeVersion unicode_version = ParseUnicodeVersion( input.options?.UnicodeVersion );

        GregsEx.RegexCompileOptions? compile_options = null;
        GregsEx.RegexWorkspaceOptions? workspace_options = null;
        GregsEx.RegexMatchOptions match_options = default;

        GregsEx.XsdProfile profile = new( unicode_version );
        GregsEx.RegexPattern pattern = GregsEx.RegexPattern.Parse( input.pattern, profile, compile_options );
        GregsEx.RegexWorkspace workspace = pattern.CreateWorkspace( workspace_options );

        GregsEx.RegexCapture[] groups = new GregsEx.RegexCapture[pattern.GroupCount];
        List<OutputMatch> output_matches = [];

        GregsEx.RegexSearchStatus status = workspace.FindCaptures( input.text, 0, groups, out GregsEx.RegexMatchRange _, match_options );

        if( status != GregsEx.RegexSearchStatus.NoMatch )
        {

            switch( status )
            {
            case GregsEx.RegexSearchStatus.Match:
            {
                List<int[]> output_groups = [];

                for( int i = 0; i < pattern.GroupCount; ++i ) // including main group
                {
                    GregsEx.RegexCapture g = groups[i];

                    if( !g.Participated )
                    {
                        output_groups.Add( [-1, -1] );
                    }
                    else
                    {
                        output_groups.Add( [g.Index, g.Length] );
                    }
                }

                OutputMatch output_match = new( ) { Groups = [.. output_groups] };

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

        string output_json = JsonSerializer.Serialize( new Output { Matches = [.. output_matches] } );

        Console.Out.WriteLine( output_json );
    }

    static void MatchXPath( string inputString )
    {
        XPathInput input = JsonSerializer.Deserialize<XPathInput>( inputString )!;

        GregsEx.UnicodeVersion unicode_version = ParseUnicodeVersion( input.options?.UnicodeVersion );

        GregsEx.RegexCompileOptions? compile_options = null;
        GregsEx.RegexWorkspaceOptions? workspace_options = null;
        GregsEx.RegexMatchOptions match_options = default;

        GregsEx.XPathFlags flags = GregsEx.XPathFlags.None;
        if( input.options != null )
        {
            if( input.options.DotAll ) flags |= GregsEx.XPathFlags.DotAll;
            if( input.options.Multiline ) flags |= GregsEx.XPathFlags.Multiline;
            if( input.options.IgnoreCase ) flags |= GregsEx.XPathFlags.IgnoreCase;
            if( input.options.IgnoreWhitespace ) flags |= GregsEx.XPathFlags.IgnoreWhitespace;
            if( input.options.Quote ) flags |= GregsEx.XPathFlags.Quote;
        }

        GregsEx.XPathProfile profile = new( flags, unicode_version );
        GregsEx.RegexPattern pattern = GregsEx.RegexPattern.Parse( input.pattern, profile, compile_options );
        GregsEx.RegexWorkspace workspace = pattern.CreateWorkspace( workspace_options );

        GregsEx.RegexCapture[] groups = new GregsEx.RegexCapture[pattern.GroupCount];
        List<OutputMatch> output_matches = [];

        for(; ; )
        {
            GregsEx.RegexSearchStatus status = workspace.FindCaptures( input.text, 0, groups, out GregsEx.RegexMatchRange _, match_options );

            if( status == GregsEx.RegexSearchStatus.NoMatch ) break;

            switch( status )
            {
            case GregsEx.RegexSearchStatus.Match:
            {
                List<int[]> output_groups = [];

                for( int i = 0; i < pattern.GroupCount; ++i ) // including main group
                {
                    GregsEx.RegexCapture g = groups[i];

                    if( !g.Participated )
                    {
                        output_groups.Add( [-1, -1] );
                    }
                    else
                    {
                        output_groups.Add( [g.Index, g.Length] );
                    }
                }

                OutputMatch output_match = new( ) { Groups = [.. output_groups] };

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

            break; // TODO: loop?
        }

        string output_json = JsonSerializer.Serialize( new Output { Matches = [.. output_matches] } );

        Console.Out.WriteLine( output_json );
    }

    static void Main( string[] args )
    {
        try
        {
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;

            string input_string = Console.In.ReadToEnd( );

            BasicInput input = JsonSerializer.Deserialize<BasicInput>( input_string )!;

            switch( input.profile )
            {
            case "EcmaScript":
                MatchEcmaScript( input_string );
                break;
            case "Xsd":
                MatchXsd( input_string );
                break;
            case "XPath":
                MatchXPath( input_string );
                break;
            case null:
            case "":
                throw new Exception( "No profile specified" );
            default:
                throw new NotSupportedException( $"Unsupported profile: '{input.profile}'" );
            }
        }
        catch( GregsEx.RegexParseException exc )
        {
            Console.Error.WriteLine( exc.Message );
            Console.Error.WriteLine( $"Offset: {exc.Diagnostic.Offset}" );
            if( !string.IsNullOrWhiteSpace( exc.Diagnostic.Message ) ) Console.Error.WriteLine( exc.Diagnostic.Message );
        }
        catch( GregsEx.RegexExecutionException exc )
        {
            Console.Error.WriteLine( exc.Message );
        }
        catch( Exception exc )
        {
            Console.Error.WriteLine( exc.Message );
        }
    }
}
