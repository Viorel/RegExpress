using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;
using System.Diagnostics.CodeAnalysis;

namespace ExportFeatureMatrix.Fluency;

class FlagObj : Indicator
{
    readonly List<BoolStep> boolSteps = [];
    readonly Func<ExecContext, bool> flagGetter;
    readonly bool isInfo;

    public FlagObj( CategoryObj categoryObj, string shortDesc, string desc, Func<ExecContext, bool> flagGetter, bool isInfo )
        : base( categoryObj, shortDesc, desc )
    {
        this.flagGetter = flagGetter;
        this.isInfo = isInfo;
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

    public FlagObj Test( Func<ExecContext, bool>? precondition, [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        PatternTestObj item = new( precondition, pattern, textMatch, textNoMatch, expected );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        PatternTestObj item = new( null, pattern, textMatch, textNoMatch, expected );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch )
    {
        PatternTestObj item = new( null, pattern, textMatch, textNoMatch, null );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string textMatch )
    {
        PatternTestObj item = new( null, pattern, textMatch, null, null );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Test( Func<ExecContext, bool> func )
    {
        FuncTestObj item = new( func );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Flag( string shortDesc, string desc, Func<ExecContext, bool> flagGetter, bool isInfo = false )
    {
        return CategoryObj.Flag( shortDesc, desc, flagGetter, isInfo );
    }

    public CategoryObj Direct( string shortDesc, string desc, Func<ExecContext, IndicatorData?> func )
    {
        return CategoryObj.Direct( shortDesc, desc, func );
    }

    public CategoryObj Category( string name )
    {
        return CategoryObj.Category( name );
    }

    public override IndicatorData? Exec( ExecContext context )
    {
        bool fm_value = flagGetter( context );
        bool? exec_value = null;

        if( context.Validate )
        {
            if( boolSteps.Count != 0 )
            {
                exec_value = false;

                foreach( BoolStep step in boolSteps )
                {
                    exec_value = step.Exec( context );

                    if( exec_value == true ) break;
                }
            }
        }

        return GetIndicatorData( context.Validate, fm_value, exec_value );
    }

    IndicatorData GetIndicatorData( bool validate, bool fm_value, bool? exec_value )
    {
        ColourEnum colour = isInfo ? ColourEnum.Info : ColourEnum.Green;

        if( !validate )
        {
            if( fm_value )
            {
                return new IndicatorData( colour, "+" );
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
                    return new IndicatorData( colour, "+?" );
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
                    return new IndicatorData( colour, exec_value.Value ? "+" : "+???" );
                }
                else
                {
                    return new IndicatorData( ColourEnum.None, !exec_value.Value ? "" : "???" );
                }
            }
        }
    }
}
