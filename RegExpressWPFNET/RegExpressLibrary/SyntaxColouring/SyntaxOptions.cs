namespace RegExpressLibrary.SyntaxColouring
{
    public sealed class SyntaxOptions
    {
        public bool Literal { get; init; }
        public XLevelEnum XLevel { get; init; }
        public FeatureMatrix FeatureMatrix { get; init; } // (not used if 'Literal' is true)
    }
}
