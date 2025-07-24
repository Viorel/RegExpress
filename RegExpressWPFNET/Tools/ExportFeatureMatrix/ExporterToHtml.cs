using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

class ExporterToHtml
{
    public void Export( string path, IReadOnlyList<RegexPlugin> plugins )
    {
        using( var xw = XmlWriter.Create( path, new XmlWriterSettings { CloseOutput = true, Indent = true, OmitXmlDeclaration = true } ) )
        {
            Export( xw, plugins );
        }
    }

    public void Export( XmlWriter xw, IReadOnlyList<RegexPlugin> plugins )
    {
        IRegexEngine[] engines = [.. plugins.SelectMany( p => p.GetEngines( ) )];
        IReadOnlyList<FeatureMatrixVariant>[] all_matrices = [.. engines.Select( e => e.GetFeatureMatrices( ) )];

        xw.WriteStartElement( "html" );

        xw.WriteStartElement( "head" );

        xw.WriteElementString( "title", "Regex Feature Matrix" );

        xw.WriteRaw( @"
<style>

h1
{
    font-family: sans-serif;
}

table
{
    border-collapse: collapse;
    font-family: 'Helvetica Narrow','Arial Narrow',Tahoma,Arial,Helvetica,sans-serif;
    font-size: 12pt;
}

th, td
{
    padding: 2pt;
    border: 0.5pt solid black;
}

th
{
    white-space: nowrap;
}

tbody > tr:nth-child(2n+2)
{
	background: #F4F4F4;
}

tbody > tr > th[colspan='100%']
{
    text-align: left;
	background: #FFF8DC;
    padding: 0.4ch 1ch 0.5ch 8pt ;
}

tbody > tr > td
{
    text-align: center;
    white-space: nowrap;
}

tbody > tr > td:nth-child(1)
{
    text-align: left;
    font-family: monospace;
    padding-left: 8pt;
}

tbody > tr > td:nth-child(2)
{
    text-align: left;
    white-space: normal;
    min-width: 30ch;
}

</style>
" );
        xw.WriteEndElement( ); // </head>

        xw.WriteStartElement( "body" );

        xw.WriteElementString( "h1", "Regex Feature Matrix" );

        xw.WriteStartElement( "table" );

        // header
        xw.WriteStartElement( "thead" );
        {
            xw.WriteStartElement( "tr" );
            {
                xw.WriteStartElement( "th" );
                xw.WriteAttributeString( "rowspan", "2" );
                xw.WriteString( "Feature" );
                xw.WriteEndElement( ); // </th>

                xw.WriteStartElement( "th" );
                xw.WriteAttributeString( "rowspan", "2" );
                xw.WriteString( "Description" );
                xw.WriteEndElement( ); // </th>

                int i = 0;
                foreach( IRegexEngine engine in engines )
                {
                    var fms = all_matrices[i];
                    if( fms == null ) continue;

                    xw.WriteStartElement( "th" );
                    if( fms.Count == 1 && string.IsNullOrWhiteSpace( fms[0].Name ) )
                    {
                        xw.WriteAttributeString( "rowspan", "2" );
                    }
                    else
                    {
                        xw.WriteAttributeString( "colspan", fms.Count.ToString( CultureInfo.InvariantCulture ) );
                    }
                    xw.WriteString( engine.Name );
                    xw.WriteElementString( "br", null );
                    xw.WriteString( engine.Version );
                    xw.WriteEndElement( ); // </th>

                    ++i;
                }
            }
            xw.WriteEndElement( ); // </tr>
            xw.WriteStartElement( "tr" );
            {
                int i = 0;
                foreach( IRegexEngine engine in engines )
                {
                    var fms = all_matrices[i];
                    if( fms == null ) continue;

                    foreach( var p in fms )
                    {
                        if( !string.IsNullOrWhiteSpace( p.Name ) )
                        {
                            xw.WriteStartElement( "th" );
                            xw.WriteString( p.Name );
                            xw.WriteEndElement( ); // </th>
                        }
                    }

                    ++i;
                }
            }
            xw.WriteEndElement( ); // </tr>
        }
        xw.WriteEndElement( ); // </thead>

        // body
        xw.WriteStartElement( "tbody" );
        {
            foreach( var d in FeatureMatrixDetails.AllFeatureMatrixDetails )
            {
                if( d.Func == null )
                {
                    xw.WriteEndElement( ); // </tbody>
                    xw.WriteStartElement( "tbody" );

                    xw.WriteStartElement( "tr" );
                    {
                        xw.WriteStartElement( "th" );
                        xw.WriteAttributeString( "colspan", "100%" );
                        xw.WriteValue( d.ShortDesc );
                        xw.WriteEndElement( ); // </th>
                    }
                    xw.WriteEndElement( ); // </tr>
                }
                else
                {
                    WriteRow( xw, d.ShortDesc, d.Desc, engines, all_matrices, d.Func );
                }
            }
        }
        xw.WriteEndElement( ); // </tbody>

        xw.WriteEndElement( ); // </table>

        xw.WriteElementString( "br", null );
        xw.WriteElementString( "br", null );

        xw.WriteEndElement( ); // </body>
        xw.WriteEndElement( ); // </html>
    }


    static void WriteRow( XmlWriter xw, string shortDesc, string? desc,
        IEnumerable<IRegexEngine> engines, IReadOnlyList<FeatureMatrixVariant>[] allMatrices,
        Func<FeatureMatrix, bool> func )
    {
        xw.WriteStartElement( "tr" );
        {
            xw.WriteElementString( "td", shortDesc );
            xw.WriteElementString( "td", desc );

            int i = 0;
            foreach( IRegexEngine engine in engines )
            {
                var fms = allMatrices[i];
                if( fms == null ) continue;

                foreach( var p in fms )
                {
                    xw.WriteStartElement( "td" );
                    if( func( p.FeatureMatrix ) )
                    {
                        xw.WriteString( "+" );
                    }
                    else
                    {
                        xw.WriteElementString( "br", null );
                    }
                    xw.WriteEndElement( ); // </td>
                }

                ++i;
            }
        }
        xw.WriteEndElement( ); // </tr>
    }

}
