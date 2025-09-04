using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.SyntaxColouring
{
    public sealed class SyntaxOptions
    {
        public bool Literal { get; init; }
        public XLevelEnum XLevel { get; init; }
        public bool AllowEmptySets { get; init; } // [], see also 'FeatureMatrix.EmptySets'

        public FeatureMatrix FeatureMatrix { get; init; } // (not used if 'Literal' is true)
    }
}
