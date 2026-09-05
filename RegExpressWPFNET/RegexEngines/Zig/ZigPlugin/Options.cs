namespace ZigPlugin;

enum RegexLibraryEnum
{
    None,
    ZigRegex,
    Mvzr,
    Pzre,
    EziGex,
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

class Options
{
    public RegexLibraryEnum Library { get; set; } = RegexLibraryEnum.ZigRegex;

    public bool case_insensitive { get; set; }
    public bool multiline { get; set; }
    public bool dot_all { get; set; }
    public bool extended { get; set; }
    public bool unicode { get; set; }

    public CaseFoldEnum case_fold { get; set; } = CaseFoldEnum.None;
    public string? max_repetition { get; set; }
    public ByteEngineEnum byte_engine { get; set; } = ByteEngineEnum.None;
    public bool unicode_word_boundary_in_dfa { get; set; }
    public bool prefilter { get; set; }
    public SimdModeEnum simd { get; set; } = SimdModeEnum.None;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
