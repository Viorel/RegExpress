using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace RegExpressLibrary.SyntaxColouring
{
    public sealed partial class PatternBuilder
    {
        readonly StringBuilder Sb;
#if DEBUG
        const string Prefix = "(?nsx)(\r\n";
        const string Or = " |\r\n";
        const string Suffix = "\r\n)";
#else
		const string Prefix = "(?nsx)(";
		const string Or = "|";
		const string Suffix = ")";
#endif
        const string AlwaysFailsPattern = "(?!)";

        int LengthBeforeGroup;
        int LengthAfterGroupHeader;

        Dictionary<string, bool?>? Variables = null;

#if DEBUG
        string? dbgCurrentGroup;
#endif

        public PatternBuilder( )
        {
            Sb = new StringBuilder( Prefix );
            LengthBeforeGroup = -1;
            LengthAfterGroupHeader = -1;
        }


        public PatternBuilder Add( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern )
        {
            if( string.IsNullOrEmpty( pattern ) ) return this;

            Debug.Assert( !Regex.IsMatch( pattern, @"^\s*(?<!\\)\| | (?<!\\)\|\s*$", RegexOptions.IgnorePatternWhitespace ) );

            if( LengthBeforeGroup > 0 ) // group in progress?
            {
                Debug.Assert( LengthAfterGroupHeader > LengthBeforeGroup );

                if( Sb.Length > LengthAfterGroupHeader ) Sb.Append( Or );
            }
            else
            {
                if( Sb.Length > Prefix.Length ) Sb.Append( Or );
            }

            Sb.Append( pattern );

            return this;
        }


        public PatternBuilder Add( params string[] patterns )
        {
            foreach( var pattern in patterns )
            {
                Add( pattern );
            }

            return this;
        }


        public PatternBuilder Add( PatternBuilder pb )
        {
            return Add( pb.ToPattern( ) );
        }


        public PatternBuilder Add( params PatternBuilder[] pbs )
        {
            foreach( var pb in pbs ) Add( pb );

            return this;
        }


        public PatternBuilder BeginGroup( string name )
        {
            if( LengthBeforeGroup >= 0 )
                throw new InvalidOperationException(
#if DEBUG
                    $"Group '{dbgCurrentGroup}' already in progress."
#else
					"Group already in progress."
#endif
                    );

#if DEBUG
            dbgCurrentGroup = name;
#endif

            LengthBeforeGroup = Sb.Length;

            if( Sb.Length > Prefix.Length ) Sb.Append( Or );

            if( string.IsNullOrEmpty( name ) )
                Sb.Append( '(' );
            else
                Sb.Append( @"(?<" ).Append( name ).Append( '>' );

            LengthAfterGroupHeader = Sb.Length;

            return this;
        }


        public PatternBuilder EndGroup( )
        {
            if( LengthBeforeGroup < 0 ) throw new InvalidOperationException( "No group in progress" );

            Debug.Assert( LengthBeforeGroup < Sb.Length );
            Debug.Assert( LengthAfterGroupHeader > LengthBeforeGroup );

            if( Sb.Length == LengthAfterGroupHeader ) // nothing added to group?
            {
                Sb.Length = LengthBeforeGroup;
            }
            else
            {
                Sb.Append( ')' );
            }

            LengthBeforeGroup = -1;
            LengthAfterGroupHeader = -1;

#if DEBUG
            dbgCurrentGroup = null;
#endif

            return this;
        }


        public PatternBuilder AddGroup( string name, [StringSyntax( StringSyntaxAttribute.Regex )] string pattern )
        {
            return BeginGroup( name ).Add( pattern ).EndGroup( );
        }


        public void SetVariable( string name, bool? value )
        {
            if( Variables == null )
            {
                if( value == null ) return;

                Variables = new Dictionary<string, bool?> { { name, value } };
            }
            else
            {
                if( value == null )
                {
                    Variables.Remove( name );
                }
                else
                {
                    Variables[name] = value;
                }
            }
        }


        public string ToPattern( )
        {
            if( LengthBeforeGroup >= 0 )
                throw new InvalidOperationException(
#if DEBUG
                    $"Group '{dbgCurrentGroup}' not finished."
#else
					"Group already in progress."
#endif
                    );

            string variables = Variables == null ? "" : string.Concat( Variables.Select( kv => $"(?<{kv.Key}>)" + ( kv.Value != true ? $"(?<-{kv.Key}>)" : "" ) ) );

            return Sb.Length == Prefix.Length ? AlwaysFailsPattern : variables + ( Sb.ToString( ) + Suffix );
        }


        public Regex ToRegex( )
        {
            return new Regex( ToPattern( ), RegexOptions.Compiled | RegexOptions.ExplicitCapture );
        }


        [Obsolete( "Make sure you do not call accidentally 'ToString' instead of 'ToPattern'" )]
#pragma warning disable CS0809 // Obsolete member overrides non-obsolete member
        public override string? ToString( )
#pragma warning restore CS0809 // Obsolete member overrides non-obsolete member
        {
            return base.ToString( );
        }
    }
}
