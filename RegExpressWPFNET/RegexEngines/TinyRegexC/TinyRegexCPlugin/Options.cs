namespace TinyRegexCPlugin;


enum ImplementationEnum
{
    None,
    kokke, // original
    rurban,
    gyrovorbis,
}

class Options
{
    public ImplementationEnum Implementation { get; set; } = ImplementationEnum.kokke;
    public bool MatchAll { get; set; } = false;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
