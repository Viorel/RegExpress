namespace RegExpressLibrary.SyntaxColouring
{
    public sealed class SyntaxOptions
    {
        public bool Literal { get; init; } = false;
        public XLevelEnum XLevel { get; init; } = XLevelEnum.none;
        public FeatureMatrix FeatureMatrix { get; init; } = new FeatureMatrix( ); // (not used if 'Literal' is true)
    }
}
