using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary;

// "engines.json" file

public sealed class EnginesData
{
    public EngineData[]? engines { get; set; }
}

public sealed class EngineData
{
    public string? path { get; set; }
    public bool no_fm { get; set; } // do not include this plugin to "Feature Matrix" exports
}

