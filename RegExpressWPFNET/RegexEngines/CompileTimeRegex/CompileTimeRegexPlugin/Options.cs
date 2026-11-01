namespace CompileTimeRegexPlugin;

enum KindEnum
{
    None,
    Ctre,
    MRegex,
}

class Options
{
    public KindEnum Kind { get; set; } = KindEnum.Ctre;

    public bool case_insensitive { get; set; }
    public bool multiline { get; set; }
    public bool singleline { get; set; }
    public string? TemplateDepth { get; set; } // for undocumented "/templateDepth:N" compiler option

    // MRegex

    public bool ungreedy { get; set; }
    public bool unroll { get; set; }

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
