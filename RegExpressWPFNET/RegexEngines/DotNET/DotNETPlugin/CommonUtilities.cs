using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;

namespace DotNETPlugin;

internal class Rootobject
{
    public required string[] Names { get; init; }
    public required int[][][] Matches { get; init; }
}

internal static class CommonUtilities
{
    public static RegexMatches ProcessResponse( ICancellable cnc, string text, Rootobject rootObject )
    {
        List<SimpleMatch> matches = [];
        SimpleTextGetter stg = new( text );
        IdentityIndexConverter index_converter = IdentityIndexConverter.Instance;

        foreach( int[][] m in rootObject.Matches )
        {
            if( cnc.IsCancellationRequested ) break;

            SimpleMatch match;

            {
                // match and default group

                int native_index = m[0][0];
                int native_length = m[0][1];

                Debug.Assert( native_index >= 0 && native_length >= 0 );

                match = SimpleMatch.Create( index_converter.ConvertIndexLength( native_index, native_length ), stg );
                match.AddDefaultGroup( );
            }

            {
                // other groups and captures

                for( int group_index = 1; group_index < m.Length; group_index++ )
                {
                    string? name = group_index >= rootObject.Names.Length ? null : rootObject.Names[group_index];
                    if( string.IsNullOrWhiteSpace( name ) ) name = group_index.ToString( CultureInfo.InvariantCulture );

                    int[] captures = m[group_index];

                    if( captures.Length == 0 )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        SimpleGroup group;

                        {
                            // group

                            int native_index = captures[0];
                            int native_length = captures[1];

                            Debug.Assert( native_index >= 0 && native_length >= 0 );

                            group = match.AddSucceededGroup( index_converter.ConvertIndexLength( native_index, native_length ), name );
                        }

                        {
                            // captures

                            for( int k = 2; k < captures.Length; k += 2 ) // skip group, already added
                            {
                                int native_index = captures[k];
                                int native_length = captures[k + 1];

                                Debug.Assert( native_index >= 0 && native_length >= 0 );

                                group.AddCapture( index_converter.ConvertIndexLength( native_index, native_length ) );
                            }
                        }
                    }
                }
            }

            matches.Add( match );
        }

        return new RegexMatches( matches.Count, matches );
    }
}
