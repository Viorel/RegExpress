using System.Collections.Generic;
using System.Linq;


namespace RegExpressLibrary.SyntaxColouring
{
    public sealed class UnderlineInfo
    {
        public IReadOnlyList<Segment> Segments { get; }

        public UnderlineInfo( IReadOnlyList<Segment> segments )
        {
            Segments = segments;
        }


        public static readonly UnderlineInfo Empty = new( Enumerable.Empty<Segment>( ).ToList( ) );
    }
}
