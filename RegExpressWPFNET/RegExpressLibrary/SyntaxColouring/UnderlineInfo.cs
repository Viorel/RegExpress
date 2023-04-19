using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


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
