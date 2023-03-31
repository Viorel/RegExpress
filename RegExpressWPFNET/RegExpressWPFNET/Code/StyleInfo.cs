using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Documents;


namespace RegExpressWPFNET.Code
{
    public sealed class StyleInfo
    {
        readonly List<(DependencyProperty prop, object val)> mValues = new List<(DependencyProperty, object)>( );


        public StyleInfo( string key )
        {
            Init( (Style)App.Current.Resources[key] );
        }

        public StyleInfo( Style style )
        {
            Init( style );
        }


        public IEnumerable<(DependencyProperty prop, object val)> Values
        {
            get { return mValues; }
        }


        void Init( Style style )
        {
            IEnumerable<Setter> setters = style.Setters.OfType<Setter>( );

            foreach( Setter setter in setters )
            {
                mValues.Add( (setter.Property, setter.Value) );
            }
        }
    }
}
