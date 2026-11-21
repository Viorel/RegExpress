using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json.Serialization;

namespace JavaScriptPlugin;

internal class OutputMatch
{
    [JsonPropertyName( "i" )]
    public required int[]?[] UnnamedGroups { get; set; }

    [JsonPropertyName( "g" )]
    public IReadOnlyDictionary<string, int[]>? NamedGroups { get; set; }
}

internal static class CommonUtilities
{
    public static RegexMatches ProcessResponse( ICancellable cnc, string text, OutputMatch[]? outputMatches )
    {
        List<IMatch> matches = [];

        if( outputMatches != null )
        {
            SimpleTextGetter stg = new( text );
            IdentityIndexConverter index_converter = IdentityIndexConverter.Instance;

            foreach( var native_match in outputMatches )
            {
                if( cnc.IsCancellationRequested ) break;

                SimpleMatch match;

                {
                    // match and default group

                    int[] g = native_match.UnnamedGroups[0]!;
                    Debug.Assert( g != null && g.Length == 2 );

                    int native_start = g[0];
                    int native_end = g[1];
                    Debug.Assert( native_start >= 0 && native_end >= native_start );

                    int native_length = native_end - native_start;

                    (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                    match = SimpleMatch.Create( native_start, native_length, char_start, char_length, stg );
                    match.AddDefaultGroup( );
                }

                {
                    // other groups (including named groups)

                    for( int group_index = 1; group_index < native_match.UnnamedGroups.Length; group_index++ )
                    {
                        string name = group_index.ToString( CultureInfo.InvariantCulture );

                        int[]? g = native_match.UnnamedGroups[group_index];
                        Debug.Assert( g == null || g.Length == 0 || g.Length == 2 );

                        if( g == null || g.Length == 0 || g[0] < 0 || g[1] < 0 )
                        {
                            match.AddFailedGroup( name );
                        }
                        else
                        {
                            int native_start = g[0];
                            int native_end = g[1];
                            Debug.Assert( native_start >= 0 && native_end >= native_start );

                            int native_length = native_end - native_start;

                            (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                            match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                        }
                    }
                }

                {
                    // named groups

                    if( native_match.NamedGroups != null )
                    {
                        foreach( var kvp in native_match.NamedGroups )
                        {
                            string name = kvp.Key;
                            int[]? g = kvp.Value;
                            Debug.Assert( g == null || g.Length == 0 || g.Length == 2 );

                            if( g == null || g.Length == 0 || g[0] < 0 || g[1] < 0 ) // (failed named groups are not actually included)
                            {
                                match.AddFailedGroup( name );
                            }
                            else
                            {
                                int native_start = g[0];
                                int native_end = g[1];
                                Debug.Assert( native_start >= 0 && native_end >= native_start );

                                int native_length = native_end - native_start;

                                (int char_start, int char_length) = index_converter.Convert( native_start, native_end );

                                match.AddSucceededGroup( native_start, native_length, char_start, char_length, name );
                            }
                        }
                    }

                    matches.Add( match );
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }
}
