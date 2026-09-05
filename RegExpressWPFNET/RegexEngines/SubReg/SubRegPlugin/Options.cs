namespace SubRegPlugin;

class Options
{
    public string? max_captures { get; set; } = "10";
    public string? max_depth { get; set; } = "4";

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
