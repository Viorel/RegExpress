//
// Based on “Get started with WebView2 in Win32 apps” (https://learn.microsoft.com/en-us/microsoft-edge/webview2/get-started/win32).
//


// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include "RegExpressCppLibraryPCH.h"

#include <locale>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include <wrl.h>
#include <wil/com.h>
// <IncludeHeader>
// include WebView2 header
#include "WebView2.h"
// </IncludeHeader>
#include "BinaryReader.h"
#include "StreamWriter.h"
#include "Convert.h"

using namespace Microsoft::WRL;

// Global variables

// The main window class name.
static TCHAR szWindowClass[] = _T( "RegExpressWebView2Worker" );

// The string that appears in the application's title bar.
static TCHAR szTitle[] = _T( "RegExpressWebView2Worker" );

HINSTANCE hInst;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc( HWND, UINT, WPARAM, LPARAM );

// Pointer to WebViewController
static wil::com_ptr<ICoreWebView2Controller> webviewController;

// Pointer to WebView window
static wil::com_ptr<ICoreWebView2> webview;


int DoGetVersion( StreamWriterW& outwr, StreamWriterW& errwr );
int DoMatch( StreamWriterW& outwr, StreamWriterW& errwr, HINSTANCE hInstance, LPCWSTR pattern, LPCWSTR flags, LPCWSTR text );


int CALLBACK WinMain(
    _In_ HINSTANCE hInstance,
    _In_ HINSTANCE hPrevInstance,
    _In_ LPSTR     lpCmdLine,
    _In_ int       nCmdShow
)
{
    auto herr = GetStdHandle( STD_ERROR_HANDLE );
    if( herr == INVALID_HANDLE_VALUE )
    {
        auto lerr = GetLastError( );

        return 1;
    }

    StreamWriterW errwr( herr );

    auto hin = GetStdHandle( STD_INPUT_HANDLE );
    if( hin == INVALID_HANDLE_VALUE )
    {
        errwr.WriteString( L"Cannot get STDIN" );

        return 2;
    }

    auto hout = GetStdHandle( STD_OUTPUT_HANDLE );
    if( hout == INVALID_HANDLE_VALUE )
    {
        errwr.WriteString( L"Cannot get STDOUT" );

        return 3;
    }

    try
    {
        HRESULT hr = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );

        if( hr != S_OK && hr != S_FALSE )
        {
            errwr.WriteString( L"CoInitializeEx failed" );

            return 1;
        }

        LPCWSTR command_line = GetCommandLineW( );
        int argc = 0;

        LPWSTR* argv = CommandLineToArgvW( command_line, &argc );

        if( argv == NULL )
        {
            errwr.WriteStringF( L"Failed to parse command line: '{}'\r\n", command_line );

            return 1;
        }

        if( argc < 2 )
        {
            errwr.WriteStringF( L"Invalid command line: '{}'\r\n", command_line );

            return 1;
        }

        if( lstrcmpiW( argv[1], L"a" ) == 0 ) // "a" -- return arguments to STDERR (for testing)
        {
            errwr.WriteStringF( L"Command line: '{}'\r\n", command_line );

            for( int i = 0; i < argc; ++i )
            {
                errwr.WriteStringF( L"{}: '{}'\r\n", i, argv[i] );
            }

            return 0;
        }

        StreamWriterW outwr( hout );


        if( lstrcmpiW( argv[1], L"v" ) == 0 ) // "v" -- get version
        {
            return DoGetVersion( outwr, errwr );
        }

        if( lstrcmpiW( argv[1], L"m" ) == 0 ) // "m" -- get matches, from command line: 'm "pattern" "flags" "text"'
        {
            if( argc < 5 )
            {
                errwr.WriteStringF( L"Invalid command line: '{}'\r\n", command_line );

                return 1;
            }

            return DoMatch( outwr, errwr, hInst, argv[2], argv[3], argv[4] );
        }

        if( lstrcmpiW( argv[1], L"b" ) == 0 ) // "b" -- get matches; get data from binary stream
        {
            BinaryReaderW inbr( hin );

            if( inbr.ReadByte( ) != 'b' ) throw std::runtime_error( "Invalid data [1]." );

            auto pattern_js = inbr.ReadString( ); // JavaScript-encodded
            auto text_js = inbr.ReadString( ); // JavaScript-encodded
            auto flags = inbr.ReadString( );

            if( inbr.ReadByte( ) != 'e' ) throw std::runtime_error( "Invalid data [2]." );

            return DoMatch( outwr, errwr, hInst, pattern_js.c_str( ), flags.c_str( ), text_js.c_str( ) );
        }

        errwr.WriteStringF( L"Invalid command line: '{}'\r\n", command_line );
    }
    catch( const std::exception& exc )
    {
        errwr.WriteString( ToWString( exc.what( ) ) );

        return 14;
    }
    catch( ... )
    {
        errwr.WriteString( L"Internal error" );

        return 215;
    }

    return 101;


