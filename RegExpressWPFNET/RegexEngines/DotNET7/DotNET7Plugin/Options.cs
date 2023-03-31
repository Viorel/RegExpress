using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace DotNET7Plugin
{
    class Options
    {
        public bool IgnoreCase { get; set; }
        public bool Multiline { get; set; }
        public bool ExplicitCapture { get; set; }
        public bool Compiled { get; set; }
        public bool Singleline { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool RightToLeft { get; set; }
        public bool ECMAScript { get; set; }
        public bool CultureInvariant { get; set; }

        public long TimeoutMs { get; set; } = 10_000;


        [JsonIgnore]
        public RegexOptions NativeOptions
        {
            get
            {
                return
                    ( IgnoreCase ? RegexOptions.IgnoreCase : 0 ) |
                    ( Multiline ? RegexOptions.Multiline : 0 ) |
                    ( ExplicitCapture ? RegexOptions.ExplicitCapture : 0 ) |
                    ( Compiled ? RegexOptions.Compiled : 0 ) |
                    ( Singleline ? RegexOptions.Singleline : 0 ) |
                    ( IgnorePatternWhitespace ? RegexOptions.IgnorePatternWhitespace : 0 ) |
                    ( RightToLeft ? RegexOptions.RightToLeft : 0 ) |
                    ( ECMAScript ? RegexOptions.ECMAScript : 0 ) |
                    ( CultureInvariant ? RegexOptions.CultureInvariant : 0 );
            }
            set
            {
                IgnoreCase = value.HasFlag( RegexOptions.IgnoreCase );
                Multiline = value.HasFlag( RegexOptions.Multiline );
                ExplicitCapture = value.HasFlag( RegexOptions.ExplicitCapture );
                Compiled = value.HasFlag( RegexOptions.Compiled );
                Singleline = value.HasFlag( RegexOptions.Singleline );
                IgnorePatternWhitespace = value.HasFlag( RegexOptions.IgnorePatternWhitespace );
                RightToLeft = value.HasFlag( RegexOptions.RightToLeft );
                ECMAScript = value.HasFlag( RegexOptions.ECMAScript );
                CultureInvariant = value.HasFlag( RegexOptions.CultureInvariant );
            }
        }


        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
