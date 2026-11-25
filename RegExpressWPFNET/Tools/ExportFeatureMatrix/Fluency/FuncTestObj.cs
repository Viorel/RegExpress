using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class FuncTestObj : BoolStep
{
    readonly Func<ExecContext, bool> func;

    public FuncTestObj( Func<ExecContext, bool> func )
    {
        this.func = func;
    }

    public override bool? Exec( ExecContext context )
    {
        return func( context );
    }
}