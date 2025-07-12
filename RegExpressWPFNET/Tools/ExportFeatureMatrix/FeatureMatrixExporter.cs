using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
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
            // create workbook

            WorkbookPart workbookPart = spreadSheet.WorkbookPart ?? spreadSheet.AddWorkbookPart( );
            Workbook workbook = new( );
            workbookPart.Workbook = workbook;

            // create string table

            SharedStringTablePart string_table_part = workbookPart.GetPartsOfType<SharedStringTablePart>( ).FirstOrDefault( ) ?? workbookPart.AddNewPart<SharedStringTablePart>( );
            string_table_part.SharedStringTable ??= new SharedStringTable( );
            SharedStringTable = string_table_part.SharedStringTable;

            // create worksheet

            WorksheetPart = workbookPart.AddNewPart<WorksheetPart>( );
            Worksheet worksheet = new( new SheetData( ) );
            WorksheetPart.Worksheet = worksheet;

            Sheets sheets = workbookPart.Workbook.GetFirstChild<Sheets>( ) ?? workbookPart.Workbook.AppendChild( new Sheets( ) );
            string relationshipId = workbookPart.GetIdOfPart( WorksheetPart );

            uint sheetId = 1;
            //if( sheets.Elements<Sheet>( ).Count( ) > 0 )
            //{
            //    sheetId = sheets.Elements<Sheet>( ).Select<Sheet, uint>( s =>
            //    {
            //        if( s.SheetId is not null && s.SheetId.HasValue )
            //        {
            //            return s.SheetId.Value;
            //        }

            //        return 0;
            //    } ).Max( ) + 1;
            //}

            //string sheetName = "Sheet" + sheetId;
            string sheetName = "Feature Matrix";

            Sheet sheet = new( ) { Id = relationshipId, SheetId = sheetId, Name = sheetName };
            sheets.Append( sheet );

            // get feature matrices from engines

            IRegexEngine[] engines = [.. plugins.SelectMany( p => p.GetEngines( ) )];

            IReadOnlyList<(string? variantName, FeatureMatrix fm)>[] all_matrices = [.. engines.Select( e => e.GetFeatureMatrices( ) )];

            uint variant_index = 0;
            EngineData[] engines_data =
                [.. plugins
                    .SelectMany( p => p.GetEngines( ) )
                    .Select( e => new EngineData { Engine = e, Matrices = [.. e.GetFeatureMatrices( ).Select( m => (index:variant_index++, m.variantName, m.fm) )] } ).Where( d => d.Matrices.Length > 0 )];

            int total_variants = engines_data.Select( d => d.Matrices.Length ).Sum( );

            // caption

            Cell cell1 = SetCell( "A", 1, "Regex Feature Matrix" );
            Cell cell2 = SetCell( ColumnNameFromIndex( (uint)total_variants + 2 ), 1, "" );
            MergeTwoExistingCells( cell1.CellReference!, cell2.CellReference! );

            // table header

            const int START_TABLE_COLUMN = 1;
            const int START_ENGINES_COLUMN = START_TABLE_COLUMN + 2;
            const int START_TABLE_ROW = 3;

            SetCell( "A", START_TABLE_ROW, "Feature" );
            SetCell( "B", START_TABLE_ROW, "Description" );

            uint row = START_TABLE_ROW;

            foreach( EngineData engine_data in engines_data )
            {
                SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + engine_data.Matrices[0].index ), row, engine_data.Engine.Name );
            }

            ++row;

            foreach( EngineData engine_data in engines_data )
            {
                foreach( var m in engine_data.Matrices )
                {
                    SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + m.index ), row, m.variantName ?? "" );
                }
            }

            // body

            ++row;

            foreach( FeatureMatrixDetails details in FeatureMatrixDetails.AllFeatureMatrixDetails )
            {
                if( details.Func == null )
                {
                    SetCell( ColumnNameFromIndex( START_TABLE_COLUMN ), row, details.ShortDesc );
                }
                else
                {
                    SetCell( ColumnNameFromIndex( START_TABLE_COLUMN ), row, details.ShortDesc );
                    SetCell( ColumnNameFromIndex( START_TABLE_COLUMN + 1 ), row, details.Desc ?? "" );

                    foreach( EngineData engine_data in engines_data )
                    {
                        foreach( var m in engine_data.Matrices )
                        {
                            SetCell( ColumnNameFromIndex( START_ENGINES_COLUMN + m.index ), row, details.Func( m.fm ) ? "+" : "" );
                        }
                    }
                }

                ++row;
            }
        }
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
        SheetData? sheetData = worksheet.GetFirstChild<SheetData>( );
        string cellReference = columnName + rowIndex;

        // If the worksheet does not contain a row with the specified row index, insert one.
        Row row;

        if( sheetData?.Elements<Row>( ).Where( r => r.RowIndex is not null && r.RowIndex == rowIndex ).Count( ) != 0 )
        {
            row = sheetData!.Elements<Row>( ).Where( r => r.RowIndex is not null && r.RowIndex == rowIndex ).First( );
        }
        else
        {
            row = new Row( ) { RowIndex = rowIndex };
            sheetData.Append( row );
        }

        // If there is not a cell with the specified column name, insert one.  

        Cell? existing_cell = row.Elements<Cell>( ).Where( c => c.CellReference is not null && c.CellReference.Value == cellReference ).FirstOrDefault( );

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

    void MergeTwoExistingCells( string cell1Name, string cell2Name )
    {
        Worksheet worksheet = WorksheetPart!.Worksheet;

        MergeCells mergeCells;

        if( worksheet.Elements<MergeCells>( ).Count( ) > 0 )
        {
            mergeCells = worksheet.Elements<MergeCells>( ).First( );
        }
        else
        {
            mergeCells = new MergeCells( );

            // Insert a MergeCells object into the specified position.
            if( worksheet.Elements<CustomSheetView>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<CustomSheetView>( ).First( ) );
            }
            else if( worksheet.Elements<DataConsolidate>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<DataConsolidate>( ).First( ) );
            }
            else if( worksheet.Elements<SortState>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<SortState>( ).First( ) );
            }
            else if( worksheet.Elements<AutoFilter>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<AutoFilter>( ).First( ) );
            }
            else if( worksheet.Elements<Scenarios>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<Scenarios>( ).First( ) );
            }
            else if( worksheet.Elements<ProtectedRanges>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<ProtectedRanges>( ).First( ) );
            }
            else if( worksheet.Elements<SheetProtection>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<SheetProtection>( ).First( ) );
            }
            else if( worksheet.Elements<SheetCalculationProperties>( ).Count( ) > 0 )
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<SheetCalculationProperties>( ).First( ) );
            }
            else
            {
                worksheet.InsertAfter( mergeCells, worksheet.Elements<SheetData>( ).First( ) );
            }
        }

        // Create the merged cell and append it to the MergeCells collection.
        MergeCell mergeCell = new MergeCell( ) { Reference = new StringValue( cell1Name + ":" + cell2Name ) };
        mergeCells.Append( mergeCell );
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


    [GeneratedRegex( "(?i)([a-z]+)([1-9][0-9]*)", RegexOptions.None, "" )]
    private static partial Regex RegexSplitColumn( );
}


class EngineData
{
    public required IRegexEngine Engine { get; init; }
    public required (uint index, string? variantName, FeatureMatrix fm)[] Matrices { get; init; }
}