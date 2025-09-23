using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary.SyntaxColouring;

public sealed class FeatureMatrixVariant
{
    public string? Name { get; private set; }
    public FeatureMatrix FeatureMatrix { get; private set; }
    public IRegexEngine RegexEngine { get; private set; }

    public FeatureMatrixVariant( string? name, FeatureMatrix featureMatrix, IRegexEngine regexEngine )
    {
        Name = name;
        FeatureMatrix = featureMatrix;
        RegexEngine = regexEngine;
    }
}
