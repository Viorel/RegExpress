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

    public override bool Exec( ExecContext context )
    {
        context.Engine.SetIgnorePatternWhitespace( yes );

        return false;
    }
}
