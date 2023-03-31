using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace RegExpressWPFNET.Code
{
    public sealed class TabData
    {
        public string Name;
        public string Pattern;
        public string Text;
        public (string Kind, Version Version) ActiveEngineId;
        public Dictionary<(string Kind, Version Version), string? /* custom options */> AllRegexOptions = new Dictionary<(string Kind, Version Version), string?>( );
        public bool ShowFirstMatchOnly;
        public bool ShowSucceededGroupsOnly;
        public bool ShowCaptures;
        public bool ShowWhiteSpaces;
        public string Eol;
        public TabMetrics Metrics;
    }


    public struct TabMetrics
    {
        public double
            RightColumnWidth,
            TopRowHeight,
            BottomRowHeight;
    }
}
