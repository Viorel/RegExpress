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

    public override bool? Exec( ExecContext context )
    {
        context.Engine.SetIgnoreCase( yes );

        return null;
    }
}
