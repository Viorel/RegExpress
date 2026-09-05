namespace PythonPlugin;

enum ModuleEnum
{
    None,
    re,
    regex,
    real_regex,
}

class Options
{
    public ModuleEnum Module { get; set; } = ModuleEnum.re;

    // "re" and "real"
    public bool ASCII { get; set; }
    public bool DOTALL { get; set; }
    public bool IGNORECASE { get; set; }
    public bool LOCALE { get; set; } // not for "real"
    public bool MULTILINE { get; set; }
    public bool VERBOSE { get; set; }


    // For "regex":
    public bool BESTMATCH { get; set; }
    public bool ENHANCEMATCH { get; set; }
    public bool FULLCASE { get; set; }
    public bool POSIX { get; set; }
    public bool REVERSE { get; set; }
    public bool UNICODE { get; set; } // also for "real"
    public bool WORD { get; set; }
    public bool VERSION0 { get; set; }
    public bool VERSION1 { get; set; } = true;
    public bool overlapped { get; set; }
    public bool partial { get; set; }
    public string? timeout { get; set; } // seconds, double

    // "real"
    public bool fallback { get; set; }


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
