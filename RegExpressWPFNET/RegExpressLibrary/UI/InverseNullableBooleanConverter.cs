using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Windows.Data;

namespace RegExpressLibrary.UI;

// https://stackoverflow.com/questions/1039636/how-to-bind-inverse-boolean-properties-in-wpf

[ValueConversion( sourceType: typeof( bool? ), targetType: typeof( bool? ) )]
public sealed class InverseNullableBooleanConverter : IValueConverter
{
    #region IValueConverter

    public object Convert( object value, Type targetType, object parameter, CultureInfo culture )
    {
        return PerformConversion( value, targetType, parameter, culture );
    }

    public object ConvertBack( object value, Type targetType, object parameter, CultureInfo culture )
    {
        return PerformConversion( value, targetType, parameter, culture );
    }

    #endregion

    static object PerformConversion( object value, Type targetType, object parameter, CultureInfo culture )
    {
        if( targetType == typeof( bool ) )
        {
            bool b = (bool)value;

            return !b;
        }

        if( targetType == typeof( bool? ) )
        {
            if( value == null ) return null!;

            bool b = ( (bool?)value ).Value;

            return !b;
        }

        throw new InvalidOperationException( "The target must be a 'bool' or 'bool?'" );
    }

}
