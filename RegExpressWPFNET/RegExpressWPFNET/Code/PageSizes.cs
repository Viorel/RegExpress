using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressWPFNET.Code
{
    internal readonly record struct PageSize( double WidthMm, double HeighMm );

    static class PageSizes
    {
        public static readonly PageSize A3 = new( 297, 420 );
        public static readonly PageSize A4 = new( 210, 297 );
        public static readonly PageSize A5 = new( 148, 210 );
    }
}
