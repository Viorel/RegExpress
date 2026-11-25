using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

abstract class BoolStep
{
    public abstract bool? Exec( ExecContext context );
}
