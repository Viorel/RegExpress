namespace CompileTimeRegexPlugin;

class Options
{
    public bool case_insensitive { get; set; }
    public bool multiline { get; set; }
    public bool singleline { get; set; }
    public string? TemplateDepth { get; set; } // for undocumented "/templateDepth:N" compiler option

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
