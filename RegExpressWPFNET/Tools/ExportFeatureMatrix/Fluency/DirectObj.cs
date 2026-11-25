using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class DirectObj : Indicator
{
    readonly Func<ExecContext, IndicatorData?> func;

    public DirectObj( CategoryObj categoryObj, string shortDesc, string desc, Func<ExecContext, IndicatorData?> func ) : base( categoryObj, shortDesc, desc )
    {
        this.func = func;
    }

    public override IndicatorData? Exec( ExecContext context )
    {
        return func( context );
    }
}
