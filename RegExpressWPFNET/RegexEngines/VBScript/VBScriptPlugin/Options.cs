namespace VBScriptPlugin;

class Options
{
    public bool IgnoreCase { get; set; }
    public bool Multiline { get; set; }
    public bool Global { get; set; } = true;


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
