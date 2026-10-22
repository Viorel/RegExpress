namespace DotNETPlugin;

enum ClassEnum
{
    None,
    RegexDotNet,
    RegexDotNetFramework,
    ReSharp,
    Scout,
    LokadUtf8Regex,
    GregsEx,
}

enum ByteRegexEngineModeEnum
{
    None,
    Optimized,
    General,
    AutomataOnly,
}

sealed class Options
{
    public ClassEnum Class { get; set; } = ClassEnum.RegexDotNet;

    public bool Compiled { get; set; }
    public bool CultureInvariant { get; set; }
    public bool ECMAScript { get; set; }
    public bool ExplicitCapture { get; set; }
    public bool IgnoreCase { get; set; }
    public bool IgnorePatternWhitespace { get; set; }
    public bool Multiline { get; set; }
    public bool NonBacktracking { get; set; } // not in .NET Framework 4.8
    public bool RightToLeft { get; set; }
    public bool Singleline { get; set; }

    public long TimeoutMs { get; set; } = 10_000;

    // RE#

    public bool UseDotnetUnicode { get; set; }
    public bool MinimizePattern { get; set; }
    public bool FindLookaroundPrefix { get; set; }

    public string? InitialDfaCapacity { get; set; }
    public string? MaxDfaCapacity { get; set; }
    public string? MaxPrefixLength { get; set; }
    public string? FindPotentialStartSizeLimit { get; set; }
    public string? StartsetInferenceLimit { get; set; }
    public string? DfaThreshold { get; set; }

    // Scout

    public bool Crlf { get; set; }
    public bool Utf8 { get; set; } = true;
    public bool UnicodeClasses { get; set; } = true;
    //public bool MatchInvalidUtf8 { get; set; } // Not supported here
    public string? LineTerminator { get; set; } = null;
    public ByteRegexEngineModeEnum ByteRegexEngineMode { get; set; } = ByteRegexEngineModeEnum.None;
    public string? DfaSizeLimit { get; set; }

    // GregsEx

    public bool HasIndices { get; set; } = true;
    public bool Global { get; set; } = true;
    public bool UnicodeSets { get; set; }
    public bool Sticky { get; set; }


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
