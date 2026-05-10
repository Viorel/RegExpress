using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Data;

namespace RegExpressWPFNET.Controls
{

    public class BoolNullVisibilityCollapsedConverter( ) : BaseBoolNullConverter( Visibility.Visible, Visibility.Collapsed ), IValueConverter
    {
    }
    public abstract class BaseBoolNullConverter( object true_val, object false_val )
    {

        public object Convert( object value, Type targetType, object parameter, CultureInfo? culture )
        {
            var res = GetConvertResult( value, parameter );
            if( targetType == typeof( Boolean ) )
                return res;
            return res ? true_val : false_val;
        }
        //uwp
        public object Convert( object value, Type targetType, object parameter, string language ) => Convert( value, targetType, parameter, default( CultureInfo ) );
        public static bool GetConvertResult( object value, object parameter )
        {
            bool res = true;
            if( value == null )
                res = false;
            else if( value is string sv )
                res = !String.IsNullOrWhiteSpace( sv );
            else if( value is bool bv )
                res = bv;
            if( parameter != null && ( ( parameter.GetType( ) == typeof( bool ) && ( (bool)parameter ) ) || ( parameter.GetType( ) == typeof( string ) && new string[] { "true", "1" }.Contains( parameter as string ) ) ) )
                res = !res;
            return res;
        }
        public object ConvertBack( object value, Type targetType, object parameter, CultureInfo culture ) => throw new NotImplementedException( );
        public object ConvertBack( object value, Type targetType, object parameter, string language ) => throw new NotImplementedException( );
    }
}
