using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows;
using System.Diagnostics;

namespace RegExpressWPFNET.Code
{
    // See: https://stackoverflow.com/questions/5649875/how-to-make-the-border-trim-the-child-elements

    public class BorderClipConverter : IMultiValueConverter
    {
        public object Convert( object[] values, Type targetType, object parameter, CultureInfo culture )
        {
            double width = (double)values[0];
            double height = (double)values[1];

            double radius = 0;

            switch( values[2] )
            {
            case double dbl:
                radius = dbl;
                break;
            case int i:
                radius = i;
                break;
            case CornerRadius corner_radius:
                Debug.Assert( corner_radius.TopRight == corner_radius.TopLeft );
                Debug.Assert( corner_radius.BottomLeft == corner_radius.TopLeft );
                Debug.Assert( corner_radius.BottomRight == corner_radius.TopLeft );

                radius = corner_radius.TopLeft;
                break;
            default:
                Debug.Assert( false );
                break;
            }

            var geometry = new RectangleGeometry( new Rect( 0, 0, width, height ), radius, radius );
            geometry.Freeze( );

            return geometry;
        }


        public object[] ConvertBack( object value, Type[] targetTypes, object parameter, CultureInfo culture )
        {
            throw new NotSupportedException( );
        }
    }
}
