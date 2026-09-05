namespace AdaPlugin;

class Options
{
    public bool Case_Insensitive { get; set; }
    public bool Single_Line { get; set; }
    public bool Multiple_Lines { get; set; }

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
