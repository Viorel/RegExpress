#include "stdafx.h"

void DoWork( )
{
	try
	{
#if 1
		QTextStream ts( stdin );
		QString input_text = ts.readAll( );

		QJsonParseError error;
		QJsonDocument json_doc = QJsonDocument::fromJson( input_text.toUtf8( ), &error );

		if( error.error != QJsonParseError::NoError )
		{
			throw std::runtime_error( "Invalid JSON at offset " + std::to_string( error.offset ) + ": " + error.errorString( ).toUtf8( ) );
		}

		QJsonObject obj = json_doc.object( );

		QString pattern = obj.value( "pattern" ).toString( );
		QString text = obj.value( "text" ).toString( );
		QJsonObject flags = obj.value( "options" ).toObject( );
#else
		QString pattern = QString::fromStdWString( L"(.)(?<n>.)?" );
		QString text = QString::fromStdWString( L"abc" );
		QString flags = QString::fromStdWString( L"" );
#endif

		QRegularExpression::PatternOptions options = QRegularExpression::PatternOption::NoPatternOption;

		if( flags.value( "CaseInsensitiveOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::CaseInsensitiveOption;
		if( flags.value( "DotMatchesEverythingOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::DotMatchesEverythingOption;
		if( flags.value( "MultilineOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::MultilineOption;
		if( flags.value( "ExtendedPatternSyntaxOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::ExtendedPatternSyntaxOption;
		if( flags.value( "InvertedGreedinessOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::InvertedGreedinessOption;
		if( flags.value( "DontCaptureOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::DontCaptureOption;
		if( flags.value( "UseUnicodePropertiesOption" ).toBool( ) ) options |= QRegularExpression::PatternOption::UseUnicodePropertiesOption;

		QRegularExpression re( pattern, options );

		if( !re.isValid( ) )
		{
			throw std::runtime_error( "Invalid pattern at offset " + std::to_string( re.patternErrorOffset( ) ) + ": " + re.errorString( ).toUtf8( ) );
		}

		int capture_count = re.captureCount( );
		QStringList names = re.namedCaptureGroups( );

		for( int i = 1; i < names.count( ); ++i ) // (ignore default group)
		{
			QJsonValue jv = names[i];

			std::cout << "n " << QString( jv.toJson( ) ).toStdString( ) << std::endl;
		}

		QRegularExpressionMatchIterator iter = re.globalMatchView( text );

		while( iter.hasNext( ) )
		{
			QRegularExpressionMatch match = iter.next( );

			std::cout << "M " << match.capturedStart( ) << ' ' << match.capturedLength( ) << std::endl;

			for( int i = 1; i <= capture_count; ++i ) // (ignore default group)
			{
				if( !match.hasCaptured( i ) )
				{
					std::cout << "g -1 -1" << std::endl;
				}
				else
				{
					std::cout << "g " << match.capturedStart( i ) << ' ' << match.capturedLength( i ) << std::endl;
				}
			}
		}
	}
	catch( const QException& exc )
	{
		std::cerr << exc.what( ) << std::endl;
	}
	catch( const std::exception& exc )
	{
		std::cerr << exc.what( ) << std::endl;
	}
	catch( ... )
	{
		std::cerr << L"Unknown error" << std::endl;
	}
}

int main( int argc, char* argv[] )
{
	__try
	{
		DoWork( );
	}
	__except( EXCEPTION_EXECUTE_HANDLER )
	{
		DWORD code = ::GetExceptionCode( );

		std::cerr << "SEH error code: " << code << std::endl;
	}
}
