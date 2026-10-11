namespace VBScriptPlugin;

enum ImplementationEnum
{
    None,
    VBScript,
    tbRegExp, // (twinBASIC)
}

class Options
{
    public ImplementationEnum Implementation { get; set; } = ImplementationEnum.VBScript;
    public bool IgnoreCase { get; set; }
    public bool Multiline { get; set; }
    public bool Global { get; set; } = true;

    // tbRegExp
    public bool DotAll { get; set; }


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
