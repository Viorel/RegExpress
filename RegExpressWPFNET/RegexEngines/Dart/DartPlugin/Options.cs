using System.ComponentModel;
using System.Runtime.CompilerServices;


namespace DartPlugin;

enum PackageEnum
{
    None,
    RegExp,
    // 'Duppix' seems abandoned
    OnigurumaDart,
}

enum OnigurumaSyntaxEnum
{
    None,
    onigSyntaxOniguruma,
    onigSyntaxRuby,
    onigSyntaxPerl,
    onigSyntaxPerlNg,
    onigSyntaxJava,
    onigSyntaxPython,
    onigSyntaxGrep,
    onigSyntaxEmacs,
    onigSyntaxPosixBasic,
    onigSyntaxPosixExtended,
    onigSyntaxGnuRegex,
}

class Options : INotifyPropertyChanged
{
    private bool m_caseInsensitive;
    private bool m_multiLine;
    private bool m_dotAll;

    public PackageEnum package { get; set; } = PackageEnum.RegExp;

    public OnigurumaSyntaxEnum OnigurumaSyntax { get; set; } = OnigurumaSyntaxEnum.onigSyntaxOniguruma;

    public bool caseInsensitive
    {
        get => m_caseInsensitive;
        set
        {
            if( m_caseInsensitive != value )
            {
                m_caseInsensitive = value;

                NotifyPropertyChanged( );
                NotifyPropertyChanged( nameof( caseSensitive ) );
                NotifyPropertyChanged( nameof( ignoreCase ) );
            }
        }
    }

    public bool multiLine
    {
        get => m_multiLine;
        set
        {
            if( m_multiLine != value )
            {
                m_multiLine = value;

                NotifyPropertyChanged( );
            }
        }
    }
    public bool caseSensitive { get => !caseInsensitive; set => caseInsensitive = !value; }
    public bool unicode { get; set; }
    public bool dotAll
    {
        get => m_dotAll;
        set
        {
            if( m_dotAll != value )
            {
                m_dotAll = value;

                NotifyPropertyChanged( );
                NotifyPropertyChanged( nameof( singleLine ) );
            }
        }
    }

    // Oniguruma

    public bool ignoreCase { get => caseInsensitive; set => caseInsensitive = value; }
    public bool extend { get; set; }
    //public bool  multiLine  { get; set; }
    public bool singleLine { get => dotAll; set => dotAll = value; }
    public bool findLongest { get; set; }
    public bool findNotEmpty { get; set; }
    public bool negateSingleLine { get; set; }
    public bool dontCaptureGroup { get; set; }
    public bool captureGroup { get; set; }
    public bool notBol { get; set; }
    public bool notEol { get; set; }
    public bool posixRegion { get; set; }
    public bool checkValidityOfString { get; set; }
    public bool ignoreCaseIsAscii { get; set; }
    public bool wordIsAscii { get; set; }
    public bool digitIsAscii { get; set; }
    public bool spaceIsAscii { get; set; }
    public bool posixIsAscii { get; set; }
    public bool textSegmentExtendedGraphemeCluster { get; set; }
    public bool textSegmentWord { get; set; }
    public bool notBeginString { get; set; }
    public bool notEndString { get; set; }
    public bool notBeginPosition { get; set; }
    public bool callbackEachMatch { get; set; }
    public bool matchWholeString { get; set; }

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }

    void NotifyPropertyChanged( [CallerMemberName] string? propertyName = null )
    {
        PropertyChanged?.Invoke( this, new PropertyChangedEventArgs( propertyName ) );
    }

    #region INotifyPropertyChanged

    public event PropertyChangedEventHandler? PropertyChanged;

    #endregion

}
