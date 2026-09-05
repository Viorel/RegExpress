namespace JavaPlugin;

enum PackageEnum
{
    None,
    regex,
    re2j,
    safere,
    reggie,
    joni,
}

class Options
{
    public PackageEnum Package { get; set; } = PackageEnum.regex;

    public bool CANON_EQ { get; set; }
    public bool CASE_INSENSITIVE { get; set; }
    public bool COMMENTS { get; set; }
    public bool DOTALL { get; set; }
    public bool LITERAL { get; set; }
    public bool MULTILINE { get; set; }
    public bool UNICODE_CASE { get; set; }
    public bool UNICODE_CHARACTER_CLASS { get; set; }
    public bool UNIX_LINES { get; set; }
    public string? regionStart { get; set; } // (int)
    public string? regionEnd { get; set; } // (int)
    public bool useAnchoringBounds { get; set; } = true;
    public bool useTransparentBounds { get; set; } = false;

    // re2j

    public bool DISABLE_UNICODE_GROUPS { get; set; }
    public bool LONGEST_MATCH { get; set; } // joni too

    // joni

    public bool FIND_NOT_EMPTY { get; set; }
    public bool NEGATE_SINGLELINE { get; set; }
    public bool DONT_CAPTURE_GROUP { get; set; }
    public bool CAPTURE_GROUP { get; set; } = true;
    //
    public bool NOTBOL { get; set; }
    public bool NOTEOL { get; set; }
    /* Does not seem to be implemented
    public bool NEWLINE_CRLF { get; set; }
    public bool NOTBOS { get; set; }
    public bool NOTEOS { get; set; }
    */
    public bool ASCII_RANGE { get; set; }
    public bool POSIX_BRACKET_ALL_RANGE { get; set; }
    public bool WORD_BOUND_ALL_RANGE { get; set; }
    public bool CR_7_BIT { get; set; }


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
