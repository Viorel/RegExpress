using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace DotNETFrameworkPlugin
{
    sealed class Options
    {
        public bool Compiled { get; set; }
        public bool CultureInvariant { get; set; }
        public bool ECMAScript { get; set; }
        public bool ExplicitCapture { get; set; }
        public bool IgnoreCase { get; set; }
        public bool IgnorePatternWhitespace { get; set; }
        public bool Multiline { get; set; }
        public bool RightToLeft { get; set; }
        public bool Singleline { get; set; }

        public long TimeoutMs { get; set; } = 10_000;


        [JsonIgnore]
        public RegexOptions NativeOptions
        {
            get
            {
                return
                    ( Compiled ? RegexOptions.Compiled : 0 ) |
                    ( CultureInvariant ? RegexOptions.CultureInvariant : 0 ) |
                    ( ECMAScript ? RegexOptions.ECMAScript : 0 ) |
                    ( ExplicitCapture ? RegexOptions.ExplicitCapture : 0 ) |
                    ( IgnoreCase ? RegexOptions.IgnoreCase : 0 ) |
                    ( IgnorePatternWhitespace ? RegexOptions.IgnorePatternWhitespace : 0 ) |
                    ( Multiline ? RegexOptions.Multiline : 0 ) |
                    ( RightToLeft ? RegexOptions.RightToLeft : 0 ) |
                    ( Singleline ? RegexOptions.Singleline : 0 );
            }
            set
            {
                Compiled = value.HasFlag( RegexOptions.Compiled );
                CultureInvariant = value.HasFlag( RegexOptions.CultureInvariant );
                ECMAScript = value.HasFlag( RegexOptions.ECMAScript );
                ExplicitCapture = value.HasFlag( RegexOptions.ExplicitCapture );
                IgnoreCase = value.HasFlag( RegexOptions.IgnoreCase );
                IgnorePatternWhitespace = value.HasFlag( RegexOptions.IgnorePatternWhitespace );
                Multiline = value.HasFlag( RegexOptions.Multiline );
                RightToLeft = value.HasFlag( RegexOptions.RightToLeft );
                Singleline = value.HasFlag( RegexOptions.Singleline );
            }
        }

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
