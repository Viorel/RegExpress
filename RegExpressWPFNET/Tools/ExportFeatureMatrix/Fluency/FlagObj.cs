using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System.Diagnostics.CodeAnalysis;

namespace ExportFeatureMatrix.Fluency;

class FlagObj : Indicator
{
    readonly List<BoolStep> boolSteps = [];
    readonly Func<RegexEngine, FeatureMatrix, bool> flagGetter;

    public FlagObj( CategoryObj categoryObj, string shortDesc, string desc, Func<RegexEngine, FeatureMatrix, bool> flagGetter )
        : base( categoryObj, shortDesc, desc )
    {
        this.flagGetter = flagGetter;
    }

    public FlagObj IgnoreCase( bool yes = true )
    {
        IgnoreCaseObj item = new( yes );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj IgnorePatternWhitespace( bool yes = true )
    {
        IgnorePatternWhitespaceObj item = new( yes );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        PatternTestObj item = new( pattern, textMatch, textNoMatch, expected );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch )
    {
        PatternTestObj item = new( pattern, textMatch, textNoMatch, null );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string textMatch )
    {
        PatternTestObj item = new( pattern, textMatch, null, null );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( Func<RegexEngine, FeatureMatrix, bool> func )
    {
        FuncTestObj item = new( func );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Flag( string shortDesc, string desc, Func<RegexEngine, FeatureMatrix, bool> flagGetter )
    {
        return CategoryObj.Flag( shortDesc, desc, flagGetter );
    }

    public CategoryObj Category( string name )
    {
        return CategoryObj.Category( name );
    }

    public override IndicatorData? Exec( bool validate, RegexEngine engine, FeatureMatrix fm )
    {
        bool fm_value = flagGetter( engine, fm );
        bool? exec_value = null;

        if( validate )
        {
            if( boolSteps.Count != 0 )
            {
                exec_value = false;

                foreach( BoolStep step in boolSteps )
                {
                    exec_value = step.Exec( engine, fm );

                    if( exec_value == true ) break;
                }
            }
        }

        return GetIndicatorData( validate, fm_value, exec_value );
    }

    IndicatorData GetIndicatorData( bool validate, bool fm_value, bool? exec_value )
    {
        if( !validate )
        {
            if( fm_value )
            {
                return new IndicatorData( ColourEnum.Green, "+" );
            }
            else
            {
                return new IndicatorData( ColourEnum.None, "" );
            }
        }
        else
        {
            if( exec_value == null )
            {
                // no steps, cannot validate

                if( fm_value )
                {
                    return new IndicatorData( ColourEnum.Green, "+?" );
                }
                else
                {
                    return new IndicatorData( ColourEnum.None, "?" );
                }
            }
            else
            {
                if( fm_value )
                {
                    return new IndicatorData( ColourEnum.Green, exec_value.Value ? "+" : "+???" );
                }
                else
                {
                    return new IndicatorData( ColourEnum.None, !exec_value.Value ? "" : "???" );
                }
            }
        }
    }
}
