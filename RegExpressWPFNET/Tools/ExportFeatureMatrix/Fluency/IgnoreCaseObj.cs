using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class IgnoreCaseObj : BoolStep
{
    readonly bool yes;

    public IgnoreCaseObj( bool yes )
    {
        this.yes = yes;
    }

    public override bool Exec( RegexEngine engine, FeatureMatrix fm, ExecContext execContext )
    {
        engine.SetIgnoreCase( yes );

        return false;
    }
}
