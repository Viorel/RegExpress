namespace GoPlugin;

enum PackageEnum
{
    None,
    regexp,
    regexp2,
    rexa,
    coregex,
    onigmo,
}

class Options
{
    public PackageEnum Package { get; set; } = PackageEnum.regexp;

    public bool IgnoreCase { get; set; }
    public bool Multiline { get; set; }
    public bool ExplicitCapture { get; set; }
    public bool Singleline { get; set; }
    public bool IgnorePatternWhitespace { get; set; }
    public bool RightToLeft { get; set; }
    public bool ECMAScript { get; set; }
    public bool RE2 { get; set; }
    public bool Unicode { get; set; }

    public bool OptionDisableCharClassASCIIBitmap { get; set; }
    public string? OptionMaxBacktrackingStackSize { get; set; }
    public string? OptionMaxCachedRuneBufferLength { get; set; }

    public bool Ungreedy { get; set; }

    public bool posix { get; set; }
    public bool longest { get; set; }
    public bool literal { get; set; }

    //
    public bool EnableDFA { get; set; } = true;
    public bool EnablePrefilter { get; set; } = true;
    public bool EnableASCIIOptimization { get; set; } = true;

    public string? MaxDFAStates { get; set; }
    public string? DeterminizationLimit { get; set; }
    public string? MinLiteralLen { get; set; }
    public string? MaxLiterals { get; set; }
    public string? MaxRecursionDepth { get; set; }

    public bool FindAll { get; set; } = true;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
