using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace DotNETPlugin
{
    enum ClassEnum
    {
        None,
        RegexDotNet,
        RegexDotNetFramework,
        ReSharp,
        Scout,
    }

    enum ByteRegexEngineModeEnum
    {
        None,
        Optimized,
        General,
        AutomataOnly,
    }

    sealed class Options
    {
        public ClassEnum Class { get; set; } = ClassEnum.RegexDotNet;

        public bool Compiled { get; set; }
        public bool CultureInvariant { get; set; }
        public bool ECMAScript { get; set; }
        public bool ExplicitCapture { get; set; }
        public bool IgnoreCase { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool Multiline { get; set; }
        public bool NonBacktracking { get; set; } // not in .NET Framework 4.8
        public bool RightToLeft { get; set; }
        public bool Singleline { get; set; }

        public long TimeoutMs { get; set; } = 10_000;

        // RE#

        public bool UseDotnetUnicode { get; set; }
        public bool MinimizePattern { get; set; }
        public bool FindLookaroundPrefix { get; set; }

        public string? InitialDfaCapacity { get; set; }
        public string? MaxDfaCapacity { get; set; }
        public string? MaxPrefixLength { get; set; }
        public string? FindPotentialStartSizeLimit { get; set; }
        public string? StartsetInferenceLimit { get; set; }
        public string? DfaThreshold { get; set; }

        // Scout

        public bool Crlf { get; set; }
        //public LineTerminatorEnum LineTerminator { get; set; } // not used here
        public bool Utf8 { get; set; } = true;
        public bool UnicodeClasses { get; set; } = true;
        //public bool MatchInvalidUtf8 { get; set; } // not used here
        public ByteRegexEngineModeEnum ByteRegexEngineMode { get; set; } = ByteRegexEngineModeEnum.None;
        public string? DfaSizeLimit { get; set; }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
