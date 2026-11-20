using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;


namespace JavaScriptPlugin;

enum RuntimeEnum
{
    None,
    WebView2,
    NodeJs,
    QuickJs,
    SpiderMonkey,
    Bun,
    RE2JS,
    RegexPlus,
    Jint,
}

enum FunctionEnum
{
    None,
    MatchAll,
    Exec
}

class Options 
{
    public RuntimeEnum Runtime { get; set; } = RuntimeEnum.WebView2;
    public FunctionEnum Function { get; set; } = FunctionEnum.MatchAll;

    public bool i { get; set; }
    public bool m { get; set; }
    public bool s { get; set; }
    public bool u { get; set; }
    public bool y { get; set; }
    public bool g { get; set; } = true;
    public bool v { get; set; } = true; // V8 (WebView2, NodeJs), QuickJs

    // SpiderMonkey

    public bool NoNativeRegexp { get; set; } // "--no-native-regexp"
    public bool EnableDuplicateNames { get; set; } // "--enable-regexp-duplicate-named-groups"
    public bool EnableRegexpModifiers { get; set; } // "--enable-regexp-modifiers"

    // RE2JS

    public bool DISABLE_UNICODE_GROUPS { get; set; }
    public bool LONGEST_MATCH { get; set; }
    public bool LOOKBEHINDS { get; set; }

    // RegexpPlus

    public bool x { get; set; } = true;
    public bool n { get; set; } = true;
    public bool subclass { get; set; }

    // Jint

    public string ? timeoutMs { get; set; }

    [JsonIgnore]
    public bool d { get; set; } = true; // to avoid binding errors and to show the checkbox in checked state


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
