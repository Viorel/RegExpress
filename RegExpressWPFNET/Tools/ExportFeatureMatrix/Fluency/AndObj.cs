using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace ExportFeatureMatrix.Fluency;

class AndObj : BoolStep
{
    readonly List<BoolStep> boolSteps = [];
    readonly FlagObj flagObj;

    public AndObj( FlagObj flagObj )
    {
        this.flagObj = flagObj;
    }

    public AndObj Test( Func<ExecContext, bool>? precondition, [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        PatternTestObj item = new( precondition, pattern, textMatch, textNoMatch, expected );
        boolSteps.Add( item );

        return this;
    }

    public AndObj Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        PatternTestObj item = new( null, pattern, textMatch, textNoMatch, expected );
        boolSteps.Add( item );

        return this;
    }

    public FlagObj Flag( string shortDesc, string desc, Func<ExecContext, bool> flagGetter, bool isInfo = false )
    {
        return flagObj.Flag( shortDesc, desc, flagGetter, isInfo );
    }

    public override bool? Exec( ExecContext context )
    {
        bool? value = null;

        foreach( BoolStep step in boolSteps )
        {
            value = step.Exec( context );

            if( value == false ) return false;
        }

        //return value; // (true or null)

        return true;
    }
}
