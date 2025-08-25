using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace JavaScriptPlugin
{
    enum RuntimeEnum
    {
        None,
        WebView2,
        NodeJs,
        QuickJs,
        SpiderMonkey,
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
        public FunctionEnum Function { get; set; } = FunctionEnum.Exec;

        public bool i { get; set; }
        public bool m { get; set; }
        public bool s { get; set; }
        public bool u { get; set; }
        public bool y { get; set; }
        public bool g { get; set; } = true;

        // V8 (WebView2, NodeJs)
        public bool v { get; set; }

        // SpiderMonkey
        public bool NoNativeRegexp { get; set; } // "--no-native-regexp"
        public bool EnableDuplicateNames { get; set; } // "--enable-regexp-duplicate-named-groups"
        public bool EnableRegexpModifiers { get; set; } // "--enable-regexp-modifiers"

        [JsonIgnore]
        public bool d { get; set; } = true; // to avoid binding errors and to show the checkbox in checked state


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
