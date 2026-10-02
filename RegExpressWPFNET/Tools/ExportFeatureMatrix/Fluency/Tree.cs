namespace ExportFeatureMatrix.Fluency;

class Tree
{
    readonly List<CategoryObj> categories = [];

    public IReadOnlyList<CategoryObj> Categories => categories;

    public CategoryObj Category( string name )
    {
        CategoryObj cat = new( this, name );
        categories.Add( cat );

        return cat;
    }
}