#if 0

    std::wstring stdin_contents;

    if( lstrcmpiW( argv[1], L"i" ) == 0 ) // "i" -- get data from STDIN instead of command-line arguments
    {
        std::getline( std::wcin, stdin_contents, L'\r' );

        stdin_contents = L"\"" + ( argv[0] + ( L"\" " + stdin_contents ) );

        command_line = stdin_contents.c_str( );
        argv = CommandLineToArgvW( command_line, &argc );
    }

    if( lstrcmpiW( argv[1], L"m" ) == 0 ) // "m" -- get matches: 'm "pattern" "flags" "text"'
    {
        if( argc < 5 )
        {
            std::wcerr << L"Invalid command line: '" << command_line << "'." << std::endl;

            return 1;
        }

        return DoMatch( hInst, argv[2], argv[3], argv[4] );
    }

    std::wcerr << L"Invalid command line: '" << command_line << "'." << std::endl;

    return 1;
#endif
}


int DoGetVersion( StreamWriterW& outwr, StreamWriterW& errwr )
{
    PWSTR v;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString( NULL, &v );

    if( hr != S_OK )
    {
        errwr.WriteString( L"Failed to get version\r\n" );

        return 1;
    }

    outwr.WriteStringF( L"{{ \"Version\": \"{}\" }}", v );

    return 0;
}


