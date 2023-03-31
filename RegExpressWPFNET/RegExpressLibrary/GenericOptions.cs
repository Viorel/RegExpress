using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary
{
    public struct GenericOptions
    // (Must not be changed to class)
    {
        public bool Literal;
        public XLevelEnum XLevel;
        public bool AllowEmptySets; // []
    }
}
