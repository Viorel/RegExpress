using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary
{
    public sealed class RegexEngineOptionsChangedArgs : EventArgs
    {
        public bool PreferImmediateReaction;
    }
}