int DoMatch( StreamWriterW& outwr, StreamWriterW& errwr, HINSTANCE hInstance, LPCWSTR patternJs, LPCWSTR flags, LPCWSTR textJs )
{
    int exit_code = 0;

    WNDCLASSEX wcex = { 0 };

    wcex.cbSize = sizeof( WNDCLASSEX );
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    //wcex.cbClsExtra = 0;
    //wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    //wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    //wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)( COLOR_WINDOW + 1 );
    //wcex.lpszMenuName = NULL;
    wcex.lpszClassName = szWindowClass;
    //wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    if( !RegisterClassEx( &wcex ) )
    {
        errwr.WriteString( L"RegisterClassEx failed" );
        exit_code = 1;

        return exit_code;
    }

    // Store instance handle in our global variable
    hInst = hInstance;

    // The parameters to CreateWindow explained:
    // szWindowClass: the name of the application
    // szTitle: the text that appears in the title bar
    // WS_OVERLAPPEDWINDOW: the type of window to create
    // CW_USEDEFAULT, CW_USEDEFAULT: initial position (x, y)
    // 500, 100: initial size (width, length)
    // NULL: the parent of this window
    // NULL: this application does not have a menu bar
    // hInstance: the first parameter from WinMain
    // NULL: not used in this application
    HWND hWnd = CreateWindow(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 900,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if( !hWnd )
    {
        errwr.WriteString( L"CreateWindow failed\r\n" );
        exit_code = 1;

        return exit_code;
    }

    // The parameters to ShowWindow explained:
    // hWnd: the value returned from CreateWindow
    // nCmdShow: the fourth parameter from WinMain
    //ShowWindow(hWnd, nCmdShow);
    //UpdateWindow(hWnd);

    ShowWindow( hWnd, SW_HIDE );

    // <-- WebView2 sample code starts here -->
    // Step 3 - Create a single WebView within the parent window
    // Locate the browser and set up the environment for WebView
    CreateCoreWebView2EnvironmentWithOptions( nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&outwr, &errwr, hWnd, patternJs, flags, textJs, &exit_code]( HRESULT result, ICoreWebView2Environment* env ) -> HRESULT
            {
                if( result != S_OK )
                {
                    errwr.WriteString( L"CreateCoreWebView2EnvironmentWithOptions failed\r\n" );
                    exit_code = 1;
                    DestroyWindow( hWnd );

                    return S_FALSE;
                }

                if( !env )
                {
                    errwr.WriteString( L"env is null\r\n" );
                    exit_code = 1;
                    DestroyWindow( hWnd );

                    return S_FALSE;
                }

                // Create a CoreWebView2Controller and get the associated CoreWebView2 whose parent is the main window hWnd
                env->CreateCoreWebView2Controller( hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [&outwr, &errwr, hWnd, patternJs, flags, textJs, &exit_code]( HRESULT result, ICoreWebView2Controller* controller ) -> HRESULT
                    {
                        if( result != S_OK )
                        {
                            errwr.WriteString( L"CreateCoreWebView2Controller failed\r\n" );
                            exit_code = 1;
                            DestroyWindow( hWnd );

                            return S_FALSE;
                        }

                        if( !controller )
                        {
                            errwr.WriteString( L"controller is null\r\n" );
                            exit_code = 1;
                            DestroyWindow( hWnd );

                            return S_FALSE;
                        }

                        webviewController = controller;
                        webviewController->get_CoreWebView2( &webview );

                        // Add a few settings for the webview
                        // The demo step is redundant since the values are the default settings
                        wil::com_ptr<ICoreWebView2Settings> settings;
                        webview->get_Settings( &settings );
                        settings->put_IsScriptEnabled( TRUE );
                        settings->put_AreDefaultScriptDialogsEnabled( TRUE );
                        settings->put_IsWebMessageEnabled( TRUE );

                        // Resize WebView to fit the bounds of the parent window
                        RECT bounds;
                        GetClientRect( hWnd, &bounds );
                        webviewController->put_Bounds( bounds );

                        // Schedule an async task to navigate to Bing
                        //webview->Navigate(L"https://www.bing.com/");

                        // Step 4 - Navigation events

                        // Step 5 - Scripting

                        // Step 6 - Communication between host and web content


                        // My

                        std::wstring flags_adjusted = std::wstring( flags );

                        bool use_exec = std::erase( flags_adjusted, L'E' ) != 0;

                        flags_adjusted += L"d"; // to generates start and end indices for matches

                        std::wstring script;

#define EOL L"\r\n"

                        if( use_exec )
                        {
                            // 'RegExp.prototype.exec' function

                            script =
                                std::wstring( ) +
                                L"( function() " EOL
                                L"{ " EOL
                                L" try " EOL
                                L" { " EOL
                                L"  let pattern = \"" + patternJs + L"\";" EOL
                                L"  let text = \"" + textJs + L"\";" EOL
                                L"  let re = new RegExp(pattern, \"" + flags_adjusted + L"\"); " EOL
                                L"  let r = [ ]; let m; let l = -2;" EOL
                                L"  while( (m = re.exec(text)) !== null) " EOL
                                L"  { " EOL
                                L"   if( l == re.lastIndex ) break; else l = re.lastIndex; " EOL
                                L"   r.push( { i: m.indices, g: m.indices.groups } );" EOL
                                L"  } " EOL
                                L"  return { \"Matches\": r }; " EOL
                                L" } " EOL
                                L" catch( err ) " EOL
                                L" { " EOL
                                L"  return { \"Error\": err.message }" EOL
                                L" } " EOL
                                L"} )()";
                        }
                        else
                        {
                            // 'String.prototype.matchAll' function:

                            script =
                                std::wstring( ) +
                                L"( function() " EOL
                                L"{ " EOL
                                L" try " EOL
                                L" { " EOL
                                L"  let pattern = \"" + patternJs + L"\";" EOL
                                L"  let text = \"" + textJs + L"\";" EOL
                                L"  let re = new RegExp(pattern, \"" + flags_adjusted + L"\"); " EOL
                                L"  let r = [ ]; " EOL
                                L"  for( const m of text.matchAll( re ) ) " EOL
                                L"  { " EOL
                                L"   r.push( { i: m.indices, g: m.indices.groups } );" EOL
                                L"  } " EOL
                                L"  return { \"Matches\": r }; " EOL
                                L" } " EOL
                                L" catch( err ) " EOL
                                L" { " EOL
                                L"  return { \"Error\": err.message }" EOL
                                L" } " EOL
                                L"} )()";
                        }

                        webview->ExecuteScript( script.c_str( ),
                            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                [&outwr, &errwr, hWnd, &exit_code]( HRESULT errorCode, LPCWSTR resultObjectAsJson ) -> HRESULT
                                {
                                    if( errorCode != S_OK )
                                    {
                                        errwr.WriteString( L"JavaScript failed\r\n" );

                                        exit_code = 1;
                                        DestroyWindow( hWnd );

                                        return S_FALSE;
                                    }

                                    LPCWSTR json = resultObjectAsJson;
                                    //MessageBox( hWnd, json, L"Result", MB_OKCANCEL );

                                    outwr.WriteString( json );
                                    outwr.WriteString( EOL );

                                    DestroyWindow( hWnd );

                                    return S_OK;
                                } ).Get( ) );

                        return S_OK;
                    } ).Get( ) );
                return S_OK;
            } ).Get( ) );

    // <-- WebView2 sample code ends here -->

    // Main message loop:
    MSG msg;
    while( GetMessage( &msg, NULL, 0, 0 ) )
    {
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }

    return (int)msg.wParam;
}


//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_DESTROY  - post a quit message and return
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch( message )
    {
    case WM_SIZE:
        if( webviewController != nullptr )
        {
            RECT bounds;
            GetClientRect( hWnd, &bounds );
            webviewController->put_Bounds( bounds );
        };
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        break;
    default:
        return DefWindowProc( hWnd, message, wParam, lParam );
        break;
    }

    return 0;
}
