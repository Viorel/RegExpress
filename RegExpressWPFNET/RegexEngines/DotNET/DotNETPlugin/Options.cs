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
    CorvusEcma262,
    FuzzyRegex,
}

enum ByteRegexEngineModeEnum
{
    None,
    Optimized,
    General,
    AutomataOnly,
}

enum GregsExProfileEnum
{
    None,
    EcmaScript,
    Xsd,
    XPath,
}

enum GregsExUnicodeVersionEnum
{
    None,
    Unicode16_0,
    Unicode17_0,
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

    public string? TimeoutMs { get; set; } = null;

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

    public GregsExProfileEnum GregsExProfile { get; set; } = GregsExProfileEnum.EcmaScript;
    public GregsExUnicodeVersionEnum GregsExUnicodeVersion { get; set; } = GregsExUnicodeVersionEnum.Unicode16_0;
    public bool HasIndices { get; set; } = true;
    public bool Global { get; set; } = true;
    public bool UnicodeSets { get; set; }
    public bool Sticky { get; set; }
    public bool Quote { get; set; }

    // FuzzyRegex

    public bool Ascii { get; set; }
    public bool Word { get; set; }
    public bool BestMatch { get; set; }
    public bool EnhanceMatch { get; set; }
    public bool Posix { get; set; }
    public bool FullCase { get; set; }
    public bool Version0 { get; set; }
    public bool Version1 { get; set; } = true;
    public bool Overlapped { get; set; }
    public bool Partial { get; set; }

    public string? StepTimeoutMs { get; set; } = null;
    public string? MaxCompiledNodes { get; set; } = null;


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
