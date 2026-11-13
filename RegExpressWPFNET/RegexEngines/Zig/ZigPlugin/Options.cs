namespace ZigPlugin;

enum RegexLibraryEnum
{
    None,
    ZigRegex,
    Mvzr,
    Pzre,
    EziGex,
    Zoptia0regex,
    Z_Regex, // "z-regex"
    ZRegex,
    Regex_Zig, // "regex.zig"
    Spectra,
}

enum CaseFoldEnum
{
    None,
    none,
    simple,
    full,
}

enum ByteEngineEnum
{
    None,
    auto,
    enabled,
    disabled,
}

enum SimdModeEnum
{
    None,
    auto,
    off,
}

enum DialectEnum
{
    None,
    rust,
    ecmascript,
}

class Options
{
    public RegexLibraryEnum Library { get; set; } = RegexLibraryEnum.ZigRegex;

    public bool case_insensitive { get; set; }
    public bool multiline { get; set; }
    public bool dot_all { get; set; }
    public bool extended { get; set; }
    public bool unicode { get; set; } = true;

    public CaseFoldEnum case_fold { get; set; } = CaseFoldEnum.None;
    public string? max_repetition { get; set; }
    public ByteEngineEnum byte_engine { get; set; } = ByteEngineEnum.None;
    public bool unicode_word_boundary_in_dfa { get; set; }
    public bool prefilter { get; set; }
    public SimdModeEnum simd { get; set; } = SimdModeEnum.None;

    public bool posix { get; set; }
    public bool longest { get; set; }
    public bool literal { get; set; }

    public bool sticky { get; set; }
    public bool v { get; set; } = true;
    public bool possessive { get; set; }

    public bool ungreedy { get; set; }
    public bool ascii { get; set; }

    public string? maxPatternLength { get; set; }
    public string? maxNestingDepth { get; set; }
    public string? maxProgramSize { get; set; }
    public string? maxCaptures { get; set; }
    public string? maxRepetition { get; set; }
    public string? startOffset { get; set; }

    public DialectEnum Dialect { get; set; } = DialectEnum.rust;
    public bool unicode_sets { get; set; }
    public bool utf8 { get; set; } = true;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
