using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressWPFNET.Code
{
    class PageSize
    {
        public double WidthMm { get; init; }
        public double HeighMm { get; init; }

        public PageSize( double widthMm, double heighMm )
        {
            WidthMm = widthMm;
            HeighMm = heighMm;
        }
    }


    static class PageSizes
    {
        public static readonly PageSize A3 = new PageSize( 297, 420 );
        public static readonly PageSize A4 = new PageSize( 210, 297 );
        public static readonly PageSize A5 = new PageSize( 148, 210 );
    }
}
