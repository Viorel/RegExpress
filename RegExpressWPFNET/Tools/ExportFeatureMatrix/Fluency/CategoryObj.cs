using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class CategoryObj
{
    readonly List<Indicator> indicators = [];

    public Tree Tree { get; }
    public string Name { get; }
    public IReadOnlyList<Indicator> Indicators => indicators;

    public CategoryObj( Tree tree, string name )
    {
        Tree = tree;
        Name = name;
    }

    public FlagObj Flag( string shortDesc, string desc, Func<RegexEngine, FeatureMatrix, bool> flagGetter, bool isInfo = false )
    {
        FlagObj flag = new( this, shortDesc, desc, flagGetter, isInfo );
        indicators.Add( flag );

        return flag;
    }

    public DirectObj Direct( string shortDesc, string desc, Func<bool, RegexEngine, FeatureMatrix, IndicatorData?> func )
    {
        DirectObj direct = new( this, shortDesc, desc, func );
        indicators.Add( direct );

        return direct;
    }

    public CategoryObj Category( string name )
    {
        return Tree.Category( name );
    }
}
