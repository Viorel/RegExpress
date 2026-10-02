namespace ExportFeatureMatrix.Fluency;

class IndicatorData
{
    public ColourEnum Colour { get; init; }
    public string? Text { get; init; }

    public IndicatorData( ColourEnum colour, string? text )
    {
        Colour = colour;
        Text = text;
    }
}
