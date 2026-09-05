namespace TinyRegexCPlugin;

class Options
{
    public bool MatchAll { get; set; } = false;

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
