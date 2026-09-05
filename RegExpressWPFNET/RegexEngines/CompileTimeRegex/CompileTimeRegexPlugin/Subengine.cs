using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;


namespace CompileTimeRegexPlugin;

partial class Subengine( Options options ) : RegexSubengine
{
    static readonly Lazy<FeatureMatrix> LazyFeatureMatrix = new( BuildFeatureMatrix );

    static readonly Lock Locker = new( );
    static readonly List<string> PathsToDeleteOnExit = [];

    static Subengine( )
    {
        AppDomain.CurrentDomain.ProcessExit += HandleExit;
    }

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.None;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        return new SyntaxOptions
        {
            XLevel = XLevelEnum.none,
            FeatureMatrix = LazyFeatureMatrix.Value
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Int64? template_depth = ValidationUtilities.ParseInt64( "TemplateDepth", options.TemplateDepth );

        string? additional_CL_option_1 = template_depth == null ? "" : $"/templateDepth:{template_depth}";

        string[] possible_group_names =
            PossibleNamesRegex( )
                .Matches( pattern )
                .Select( m => m.Groups["n"] )
                .Where( g => g.Success )
                .Select( g => g.Value )
                .ToArray( );

        string temp_dir = Path.Combine( Path.GetTempPath( ), Path.GetRandomFileName( ) );

        lock( Locker ) PathsToDeleteOnExit.Add( temp_dir );

        try
        {
            Directory.CreateDirectory( temp_dir );

            string worker_dir = GetWorkerDirectory( );

            // copy files

            CopyDirectory(
                Path.Combine( worker_dir, "compile-time-regular-expressions" ),
                Path.Combine( temp_dir, "compile-time-regular-expressions" ),
                recursive: true );

            string build_cmd_full_path = Path.Combine( temp_dir, "build.cmd" );

            File.Copy(
                Path.Combine( worker_dir, "build.cmd" ),
                build_cmd_full_path );

            // create CPP file
            {
                string cpp_contents = File.ReadAllText( Path.Combine( worker_dir, "CompileTimeRegexSample.cpp" ) );

                string flags;
                if( options.case_insensitive ) flags = ", ctre::case_insensitive"; else flags = ", ctre::case_sensitive";
                if( options.multiline ) flags += ", ctre::multiline";
                if( options.singleline ) flags += ", ctre::singleline";

                string names = string.Join( ", ", possible_group_names.Select( n => "L" + ToCString( n ) ) );
                if( !string.IsNullOrWhiteSpace( names ) ) names = ", " + names;

                cpp_contents = ReplaceRegex( ).Replace( cpp_contents, m =>
                {
                    return
                        m.Groups["pattern"].Success ? "L" + ToCString( pattern ) :
                        m.Groups["text"].Success ? "L" + ToCString( text ) :
                        m.Groups["flags"].Success ? flags :
                        m.Groups["names"].Success ? names :
                        throw new InvalidOperationException( );
                } );

                File.WriteAllText( Path.Combine( temp_dir, "CompileTimeRegexSample.cpp" ), cpp_contents );
            }

            // build executable
            string built_exe_full_path = Path.Combine( temp_dir, "CompileTimeRegexSample.exe" );
            {
                ProcessHelper ph = new( build_cmd_full_path )
                {
                    AllEncoding = EncodingEnum.ASCII,
                    Arguments = [additional_CL_option_1],
                };

                if( !ph.Start( cnc ) ) return RegexMatches.Empty;

                if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

                // file is not created in case of errors

                if( !File.Exists( built_exe_full_path ) )
                {
                    using StreamReader sr = new( ph.OutputStream );
                    string output = sr.ReadToEnd( );

                    // get error messages
                    string filtered_output = string.Join( Environment.NewLine, ErrorMessageRegex( ).Matches( output ).Cast<Match>( ).Select( m => m.Value ) );

                    throw new Exception( $"The code failed to compile.{Environment.NewLine}{Environment.NewLine}{filtered_output}" );
                }
            }

            // execute
            {
                ProcessHelper ph = new( built_exe_full_path )
                {
                    AllEncoding = EncodingEnum.ASCII
                };

                if( !ph.Start( cnc ) ) return RegexMatches.Empty;

                if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

                List<IMatch> matches = [];
                SimpleTextGetter stg = new( text );
                SimpleMatch? current_match = null;
                string? line;

                while( ( line = ph.StreamReader.ReadLine( ) ) != null )
                {
                    line = line.Trim( );

                    if( line.Length == 0 ) continue;
                    if( line.StartsWith( "d " ) ) continue; // (for debugging)

                    {
                        Match m = ParseMatchRegex( ).Match( line );
                        if( m.Success )
                        {
                            int native_index = int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                            Debug.Assert( native_index >= 0 );

                            if( native_index >= 0 )
                            {
                                int native_length = int.Parse( m.Groups[2].Value, CultureInfo.InvariantCulture );

                                current_match = SimpleMatch.Create( native_index, native_length, stg );
                                current_match.AddDefaultGroup( );

                                matches.Add( current_match );
                            }

                            continue;
                        }
                    }
                    {
                        Match g = ParseGroupRegex( ).Match( line );
                        if( g.Success )
                        {
                            if( current_match == null ) throw new Exception( "Invalid response." );

                            int native_index = int.Parse( g.Groups[1].Value, CultureInfo.InvariantCulture );
                            int native_length = int.Parse( g.Groups[2].Value, CultureInfo.InvariantCulture );
                            bool success = native_index >= 0;

                            string name = current_match.Groups.Count( ).ToString( CultureInfo.InvariantCulture );

                            if( !success )
                            {
                                current_match.AddFailedGroup( name );
                            }
                            else
                            {
                                current_match.AddSucceededGroup( native_index, native_length, name );
                            }

                            continue;
                        }
                    }
                    {
                        Match n = ParseNamedGroupRegex( ).Match( line );
                        if( n.Success )
                        {
                            if( current_match == null ) throw new Exception( "Invalid response." );

                            int i = int.Parse( n.Groups[1].Value, CultureInfo.InvariantCulture );
                            int native_index = int.Parse( n.Groups[2].Value, CultureInfo.InvariantCulture );
                            int native_length = int.Parse( n.Groups[3].Value, CultureInfo.InvariantCulture );

                            if( i >= 0 && i < possible_group_names.Length )
                            {
                                string name = possible_group_names[i];

                                // try to assign the name

                                SimpleGroup? candidate_group = current_match.Groups
                                    .Skip( 1 )
                                    .Cast<SimpleGroup>( )
                                    .Where( g => g.Success )
                                    .Where( g => g.NativeIndex == native_index && g.NativeLength == native_length )
                                    .Where( g => int.TryParse( g.Name, CultureInfo.InvariantCulture, out var _ ) )
                                    .FirstOrDefault( );

                                candidate_group?.SetName( name );
                            }

                            continue;
                        }
                    }
                }

                return new RegexMatches( matches.Count, matches );
            }
        }
        catch( Exception exc )
        {
            _ = exc;

            throw;
        }
        finally
        {
            try
            {
                Directory.Delete( temp_dir, recursive: true );

                lock( Locker ) PathsToDeleteOnExit.Remove( temp_dir );
            }
            catch
            {
                // ignore?
            }
        }
    }

