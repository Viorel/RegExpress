using System.ComponentModel;
using System.Runtime.CompilerServices;


namespace DartPlugin;

enum PackageEnum
{
    None,
    RegExp,
    OnigurumaDart,
    IRegexp,
}

enum OnigurumaSyntaxEnum
{
    None,
    onigSyntaxOniguruma,
    onigSyntaxAsis, // (undocumented?)
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

class Options 
{
    public PackageEnum package { get; set; } = PackageEnum.RegExp;

    public OnigurumaSyntaxEnum OnigurumaSyntax { get; set; } = OnigurumaSyntaxEnum.onigSyntaxOniguruma;

    public bool caseInsensitive { get; set; }
    public bool multiLine { get; set; }
    public bool unicode { get; set; }
    public bool dotAll { get; set; }

    // Oniguruma

    public bool extend { get; set; }
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
}
