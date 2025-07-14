using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using DocumentFormat.OpenXml;
using DocumentFormat.OpenXml.Packaging;
using DocumentFormat.OpenXml.Spreadsheet;
using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

partial class FeatureMatrixExporter
{
    WorksheetPart? WorksheetPart = null;
    SharedStringTable? SharedStringTable = null;

    public void ExportToExcel( string outputExcelPath, IReadOnlyList<RegexPlugin> plugins )
    {
        using( SpreadsheetDocument spreadSheet = SpreadsheetDocument.Create( outputExcelPath, SpreadsheetDocumentType.Workbook ) )
        {
            const uint START_TABLE_COLUMN = 1;
            const uint START_ENGINES_COLUMN = START_TABLE_COLUMN + 2;
            const uint START_TABLE_ROW = 3;

            // create workbook

            WorkbookPart workbookPart = spreadSheet.WorkbookPart ?? spreadSheet.AddWorkbookPart( );
            Workbook workbook = new( );
            workbookPart.Workbook = workbook;

            // create string table

            SharedStringTablePart string_table_part = workbookPart.GetPartsOfType<SharedStringTablePart>( ).FirstOrDefault( ) ?? workbookPart.AddNewPart<SharedStringTablePart>( );
            string_table_part.SharedStringTable ??= new SharedStringTable( );
            SharedStringTable = string_table_part.SharedStringTable;

            // styles

            CreateStyles( workbookPart );

            // view to freeze rows and columns

            Pane pane = new( ) { ActivePane = PaneValues.BottomRight, HorizontalSplit = 2, VerticalSplit = 4, State = PaneStateValues.Frozen, TopLeftCell = $"{ColumnNameFromIndex( START_ENGINES_COLUMN )}{START_TABLE_ROW + 2}" };
            SheetView sheetView = new( pane ) { TabSelected = true, WorkbookViewId = 0 };

            // create worksheet

            WorksheetPart = workbookPart.AddNewPart<WorksheetPart>( );
            Worksheet worksheet = new( new SheetViews( sheetView ), new SheetData( ) );
            WorksheetPart.Worksheet = worksheet;

            Sheets sheets = workbookPart.Workbook.GetFirstChild<Sheets>( ) ?? workbookPart.Workbook.AppendChild( new Sheets( ) );
            string relationshipId = workbookPart.GetIdOfPart( WorksheetPart );

            string sheetName = "Feature Matrix";

            Sheet sheet = new( ) { Id = relationshipId, SheetId = 1, Name = sheetName };
            sheets.Append( sheet );

            // get feature matrices from engines

            IRegexEngine[] engines = [.. plugins.SelectMany( p => p.GetEngines( ) )];

            IReadOnlyList<(string? variantName, FeatureMatrix fm)>[] all_matrices = [.. engines.Select( e => e.GetFeatureMatrices( ) )];

            uint variant_index = 0;
            EngineData[] engines_data =
                [.. plugins
                    .SelectMany( p => p.GetEngines( ) )
                    .Select( e => new EngineData { Engine = e, Matrices = [.. e.GetFeatureMatrices( ).Select( m => (index:variant_index++, m.variantName, m.fm) )] } ).Where( d => d.Matrices.Length > 0 )];

            uint total_variants = (uint)engines_data.Select( d => d.Matrices.Length ).Sum( );

            // caption

            Cell cell1 = SetCell( "A", 1, "Regex Feature Matrix" );
            cell1.StyleIndex = 1;
            Cell cell2 = SetCell( ColumnNameFromIndex( (uint)total_variants + 2 ), 1, "" );
            MergeExistingCells( cell1, cell2 );
            SetRowHeight( cell1, 40 );

            SetColumnWidth( 1, 2, 20 );

            // table header

            cell1 = SetCell( "A", START_TABLE_ROW, "Feature" );
            cell1.StyleIndex = 2;
            SetRowHeight( cell1, 35 );
            cell2 = SetCell( "A", START_TABLE_ROW + 1, "" );
            SetRowHeight( cell2, 25 );
            MergeExistingCells( cell1, cell2 );

            cell1 = SetCell( "B", START_TABLE_ROW, "Description" );
            cell1.StyleIndex = 2;
            cell2 = SetCell( "B", START_TABLE_ROW + 1, "" );
            MergeExistingCells( cell1, cell2 );

            uint row_index = START_TABLE_ROW;

            foreach( EngineData engine_data in engines_data )
            {
                cell1 = SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + engine_data.Matrices[0].index ), row_index, $"{engine_data.Engine.Name}\r\n{engine_data.Engine.Version}" );
                cell1.StyleIndex = 2;

                if( engine_data.Matrices.Length > 1 )
                {
                    cell2 = SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + engine_data.Matrices.Last( ).index ), row_index, "" );
                    MergeExistingCells( cell1, cell2 );
                }
                else
                {
                    cell2 = SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + engine_data.Matrices[0].index ), row_index + 1, "" );
                    MergeExistingCells( cell1, cell2 );
                }
            }

            ++row_index;

            foreach( EngineData engine_data in engines_data )
            {
                foreach( var m in engine_data.Matrices )
                {
                    uint column_index = START_ENGINES_COLUMN + m.index;
                    cell1 = SetCell( ColumnNameFromIndex( column_index ), row_index, m.variantName ?? "" );
                    cell1.StyleIndex = 2;

                    double width = EvaluateWidth( !string.IsNullOrWhiteSpace( m.variantName ) ? m.variantName : engine_data.Engine.Name, isBold: true );

                    SetColumnWidth( column_index, width + 4 );
                }
            }

            // body

            ++row_index;

            foreach( FeatureMatrixDetails details in FeatureMatrixDetails.AllFeatureMatrixDetails )
            {
                if( details.Func == null )
                {
                    // name of the group

                    cell1 = SetCell( ColumnNameFromIndex( START_TABLE_COLUMN ), row_index, details.ShortDesc );
                    cell1.StyleIndex = 3;
                    cell2 = SetCell( ColumnNameFromIndex( START_TABLE_COLUMN + total_variants + 1 ), row_index, "" );
                    MergeExistingCells( cell1, cell2 );
                }
                else
                {
                    SetCell( ColumnNameFromIndex( START_TABLE_COLUMN ), row_index, details.ShortDesc );
                    SetCell( ColumnNameFromIndex( START_TABLE_COLUMN + 1 ), row_index, details.Desc ?? "" );

                    foreach( EngineData engine_data in engines_data )
                    {
                        foreach( var m in engine_data.Matrices )
                        {
                            bool yes = details.Func( m.fm );
                            cell1 = SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + m.index ), row_index, yes ? "+" : "" );
                            if( yes ) cell1.StyleIndex = 4;
                        }
                    }
                }

                ++row_index;
            }
        }
    }

    void CreateStyles( WorkbookPart workbookPart )
    {
        // https://stackoverflow.com/questions/28560455/create-excel-file-with-style-tag-using-openxmlwriter-sax


        Fonts fonts = new( );
        Fills fills = new( );
        Borders borders = new( );
        CellFormats cell_formats = new( );

        // first two fills are reserved (https://boyersnet.com/blog/2021/02/10/building-an-excel-document-with-csharp-and-openxml/)
        fills.Append( new Fill( new PatternFill { PatternType = PatternValues.None } ) );
        fills.Append( new Fill( new PatternFill { PatternType = PatternValues.Gray125 } ) );

        // 0 - default

        {
            Font font = new( );
            fonts.Append( font );

            Fill fill = new( );
            fills.Append( fill );

            Border border = new( );
            borders.Append( border );

            CellFormat cell_format = new( ) { FontId = (uint)fonts.ChildElements.Count - 1, FillId = (uint)fills.ChildElements.Count - 1, BorderId = (uint)borders.ChildElements.Count - 1 };
            cell_formats.Append( cell_format );
        }

        // 1 -- caption

        {
            Font font = new( new Bold( ) ) { FontSize = new FontSize { Val = new DoubleValue( 20.0 ) } };
            fonts.Append( font );

            CellFormat cell_format = new( )
            {
                FontId = (uint)fonts.ChildElements.Count - 1,
                ApplyAlignment = true,
                Alignment = new Alignment { Horizontal = HorizontalAlignmentValues.Left, Vertical = VerticalAlignmentValues.Center }
            };
            cell_formats.Append( cell_format );
        }

        // 2 -- table header

        {
            Font font = new( new Bold( ) );
            fonts.Append( font );

            CellFormat cell_format = new( )
            {
                FontId = (uint)fonts.ChildElements.Count - 1,
                ApplyAlignment = true,
                Alignment = new Alignment { Horizontal = HorizontalAlignmentValues.Center, Vertical = VerticalAlignmentValues.Center, WrapText = true }
            };
            cell_formats.Append( cell_format );
        }

        // 3 -- title of Feature Group

        {
            Font font = new( new Bold( ) );
            fonts.Append( font );

            Fill fill = new( new PatternFill( new ForegroundColor { Rgb = "FFFFF8DC" } ) { PatternType = PatternValues.Solid } );
            fills.Append( fill );

            CellFormat cell_format = new( )
            {
                FontId = (uint)fonts.ChildElements.Count - 1,
                ApplyFill = true,
                FillId = (uint)fills.ChildElements.Count - 1,
                ApplyAlignment = true,
                Alignment = new Alignment { Horizontal = HorizontalAlignmentValues.Left, Vertical = VerticalAlignmentValues.Center }
            };
            cell_formats.Append( cell_format );
        }

        // 4 -- the "+"

        {
            Font font = new( );
            fonts.Append( font );

            Fill fill = new( new PatternFill( new ForegroundColor { Rgb = "FFE8FCE8" } ) { PatternType = PatternValues.Solid } );
            fills.Append( fill );

            CellFormat cell_format = new( )
            {
                FontId = (uint)fonts.ChildElements.Count - 1,
                ApplyFill = true,
                FillId = (uint)fills.ChildElements.Count - 1,
                ApplyAlignment = true,
                Alignment = new Alignment { Horizontal = HorizontalAlignmentValues.Center, Vertical = VerticalAlignmentValues.Center }
            };
            cell_formats.Append( cell_format );
        }

        WorkbookStylesPart stylesPart = workbookPart.AddNewPart<WorkbookStylesPart>( );
        Stylesheet stylesheet = new( );

        stylesheet.Append( fonts );
        stylesheet.Append( fills );
        stylesheet.Append( borders );
        stylesheet.Append( cell_formats );

        stylesPart.Stylesheet = stylesheet;
        //stylesheet.Save( ); //?
    }

    Cell SetCell( string column, uint row, string value )
    {
        Cell cell = InsertCellInWorksheet( column, row );
        int text_index = InsertSharedStringItem( value );

        cell.CellValue = new CellValue( text_index.ToString( ) );
        cell.DataType = new EnumValue<CellValues>( CellValues.SharedString );

        return cell;
    }

    // Given text and a SharedStringTablePart, creates a SharedStringItem with the specified text 
    // and inserts it into the SharedStringTablePart. If the item already exists, returns its index.

    int InsertSharedStringItem( string text )
    {
        int i = 0;

        // Iterate through all the items in the SharedStringTable. If the text already exists, return its index.
        foreach( SharedStringItem item in SharedStringTable!.Elements<SharedStringItem>( ) )
        {
            if( item.InnerText == text )
            {
                return i;
            }

            i++;
        }

        // The text does not exist in the part. Create the SharedStringItem and return its index.
        SharedStringTable.AppendChild( new SharedStringItem( new DocumentFormat.OpenXml.Spreadsheet.Text( text ) ) );

        return i;
    }

    // Given a column name, a row index, and a WorksheetPart, inserts a cell into the worksheet. 
    // If the cell already exists, returns it. 
    Cell InsertCellInWorksheet( string columnName, uint rowIndex )
    {
        Worksheet worksheet = WorksheetPart!.Worksheet;
        SheetData sheetData = worksheet.GetFirstChild<SheetData>( )!;
        string cellReference = columnName + rowIndex;

        // If the worksheet does not contain a row with the specified row index, insert one.
        Row? row = sheetData.Elements<Row>( ).Where( r => r.RowIndex is not null && r.RowIndex == rowIndex ).FirstOrDefault( );

        if( row == null )
        {
            row = new Row( ) { RowIndex = rowIndex };
            sheetData.Append( row );
        }

        // If there is not a cell with the specified column name, insert one.  

        Cell? existing_cell = row.Elements<Cell>( ).Where( c => c.CellReference is not null && string.Equals( c.CellReference.Value, cellReference, StringComparison.InvariantCultureIgnoreCase ) ).FirstOrDefault( );

        if( existing_cell != null ) return existing_cell;

        // Cells must be in sequential order according to CellReference. Determine where to insert the new cell.
        Cell? refCell = null;

        foreach( Cell cell in row.Elements<Cell>( ) )
        {
            var existing_column_p = SplitCellName( cell.CellReference!.Value! );

            if( CompareColumnNames( existing_column_p.column, columnName ) > 0 )
            {
                refCell = cell;
                break;
            }
        }

        Cell newCell = new( ) { CellReference = cellReference };
        row.InsertBefore( newCell, refCell );

        return newCell;
    }

    void SetColumnWidth( uint columnIndex, double width )
    {
        SetColumnWidth( columnIndex, columnIndex, width );
    }

    void SetColumnWidth( uint columnIndexMin, uint columnIndexMax, double width )
    {
        Worksheet worksheet = WorksheetPart!.Worksheet;
        SheetData sheetData = worksheet.GetFirstChild<SheetData>( )!;

        Columns? columns = worksheet.GetFirstChild<Columns>( );

        if( columns == null )
        {
            columns = new Columns( );
            worksheet.InsertBefore( columns, sheetData );
        }

        columns.Append( new Column { Min = columnIndexMin, Max = columnIndexMax, CustomWidth = true, Width = width } );
    }

    void SetRowHeight( Cell cell, double height )
    {
        var p = SplitCellName( cell.CellReference! );

        SetRowHeight( p.row, height );
    }

    void SetRowHeight( uint rowIndex, double height )
    {
        Row row = GetRow( rowIndex );
        row.CustomHeight = true;
        row.Height = height;
    }

    void MergeExistingCells( Cell cell1, Cell cell2 )
    {
        MergeExistingCells( cell1.CellReference!, cell2.CellReference! );
    }

    void MergeExistingCells( string cell1Name, string cell2Name )
    {
        Worksheet worksheet = WorksheetPart!.Worksheet;

        MergeCells? mergeCells = worksheet.Elements<MergeCells>( ).FirstOrDefault( );

        if( mergeCells == null )
        {
            mergeCells = new MergeCells( );

            worksheet.InsertAfter( mergeCells,
                worksheet.Elements<CustomSheetView>( ).FirstOrDefault( ) ??
                worksheet.Elements<DataConsolidate>( ).FirstOrDefault( ) ??
                worksheet.Elements<SortState>( ).FirstOrDefault( ) ??
                worksheet.Elements<AutoFilter>( ).FirstOrDefault( ) ??
                worksheet.Elements<Scenarios>( ).FirstOrDefault( ) ??
                worksheet.Elements<ProtectedRanges>( ).FirstOrDefault( ) ??
                worksheet.Elements<SheetProtection>( ).FirstOrDefault( ) ??
                worksheet.Elements<SheetCalculationProperties>( ).FirstOrDefault( ) ??
                (OpenXmlElement)worksheet.Elements<SheetData>( ).First( ) );
        }

        // Create the merged cell and append it to the MergeCells collection.
        MergeCell mergeCell = new( ) { Reference = new StringValue( $"{cell1Name}:{cell2Name}" ) };
        mergeCells.Append( mergeCell );
    }

    Row GetRow( Cell cell )
    {
        var p = SplitCellName( cell.CellReference! );

        return GetRow( p.row );
    }

    Row GetRow( uint rowIndex )
    {
        Worksheet worksheet = WorksheetPart!.Worksheet;
        SheetData? sheetData = worksheet.GetFirstChild<SheetData>( );

        Row? row = sheetData!.Elements<Row>( ).Where( r => r.RowIndex is not null && r.RowIndex == rowIndex ).FirstOrDefault( );

        if( row == null ) throw new InvalidOperationException( $"Row not found: {rowIndex}" );

        return row;
    }

    static string ColumnNameFromIndex( uint columnIndex )
    {
        // https://stackoverflow.com/questions/12796973/function-to-convert-column-number-to-letter/15366979#15366979

        string result = "";
        uint n = columnIndex;

        do
        {
            uint c = ( n - 1 ) % 26;
            result = ( (char)( 'A' + c ) ) + result;
            n = ( n - c ) / 26;
        } while( n > 0 );

        return result;
    }

    static (string column, uint row) SplitCellName( string columnName )
    {
        Match m = RegexSplitColumn( ).Match( columnName );

        if( !m.Success ) throw new InvalidOperationException( $"Invalid column: '{columnName}'" );

        return (m.Groups[1].Value, Convert.ToUInt32( m.Groups[2].Value ));
    }

    static int CompareColumnNames( string name1, string name2 )
    {
        var l = name1.Length.CompareTo( name2.Length );
        if( l != 0 ) return l;

        return string.Compare( name1, name2, ignoreCase: true );
    }

    static double EvaluateWidth( string text, bool isBold )
    {
        System.Windows.Media.FormattedText ft = new(
            text,
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            new System.Windows.Media.Typeface( new System.Windows.Media.FontFamily( "Calibri" ), FontStyles.Normal, isBold ? FontWeights.Bold : FontWeights.Normal, FontStretches.Normal ),
            18,
            System.Windows.Media.Brushes.Black,
            1 );

        System.Windows.Media.FormattedText ft0 = new(
            "0",
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            new System.Windows.Media.Typeface( new System.Windows.Media.FontFamily( "Calibri" ), FontStyles.Normal, FontWeights.Normal, FontStretches.Normal ),
            18,
            System.Windows.Media.Brushes.Black,
            1 );

        // https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.spreadsheet.column
        // Truncate(({pixels}-5)/{Maximum Digit Width} * 100+0.5)/100

        return ft.WidthIncludingTrailingWhitespace / ft0.WidthIncludingTrailingWhitespace;
    }


    [GeneratedRegex( "(?i)([a-z]+)([1-9][0-9]*)", RegexOptions.None, "" )]
    private static partial Regex RegexSplitColumn( );

}


class EngineData
{
    public required IRegexEngine Engine { get; init; }
    public required (uint index, string? variantName, FeatureMatrix fm)[] Matrices { get; init; }
}