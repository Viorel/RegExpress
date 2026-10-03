using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class IgnorePatternWhitespaceObj : BoolStep
{
    private bool yes;

    public IgnorePatternWhitespaceObj( bool yes )
    {
        this.yes = yes;
    }

    public override bool Exec( RegexEngine engine, FeatureMatrix fm, ExecContext execContext )
    {
        engine.SetIgnorePatternWhitespace( yes );

        return false;
    }
}
