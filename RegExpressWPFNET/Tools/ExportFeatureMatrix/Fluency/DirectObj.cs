using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class DirectObj : Indicator
{
    readonly Func<bool, RegexEngine, FeatureMatrix, IndicatorData?> func;

    public DirectObj( CategoryObj categoryObj, string shortDesc, string desc, Func<bool, RegexEngine, FeatureMatrix, IndicatorData?> func ) : base( categoryObj, shortDesc, desc )
    {
        this.func = func;
    }

    public override IndicatorData? Exec( bool validate, RegexEngine engine, FeatureMatrix fm )
    {
        return func( validate, engine, fm );
    }
}
