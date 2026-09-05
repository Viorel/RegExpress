namespace CppBuilderPlugin;

class Options
{
    public bool roIgnoreCase { get; set; }
    public bool roMultiLine { get; set; }
    public bool roExplicitCapture { get; set; }
    public bool roCompiled { get; set; }
    public bool roSingleLine { get; set; }
    public bool roIgnorePatternSpace { get; set; }
    public bool roNotEmpty { get; set; }

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
