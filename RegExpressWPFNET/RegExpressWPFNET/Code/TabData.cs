using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace RegExpressWPFNET.Code
{
    public sealed class TabData
    {
        public string? Name;
        public string? Pattern;
        public string? Text;
        public string? ActiveKind;
        public Version? ActiveVersion;

        [JsonIgnore]
        public (string? Kind, Version? Version) ActiveCombinedId => (ActiveKind, ActiveVersion);

        public List<EngineOptions>? EngineOptions;
        public bool ShowFirstMatchOnly;
        public bool ShowSucceededGroupsOnly;
        public bool ShowCaptures;
        public bool ShowWhiteSpaces;
        public string? Eol;
        public TabMetrics Metrics;
    }


    class AllTabData
    {
        public List<TabData> Tabs { get; set; } = new( );
    }


    public sealed class EngineOptions
    {
        public string? Kind;
        public Version? Version;

        [JsonIgnore]
        public (string? Kind, Version? Version) CombinedId => (Kind, Version);


        [JsonConverter( typeof( RawJsonConverter ) )]
        public string? Options; // (JSON)
    }


    public struct TabMetrics
    {
        public double
            RightColumnWidth,
            TopRowHeight,
            BottomRowHeight;
    }
}
