namespace RegExpressLibrary.SyntaxColouring
{
    public sealed class Highlights
    {
        // (Positions in the text; empty if no highlights)

        public Segment LeftPar = Segment.Empty;
        public Segment RightPar = Segment.Empty;

        public Segment LeftBracket = Segment.Empty;
        public Segment RightBracket = Segment.Empty;
    }
}
