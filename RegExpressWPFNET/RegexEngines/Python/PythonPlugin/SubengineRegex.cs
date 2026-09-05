using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.IndexConverters;
using RegExpressLibrary.Matches.Simple;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;


namespace PythonPlugin;

partial class SubengineRegex( Options options ) : RegexSubengine
{
    static readonly LazyData<(bool isPosix, bool isVersion1), FeatureMatrix> LazyFeatureMatrix = new( d => BuildFeatureMatrix( d.isPosix, d.isVersion1 ) );

    static Lazy<string> LazyPythonWorker = new( LoadPythonWorker );

    public override RegexEngineCapabilityEnum GetCapabilities( )
    {
        return RegexEngineCapabilityEnum.HasCaptures | RegexEngineCapabilityEnum.OverlappingMatches;
    }

    public override SyntaxOptions GetSyntaxOptions( )
    {
        FeatureMatrix fm = LazyFeatureMatrix.GetValue( (options.POSIX, options.VERSION1) );

        return new SyntaxOptions
        {
            XLevel = options.VERBOSE ? XLevelEnum.x : XLevelEnum.none,
            FeatureMatrix = fm,
        };
    }

    public override RegexMatches GetMatches( ICancellable cnc, string pattern, string text )
    {
        Debug.Assert( options.Module == ModuleEnum.regex );

        string script = LazyPythonWorker.Value;

        double? timeout = ValidationUtilities.ParseDouble( "timeout", options.timeout );

        using ProcessHelper ph = new( GetPythonExePath( ) );

        ph.AllEncoding = EncodingEnum.UTF8;
        ph.Arguments = [
            "-I",           // isolate Python from the user's environment (implies -E, -P and -s)
            "-E",           // ignore PYTHON* environment variables (such as PYTHONPATH)
            "-P",           // don't prepend a potentially unsafe path to sys.path; also PYTHONSAFEPATH
            "-s",           // don't add user site directory to sys.path; also PYTHONNOUSERSITE=x
            "-S",           // don't imply 'import site' on initialization
            "-X", "utf8",   // set implementation-specific option
            "-c", script    // program passed in as string (terminates option list)
            ];

        ph.StreamWriter = sw =>
        {
            var obj = new
            {
                pattern = pattern,
                text,
                flags = new
                {
                    options.ASCII,
                    options.DOTALL,
                    options.IGNORECASE,
                    options.LOCALE,
                    options.MULTILINE,
                    options.VERBOSE,
                    //
                    options.BESTMATCH,
                    options.ENHANCEMATCH,
                    options.FULLCASE,
                    options.POSIX,
                    options.REVERSE,
                    options.UNICODE,
                    options.WORD,
                    options.VERSION0,
                    options.VERSION1,
                    //
                    options.overlapped,
                    options.partial,
                },
                timeout = timeout
            };
            var json = JsonSerializer.Serialize( obj, JsonUtilities.JsonOptions );
            sw.WriteLine( json );
        };

        if( !ph.Start( cnc ) ) return RegexMatches.Empty;

        if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

        List<IMatch> matches = [];
        SimpleTextGetter stg = new( text );
        CodepointIndexConverter index_converter = new( text );
        SimpleMatch? match = null;
        Dictionary<int, string> names = [];
        string? line;

        while( ( line = ph.StreamReader.ReadLine( ) ) != null )
        {
            if( line.Length == 0 || line.StartsWith( "#" ) ) continue;

            var m = NMgcRegex( ).Match( line );

            if( !m.Success )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                throw new Exception( "Internal error in Python engine." );
            }
            else
            {
                switch( m.Groups["t"].Value )
                {
                case "N":
                {
                    int index = int.Parse( m.Groups["i"].Value, CultureInfo.InvariantCulture );
                    string name = m.Groups["n"].Value;

                    Debug.Assert( !names.ContainsKey( index ) );

                    names[index] = name;
                }
                break;
                case "M":
                {
                    int native_start = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                    int native_end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                    int native_length = native_end - native_start;

                    Debug.Assert( native_start >= 0 && native_end >= native_start );

                    (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                    match = SimpleMatch.Create( native_start, native_length, char_index, char_length, stg );
                    matches.Add( match );
                }
                break;
                case "g":
                {
                    int native_start = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                    int native_end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                    int native_length = native_end - native_start;
                    bool success = native_start >= 0;

                    Debug.Assert( match != null );

                    int group_i = match.Groups.Count( );

                    string? name;
                    if( !names.TryGetValue( group_i, out name ) ) name = group_i.ToString( CultureInfo.InvariantCulture );

                    if( !success )
                    {
                        match.AddFailedGroup( name );
                    }
                    else
                    {
                        (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                        match.AddSucceededGroup( native_start, native_length, char_index, char_length, name );
                    }

                    ++group_i;
                }
                break;
                case "c":
                {
                    int native_start = int.Parse( m.Groups["s"].Value, CultureInfo.InvariantCulture );
                    int native_end = int.Parse( m.Groups["e"].Value, CultureInfo.InvariantCulture );
                    int native_length = native_end - native_start;

                    Debug.Assert( match != null );

                    (int char_index, int char_length) = index_converter.Convert( native_start, native_end );

                    SimpleGroup group = (SimpleGroup)match.Groups.Last( );

                    group.AddCapture( native_start, native_length, char_index, char_length );
                }
                break;
                default:
                    if( Debugger.IsAttached ) Debugger.Break( );

                    throw new Exception( "Internal error in Python engine." );
                }
            }
        }

        return new RegexMatches( matches.Count, matches );
    }

    static string GetPythonExePath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string python_exe = Path.Combine( assembly_dir, @"python-embed-amd64", @"python.exe" );

        return python_exe;
    }

