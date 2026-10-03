using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class FuncTestObj : BoolStep
{
    readonly Func<RegexEngine, FeatureMatrix, bool> func;

    public FuncTestObj( Func<RegexEngine, FeatureMatrix, bool> func )
    {
        this.func = func;
    }

    public override bool Exec( RegexEngine engine, FeatureMatrix fm, ExecContext execContext )
    {
        return func( engine, fm );
    }
}