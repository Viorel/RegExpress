namespace GretaPlugin;

enum ModeEnum
{
    None = 0,
    Default,
    MODE_FAST,
    MODE_SAFE,
    MODE_MIXED,
}

class Options
{
    public bool NOCASE { get; set; }
    public bool MULTILINE { get; set; }
    public bool SINGLELINE { get; set; }
    public bool EXTENDED { get; set; }
    public bool RIGHTMOST { get; set; }
    public bool NORMALIZE { get; set; }
    public ModeEnum Mode { get; set; } = ModeEnum.Default;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