    static string GetPythonWorkerPath( )
    {
        string assembly_location = Assembly.GetExecutingAssembly( ).Location;
        string assembly_dir = Path.GetDirectoryName( assembly_location )!;
        string worker_path = Path.Combine( assembly_dir, @"PythonWorkerRegex.py" );

        return worker_path;
    }

    static string LoadPythonWorker( )
    {
        string worker_path = GetPythonWorkerPath( );
        string worker = File.ReadAllText( worker_path );

        return worker;
    }

    static FeatureMatrix BuildFeatureMatrix( bool isPosix, bool isVersion1 )
    {
        return new FeatureMatrix
        {
            Parentheses = FeatureMatrix.PunctuationEnum.Normal,

            Brackets = true,
            ExtendedBrackets = false,

            VerticalLine = FeatureMatrix.PunctuationEnum.Normal,
            AlternationOnSeparateLines = false,

            InlineComments = true,
            XModeComments = true,
            InsideSets_XModeComments = false,

            Flags = true,
            ScopedFlags = true,
            CircumflexFlags = false,
            ScopedCircumflexFlags = false,
            XFlag = true,
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
            Esc_v = true,
            Esc_Octal = FeatureMatrix.OctalEnum.None,
            Esc_Octal0_1_3 = false,
            Esc_oBrace = false,
            Esc_x2 = true,
            Esc_xBrace = false,
            Esc_u4 = true,
            Esc_U8 = true,
            Esc_uBrace = false,
            Esc_UBrace = false,
            Esc_c1 = false,
            Esc_C1 = false,
            Esc_CMinus = false,
            Esc_NBrace = true,
            GenericEscape = false,

            InsideSets_Esc_a = true,
            InsideSets_Esc_b = true,
            InsideSets_Esc_e = false,
            InsideSets_Esc_f = true,
            InsideSets_Esc_n = true,
            InsideSets_Esc_r = true,
            InsideSets_Esc_t = true,
            InsideSets_Esc_v = true,
            InsideSets_Esc_Octal = FeatureMatrix.OctalEnum.Octal_1_3,
            InsideSets_Esc_Octal0_1_3 = false,
            InsideSets_Esc_oBrace = false,
            InsideSets_Esc_x2 = true,
            InsideSets_Esc_xBrace = false,
            InsideSets_Esc_u4 = true,
            InsideSets_Esc_U8 = true,
            InsideSets_Esc_uBrace = false,
            InsideSets_Esc_UBrace = false,
            InsideSets_Esc_c1 = false,
            InsideSets_Esc_C1 = false,
            InsideSets_Esc_CMinus = false,
            InsideSets_Esc_NBrace = true,
            InsideSets_GenericEscape = false,

            Class_Dot = true,
            Class_Cbyte = false,
            Class_Ccp = false,
            Class_dD = true,
            Class_hHhexa = false,
            Class_hHhorspace = false,
            Class_lL = false,
            Class_N = false,
            Class_O = false,
            Class_R = true,
            Class_sS = true,
            Class_sSx = false,
            Class_uU = false,
            Class_vV = false,
            Class_wW = true,
            Class_X = true,
            Class_pP = true,
            Class_pPBrace = true,

            InsideSets_Class_dD = true,
            InsideSets_Class_hHhexa = false,
            InsideSets_Class_hHhorspace = false,
            InsideSets_Class_lL = false,
            InsideSets_Class_R = false,
            InsideSets_Class_sS = true,
            InsideSets_Class_sSx = false,
            InsideSets_Class_uU = false,
            InsideSets_Class_vV = false,
            InsideSets_Class_wW = true,
            InsideSets_Class_X = false,
            InsideSets_Class_pP = true,
            InsideSets_Class_pPBrace = true,
            InsideSets_Class_Name = true,
            InsideSets_Equivalence = false,
            InsideSets_Collating = false,

            InsideSets_Operators = isVersion1,
            InsideSets_OperatorsExtended = false,
            InsideSets_Operator_Ampersand = false,
            InsideSets_Operator_Plus = false,
            InsideSets_Operator_VerticalLine = false,
            InsideSets_Operator_Minus = false,
            InsideSets_Operator_Circumflex = false,
            InsideSets_Operator_Exclamation = false,
            InsideSets_Operator_DoubleAmpersand = isVersion1,
            InsideSets_Operator_DoubleVerticalLine = isVersion1,
            InsideSets_Operator_DoubleMinus = isVersion1,
            InsideSets_Operator_DoubleTilde = isVersion1,

            Anchor_Circumflex = true,
            Anchor_Dollar = true,
            Anchor_A = true,
            Anchor_Z = FeatureMatrix.AnchorZModeEnum.Compatible, // 
            Anchor_z = true,
            Anchor_G = true,
            Anchor_bB = true,
            Anchor_bg = false,
            Anchor_bBBrace = false,
            Anchor_PosixWB = false,
            Anchor_K = true,
            Anchor_mM = true,
            Anchor_LtGt = false,
            Anchor_GraveApos = false,
            Anchor_yY = false,

            NamedGroup_Apos = false,
            NamedGroup_LtGt = true,
            NamedGroup_PLtGt = true,
            BalancingGroup = false,
            CapturingGroup = false,
            DuplicateGroupName = true,

            NoncapturingGroup = true,
            PositiveLookahead = true,
            NegativeLookahead = true,
            PositiveLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NegativeLookbehind = FeatureMatrix.LookModeEnum.AnyLength,
            NestedLookaround = true,
            AtomicGroup = true,
            BranchReset = true,
            NonatomicPositiveLookahead = false,
            NonatomicPositiveLookbehind = false,
            AbsentOperator = false,
            AllowSpacesInGroups = false,

            Backref_Num = FeatureMatrix.BackrefEnum.Any, // TODO: actually it supports \1, \2, ... \99.
            Backref_kApos = false,
            Backref_kLtGt = false,
            Backref_kBrace = false,
            Backref_kNum = false,
            Backref_kNegNum = false,
            Backref_gApos = FeatureMatrix.BackrefModeEnum.None,
            Backref_gLtGt = FeatureMatrix.BackrefModeEnum.Value,
            Backref_gNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gNegNum = FeatureMatrix.BackrefModeEnum.None,
            Backref_gBrace = FeatureMatrix.BackrefModeEnum.None,
            Backref_PEqName = true,
            AllowSpacesInBackref = false,

            Recursive_Num = true,
            Recursive_PlusMinusNum = true,
            Recursive_R = true,
            Recursive_Name = true,
            Recursive_PGtName = true,
            Recursive_ReturnGroups = false,

            Quantifier_Asterisk = true,
            Quantifier_Plus = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Question = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_FreeForm = FeatureMatrix.PunctuationEnum.Normal,
            Quantifier_Braces_Spaces = FeatureMatrix.SpaceUsageEnum.None,
            Quantifier_LowAbbrev = true,
            Quantifier_Lazy = !isPosix,
            Quantifier_Possessive = true,

            Conditional_BackrefByNumber = true,
            Conditional_BackrefByName = true,
            Conditional_Pattern = true,
            Conditional_PatternOrBackrefByName = false,
            Conditional_BackrefByName_Apos = false,
            Conditional_BackrefByName_LtGt = false,
            Conditional_R = false,
            Conditional_RName = false,
            Conditional_DEFINE = true,
            Conditional_VERSION = false,

            ControlVerbs = true,
            ScriptRuns = false,
            Callouts = false,

            EmptyConstruct = true,
            EmptyConstructX = true,
            EmptySet = false,
            EmptySetAny = false,

            Unicode_Class_Dot = true,
            Unicode_Class_vW = true,
            InsideSets_Unicode = true,
            UnicodeCaseFolding = true,
            KeepSurrogatePairs = true,
            FuzzyMatchingParams = false,
            TreatmentOfCatastrophicPatterns = FeatureMatrix.CatastrophicBacktrackingEnum.Accept,
            Σσς = true,
            ßSS = isVersion1,
        };
    }

    [GeneratedRegex( @"^(?'t'[Mgc]) (?'s'-?\d+), (?'e'-?\d+)|(?'t'N) (?'i'\d+) <(?'n'.*)>$", RegexOptions.ExplicitCapture )]
    private static partial Regex NMgcRegex( );
}
