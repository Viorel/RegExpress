using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

internal class ExecContext
{
    public bool Validate { get; }
    public RegexEngine Engine { get; }
    public FeatureMatrix Fm { get; }

    public ExecContext( bool validate, RegexEngine engine, FeatureMatrix fm )
    {
        Engine = engine;
        Fm = fm;
        Validate = validate;
    }

}
