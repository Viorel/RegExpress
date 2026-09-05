using System.Collections.Generic;
using System.Text.Json.Serialization;


namespace RegExpressWPFNET.Code
{
    public sealed class TabData
    {
        public string? Name;
        public string? Subtitle;
        public string? Pattern;
        public string? Text;
        public string? ActiveKind;
        public string? ActiveVersion;

        [JsonIgnore]
        public (string? Kind, string? Version) ActiveCombinedId => (ActiveKind, ActiveVersion);

        public List<EngineOptions>? EngineOptions;
        public bool ShowFirstMatchOnly;
        public bool UnderlineCurrentMatch;
        public bool ShowSucceededGroupsOnly;
        public bool ShowCaptures;
        public bool ShowWhiteSpaces;
        public string? Eol;
        public TabMetrics Metrics;
        public bool Wrap;
    }


    class AllTabData
    {
        public List<TabData> Tabs { get; set; } = new( );
    }


    public sealed class EngineOptions
    {
        public string? Kind;
        public string? Version;

        [JsonIgnore]
        public (string? Kind, string? Version) CombinedId => (Kind, Version);


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
