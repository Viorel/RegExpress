namespace RegExpressLibrary.SyntaxColouring;

public sealed class FeatureMatrixVariant
{
    public string? Name { get; private set; }
    public FeatureMatrix FeatureMatrix { get; private set; }
    public RegexEngine RegexEngine { get; private set; }

    public FeatureMatrixVariant( string? name, FeatureMatrix featureMatrix, RegexEngine regexEngine )
    {
        Name = name;
        FeatureMatrix = featureMatrix;
        RegexEngine = regexEngine;
    }

    public FeatureMatrixVariant( string? name, RegexEngine regexEngine )
        : this( name, regexEngine.GetSyntaxOptions( ).FeatureMatrix, regexEngine )
    {
    }
}
