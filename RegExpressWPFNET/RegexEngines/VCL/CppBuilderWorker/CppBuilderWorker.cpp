
static void FindPossibleNames( std::set<UnicodeString>* set, UnicodeString pattern )
{
    TRegExOptions options{};
    TRegEx regex( LR"REGEX(\(\s*\?\s*<\s*(?![=!])(?<n>.*?)\s*>)REGEX", options );

    TMatchCollection matches = regex.Matches( pattern );

    for( int i = 0; i < matches.Count; ++i )
    {
        TMatch match = matches[i];

        set->insert( match.Groups["n"].Value );
    }
}

static void DoWork( )
{
    try
    {
        //UnicodeString input_json = R"({ "pattern": ".", "text": "abc" })";
        //TStringReader* string_reader = new TStringReader(input_json);
        //TJsonTextReader* json_reader = new TJsonTextReader(string_reader);
#if 1
        THandleStream* stdin_stream = new THandleStream( THandle( GetStdHandle( STD_INPUT_HANDLE ) ) );
        TStreamReader* stream_reader = new TStreamReader( stdin_stream );
        UnicodeString input_string = stream_reader->ReadToEnd( );
#else
        UnicodeString input_string = LR"JSON({ "pattern" : ".", "text" : "abc", "flags": "g" })JSON";
#endif

        //std::wcout << input_string << std::endl;

        TJSONObject* json_object = new TJSONObject( );

        int e = json_object->Parse( input_string.BytesOf( ), 0 );
        if( e < 0 ) throw Exception( "Invalid input: '" + input_string + "'" );

        TJSONValue* value;

        value = json_object->GetValue( "pattern" );
        if( value == nullptr ) throw std::runtime_error( "No pattern" );
        UnicodeString pattern = value->Value( );

        value = json_object->GetValue( "text" );
        if( value == nullptr ) throw std::runtime_error( "No text" );
        UnicodeString text = value->Value( );

        value = json_object->GetValue( "flags" );
        UnicodeString flags = value == nullptr ? L"" : value->Value( );

        //std::wcout << "pattern: " << pattern << std::endl;
        //std::wcout << "text: " << text << std::endl;
        //std::wcout << "flags: " << flags << std::endl;

        TRegExOptions options;
        if( std::find( flags.cbegin( ), flags.cend( ), L'i' ) != flags.cend( ) ) options << TRegExOption::roIgnoreCase;
        if( std::find( flags.cbegin( ), flags.cend( ), L'm' ) != flags.cend( ) ) options << TRegExOption::roMultiLine;
        if( std::find( flags.cbegin( ), flags.cend( ), L'n' ) != flags.cend( ) ) options << TRegExOption::roExplicitCapture;
        if( std::find( flags.cbegin( ), flags.cend( ), L'C' ) != flags.cend( ) ) options << TRegExOption::roCompiled;
        if( std::find( flags.cbegin( ), flags.cend( ), L's' ) != flags.cend( ) ) options << TRegExOption::roSingleLine;
        if( std::find( flags.cbegin( ), flags.cend( ), L'x' ) != flags.cend( ) ) options << TRegExOption::roIgnorePatternSpace;
        if( std::find( flags.cbegin( ), flags.cend( ), L'N' ) != flags.cend( ) ) options << TRegExOption::roNotEmpty;

        std::set<UnicodeString> possible_names;
        FindPossibleNames( &possible_names, pattern );

        TRegEx regex( pattern, options );

        TMatchCollection matches = regex.Matches( text );

        for( int i = 0; i < matches.Count; ++i )
        {
            TMatch match = matches[i];

            std::wcout << L"M " << match.Index << L" " << match.Length << L"\n"; // (starting at 1)

            TGroupCollection groups = match.Groups;

            for( int j = 1; j < groups.Count; ++j ) // (the default group 0 is ignored)
            {
                System::Regularexpressions::TGroup group = groups[j];

                if( !group.Success )
                {
                    std::wcout << L"g -1 -1\n";
                }
                else
                {
                    std::wcout << L"g " << group.Index << L" " << group.Length; // (starting at 1)

                    // try to find the possible name
                    for( const UnicodeString& name : possible_names )
                    {
                        System::Regularexpressions::TGroup found_group;
                        if( groups.TryGetNamedGroup( name, found_group ) )
                        {
                            if( found_group.Index == group.Index && found_group.Length == group.Length )
                            {
                                TJSONString* js = new TJSONString( name );
                                std::wcout << " " << js->ToJSON( );
                            }
                        }
                    }

                    std::wcout << L"\n";
                }
            }
        }
    }
    catch( const Exception& exc )
    {
        std::wcerr << exc.Message << L"\n";
    }
    catch( const std::exception& exc )
    {
        std::wcerr << exc.what( ) << L"\n";
    }
    catch( ... )
    {
        std::wcerr << L"Unknown error\n";
    }
}

int WINAPI _tWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow )
{
    __try
    {
        DoWork( );
    }
    __except( EXCEPTION_EXECUTE_HANDLER )
    {
        std::wcerr << L"SEH Exception caught: Access violation occurred!\n";
    }
}

