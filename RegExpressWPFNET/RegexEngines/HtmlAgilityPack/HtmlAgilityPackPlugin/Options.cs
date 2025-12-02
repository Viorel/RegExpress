namespace HtmlAgilityPackPlugin
{
    public enum SelectorMode
    {
        XPath,
        CssSelector
    }

    public enum OutputMode
    {
        OuterHtml,
        InnerHtml,
        InnerText
    }

    sealed class Options
    {
        public SelectorMode SelectorMode { get; set; } = SelectorMode.XPath;
        public OutputMode OutputMode { get; set; } = OutputMode.OuterHtml;

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