    static string GetWorkerDirectory( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;

        return assembly_dir;
    }

    static void CopyDirectory( string sourceDir, string destinationDir, bool recursive )
    {
        // From https://learn.microsoft.com/en-us/dotnet/standard/io/how-to-copy-directories

        // Get information about the source directory
        var dir = new DirectoryInfo( sourceDir );

        // Check if the source directory exists
        if( !dir.Exists )
            throw new DirectoryNotFoundException( $"Source directory not found: {dir.FullName}" );

        // Cache directories before we start copying
        DirectoryInfo[] dirs = dir.GetDirectories( );

        // Create the destination directory
        Directory.CreateDirectory( destinationDir );

        // Get the files in the source directory and copy to the destination directory
        foreach( FileInfo file in dir.GetFiles( ) )
        {
            string targetFilePath = Path.Combine( destinationDir, file.Name );
            file.CopyTo( targetFilePath );
        }

        // If recursive and copying subdirectories, recursively call this method
        if( recursive )
        {
            foreach( DirectoryInfo subDir in dirs )
            {
                string newDestinationDir = Path.Combine( destinationDir, subDir.Name );
                CopyDirectory( subDir.FullName, newDestinationDir, true );
            }
        }
    }

    static string ToCString( string text )
    {
        if( text.Length == 0 ) return "\"\"";

        StringBuilder sb = new( "\"" );

        for( int i = 0; i < text.Length; )
        {
            Rune rune = Rune.GetRuneAt( text, i );

            int value = rune.Value;

            if( value >= '0' && value <= '9' ||
                value >= 'A' && value <= 'Z' ||
                value >= 'a' && value <= 'z' ||
                value == ' '
                ) // TODO: add more
            {
                sb.Append( unchecked((char)value) );
                ++i;
            }
            else if( value <= 0xFF )
            {
                sb.Append( $"\\u{value:X4}" );
                ++i;
            }
            else if( value <= 0xFFFF )
            {
                sb.Append( $"\\u{value:X4}" );
                Debug.Assert( rune.Utf16SequenceLength == 1 );
                ++i;
            }
            else
            {
                sb.Append( $"\\U{value:X8}" );
                Debug.Assert( rune.Utf16SequenceLength == 2 );
                i += 2;
            }
        }

        return sb.Append( '"' ).ToString( );
    }

