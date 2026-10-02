using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

abstract class Indicator
{
    public CategoryObj CategoryObj { get; }
    public string ShortDesc { get; }
    public string Desc { get; }

    protected Indicator( CategoryObj categoryObj, string shortDesc, string desc )
    {
        CategoryObj = categoryObj;
        ShortDesc = shortDesc;
        Desc = desc;
    }

    public abstract IndicatorData? Exec( bool validate, RegexEngine engine, FeatureMatrix fm );
}
