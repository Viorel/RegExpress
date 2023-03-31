using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.SyntaxColouring
{
    public class ColouredSegments
    {
        public List<Segment> Comments { get; } = new List<Segment>( );
        public List<Segment> CharacterClass { get; } = new List<Segment>( );
        public List<Segment> CharacterEscapes { get; } = new List<Segment>( );
        public List<Segment> Escapes { get; } = new List<Segment>( );
        public List<Segment> GroupNames { get; } = new List<Segment>( );
        public List<Segment> QuotedSequences { get; } = new List<Segment>( );
        public List<Segment> Anchors { get; } = new List<Segment>( );
        public List<Segment> Quantifiers { get; } = new List<Segment>( );
        public List<Segment> Symbols { get; } = new List<Segment>( );
        public List<Segment> Brackets { get; } = new List<Segment>( );

        public IEnumerable<List<Segment>> All { get; }


        public ColouredSegments( )
        {
            All = new List<List<Segment>> { Comments, CharacterClass, CharacterEscapes, Escapes, GroupNames, QuotedSequences, Anchors, Quantifiers, Symbols, Brackets };
        }
    }
}
