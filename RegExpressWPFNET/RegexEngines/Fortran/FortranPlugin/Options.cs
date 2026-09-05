namespace FortranPlugin;

enum ModuleEnum
{
    None,
    Forgex,
    RegexPerazz,
    RegexJeyemhex,
}

class Options
{
    public ModuleEnum Module { get; set; } = ModuleEnum.Forgex;

    public bool MatchAll { get; set; } = false;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
