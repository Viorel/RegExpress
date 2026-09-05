namespace DPlugin;

class Options
{

    //public bool g { get; set; } = true;
    public bool i { get; set; }
    public bool m { get; set; }
    public bool s { get; set; }
    public bool x { get; set; }


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
