namespace RealPlugin;

class Options
{
    public bool icase { get; set; }
    public bool multiline { get; set; }
    public bool dotall { get; set; }
    //public bool bytes { get; set; } // not supported here
    public bool verbose { get; set; }
    public bool ecma { get; set; }
    public bool ascii { get; set; }
    public bool dollar_endonly { get; set; }
    public bool allow_raw_byte { get; set; }
    public bool ungreedy { get; set; }
    public bool longest { get; set; }
    public bool fullmatch { get; set; }

    public string? program_size { get; set; }
    public string? repeat_count { get; set; }
    public string? group_count { get; set; }
    public string? nesting_depth { get; set; }
    public string? lookaround_length { get; set; }
    public string? dfa_memory { get; set; }


    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
