using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HtmlAgilityPack;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using Universal.HtmlAgilityPack;


namespace HtmlAgilityPackPlugin
{
    static class Matcher
    {
        // Cache the FieldInfo for the private _outerlength field
        // HtmlAgilityPack's OuterHtml property returns reconstructed/normalized HTML which may differ
        // in length from the original text (e.g., due to CRLF normalization). The private _outerlength
        // field contains the actual length in the original source text.
        private static readonly FieldInfo? OuterLengthField = typeof( HtmlNode ).GetField( "_outerlength", BindingFlags.NonPublic | BindingFlags.Instance );

        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            if( string.IsNullOrWhiteSpace( pattern ) )
            {
                return RegexMatches.Empty;
            }

            if( string.IsNullOrEmpty( text ) )
            {
                return RegexMatches.Empty;
            }

            try
            {
                var doc = new HtmlDocument( );
                doc.LoadHtml( text );

                IEnumerable<HtmlNode> nodes;

                if( options.SelectorMode == SelectorMode.XPath )
                {
                    nodes = doc.DocumentNode.SelectNodes( pattern );
                }
                else // CssSelector
                {
                    // CSS selectors are case-insensitive for tag names in HTML
                    // HtmlAgilityPack normalizes tag names to lowercase, so convert selector to lowercase
                    nodes = doc.DocumentNode.QuerySelectorAll( pattern.ToLowerInvariant( ) );
                }

                if( nodes == null || !nodes.Any( ) )
                {
                    return RegexMatches.Empty;
                }

                if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

                var matches = new List<IMatch>( );
                var sourceTextGetter = new SimpleTextGetter( text );

                foreach( var node in nodes )
                {
                    if( cnc.IsCancellationRequested ) return RegexMatches.Empty;

                    // Get the position of the node in the original text (for highlighting)
                    int index = node.StreamPosition;

                    // Use the private _outerlength field to get the actual length in the original text.
                    // OuterHtml.Length can differ due to HTML normalization (e.g., CRLF → LF conversion).
                    int length;
                    if( OuterLengthField != null )
                    {
                        length = (int)( OuterLengthField.GetValue( node ) ?? node.OuterHtml.Length );
                    }
                    else
                    {
                        // Fallback if reflection fails (shouldn't happen, but be safe)
                        length = node.OuterHtml.Length;
                    }

                    // Validate and clamp the index/length to avoid out-of-bounds
                    if( index < 0 ) index = 0;
                    if( index > text.Length ) continue;
                    if( index + length > text.Length ) length = text.Length - index;
                    if( length <= 0 ) continue;

                    // Match is always the full element (OuterHtml)
                    var match = SimpleMatch.Create( index, length, sourceTextGetter );

                    // Add default group (group 0 - the full match) - this is skipped in display but needed for structure
                    match.AddGroup( index, length, true, "" );

                    // Add named "Value" group based on output mode
                    if( options.OutputMode == OutputMode.InnerHtml )
                    {
                        string innerHtml = node.InnerHtml;
                        // Use SimpleTextGetterWithOffset to return the innerHtml as the group's Value
                        // The index/length point to the element in source for highlighting
                        var valueTextGetter = new SimpleTextGetterWithOffset( index, innerHtml );
                        match.AddGroup( index, innerHtml.Length, true, "Value", valueTextGetter );
                    }
                    else if( options.OutputMode == OutputMode.InnerText )
                    {
                        string innerText = node.InnerText;
                        // Use SimpleTextGetterWithOffset to return the innerText as the group's Value
                        var valueTextGetter = new SimpleTextGetterWithOffset( index, innerText );
                        match.AddGroup( index, innerText.Length, true, "Value", valueTextGetter );
                    }

                    matches.Add( match );
                }

                return new RegexMatches( matches.Count, matches );
            }
            catch( Exception ex )
            {
                throw new Exception( $"Error processing selector: {ex.Message}", ex );
            }
        }

        public static string? GetVersion( )
        {
            try
            {
                var assembly = typeof( HtmlDocument ).Assembly;
                var version = assembly.GetName( ).Version;
                return version?.ToString( );
            }
            catch
            {
                return null;
            }
        }
    }
}
