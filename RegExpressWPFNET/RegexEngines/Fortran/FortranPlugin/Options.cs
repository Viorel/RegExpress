namespace FortranPlugin;

enum ModuleEnum
{
    None,
    Forgex,
    RegexPerazz,
    RegexJeyemhex,
    Fregex,
}

class Options
{
    public ModuleEnum Module { get; set; } = ModuleEnum.Forgex;

    public bool ignorecase { get; set; }
    public bool multiline { get; set; }
    public bool dotall { get; set; }
    public string? step_limit { get; set; } = null;

    public bool MatchAll { get; set; } = false;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
