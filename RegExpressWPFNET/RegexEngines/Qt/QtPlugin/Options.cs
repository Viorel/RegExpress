namespace QtPlugin;

class Options
{
    public bool CaseInsensitiveOption { get; set; }
    public bool DotMatchesEverythingOption { get; set; }
    public bool MultilineOption { get; set; }
    public bool ExtendedPatternSyntaxOption { get; set; }
    public bool InvertedGreedinessOption { get; set; }
    public bool DontCaptureOption { get; set; }
    public bool UseUnicodePropertiesOption { get; set; }

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