    private static void HandleExit( object? sender, EventArgs e )
    {
        lock( Locker ) // ('lock' probably not needed)
        {
            foreach( string path in PathsToDeleteOnExit )
            {
                try
                {
                    Directory.Delete( path, recursive: true );
                }
                catch
                {
                    // ignore?
                }
            }

            PathsToDeleteOnExit.Clear( );
        }
    }

    private static FeatureMatrix BuildFeatureMatrix( )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = false,
            XModeComments = false,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = false,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = false,
            XXFlag = false,

            Literal_QE = false,
            InsideSets_Literal_QE = false,
            InsideSets_Literal_qBrace = false,

            Esc_a = true,
            Esc_b = false,
            Esc_e = false,
            Esc_f = true,
            Esc_n = true,
            Esc_r = true,
            Esc_t = true,
            Esc_v = false,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = true,
            Esc_u4 = true,
            Esc_U8 = false,
            Esc_uBrace = true,
            Esc_UBrace = false,
            Esc_c1 = false,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = false,
            GenericEscape = false,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = false,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = false,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.None,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = true,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = false,
            InsideSets_Esc_uBrace = true,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = false,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = false,
            InsideSets_GenericEscape = false,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = false,
            Class_hHhorspace = true,
            Class_lL = false,
            Class_N = true,
            Class_O = false,
            Class_R = false,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = true,
            Class_wW = true,
            Class_X = false,
            Class_pP = false,
            Class_pPBrace = true,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = true,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = true,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = true,
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = false,
            InsideSets_Class_pPBrace = true,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = false,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = false,
            InsideSets_Operator_DoubleVerticalLine = false,
            InsideSets_Operator_DoubleMinus = false,
            InsideSets_Operator_DoubleTilde = false,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Compatible,
            Anchor_z = true,
            Anchor_G = false,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = false,
            Anchor_mM = false,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = false,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = true,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.FixedLength, // (compilation error in case of (?<=x{2,3})
            NegativeLookbehind = FeatureMatrix.LookModeEnum.FixedLength, //  and (?<!x{2,3}); it is probably a defect
            NestedLookaround = true,
            AtomicGroup = true,
            BranchReset = false,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.OneDigit,
            Backref_kApos = false,
            Backref_kLtGt = false,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.Value,
            Backref_PEqName = false,
            AllowSpacesInBackref = false,

            Recursive_Num = false,
            Recursive_PlusMinusNum = false,
            Recursive_R = false,
            Recursive_Name = false,
            Recursive_PGtName = false,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = true,
            Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.None,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = false,
            Quantifier_Lazy = true,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = false,
            Conditional_BackrefByName = false,
            Conditional_Pattern = false,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = false,
            Conditional_BackrefByName_LtGt = false,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = false,
            Conditional_VERSION = false,

            ControlVerbs = false,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = false,
            EmptyConstructX = false,
            EmptySet = false,
            EmptySetAny = true,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = false,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = false,
            KeepSurrogatePairs = false,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = false,
            ßSS = false,
        };
    }

    [GeneratedRegex( @"(?<pattern>/\*START-PATTERN\*/.*?/\*END-PATTERN\*/) | (?<text>/\*START-TEXT\*/.*?/\*END-TEXT\*/) | (?<flags>/\*START-MODIFIERS\*/.*?/\*END-MODIFIERS\*/) | (?<names>/\*START-NAMES\*/.*?/\*END-NAMES\*/)", RegexOptions.IgnorePatternWhitespace )]
    private static partial Regex ReplaceRegex( );

    /* Example of errors:
    **********************************************************************
    ** Visual Studio 2022 Developer Command Prompt v17.14.16
    ** Copyright (c) 2025 Microsoft Corporation
    **********************************************************************
    [vcvarsall.bat] Environment initialized for: 'x64'
    CompileTimeRegexSample.cpp
    CompileTimeRegexSample.cpp(34): error C2026: string too big, trailing characters truncated
    CompileTimeRegexSample.cpp(34): fatal error C1003: error count exceeds 100; stopping compilation
    */

    [GeneratedRegex( @"(?<=(\(\d+\): | ^)\s*) (fatal\s+)? error.*?:.*?(?=\r|\n|$)", RegexOptions.IgnorePatternWhitespace | RegexOptions.IgnoreCase | RegexOptions.Multiline )]
    private static partial Regex ErrorMessageRegex( );

    [GeneratedRegex( "\\(\\? ((?'a'')|<) (?'n'.*?) (?(a)'|>)", RegexOptions.ExplicitCapture | RegexOptions.IgnorePatternWhitespace )]
    private static partial Regex PossibleNamesRegex( );

    [GeneratedRegex( @"(?ix)^\s* M \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseMatchRegex( );

    [GeneratedRegex( @"(?ix)^\s* g \s+ (-?\d+) \s+ (-?\d+)" )]
    private static partial Regex ParseGroupRegex( );

    [GeneratedRegex( @"(?ix)^\s* n \s+ (\d+) \s+ (\d+) \s+ (\d+)" )]
    private static partial Regex ParseNamedGroupRegex( );
}
