//
// Based on “Get started with WebView2 in Win32 apps” (https://learn.microsoft.com/en-us/microsoft-edge/webview2/get-started/win32).
//


// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <iostream>

//#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include <wrl.h>
#include <wil/com.h>
// <IncludeHeader>
// include WebView2 header
#include "WebView2.h"
// </IncludeHeader>
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


int DoGetVersion( );
int DoMatch( HINSTANCE hInstance, LPCWSTR pattern, LPCWSTR flags, LPCWSTR text );


int CALLBACK WinMain(
    _In_ HINSTANCE hInstance,
    _In_ HINSTANCE hPrevInstance,
    _In_ LPSTR     lpCmdLine,
    _In_ int       nCmdShow
)
{
    //AttachConsole( ATTACH_PARENT_PROCESS ); // (does not seem to have effect)

    setlocale( LC_ALL, ".utf8" ); // this seems to be enough

    //SetConsoleCP( CP_UTF8 );
    //SetConsoleOutputCP( CP_UTF8 );

    //std::locale utf8_locale( "en_US.UTF-8" );
    //std::wcin.imbue( utf8_locale );
    //std::wcout.imbue( utf8_locale );
    //std::wcerr.imbue( utf8_locale );


    HRESULT hr = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );

    if( hr != S_OK && hr != S_FALSE )
    {
        std::cerr << "CoInitializeEx failed" << std::endl;

        return 1;
    }

    LPCWSTR command_line = GetCommandLineW( );
    int argc = 0;

    LPWSTR* argv = CommandLineToArgvW( command_line, &argc );

    if( argv == NULL )
    {
        std::wcerr << L"Failed to parse command line: '" << command_line << "'." << std::endl;

        return 1;
    }

    if( argc < 2 )
    {
        std::wcerr << L"Invalid command line: '" << command_line << "'." << std::endl;

        return 1;
    }


    if( lstrcmpiW( argv[1], L"a" ) == 0 ) // "a" -- return arguments to STDERR (for testing)
    {
        std::wcerr << L"Command line: '" << command_line << "'" << std::endl;

        for( int i = 0; i < argc; ++i )
        {
            std::wcerr << i << ": '" << argv[i] << "'" << std::endl;
        }

        return 0;
    }

    if( lstrcmpiW( argv[1], L"t" ) == 0 ) // "t" -- return arguments and STDIN contents to STDERR (for testing)
    {
        std::wcerr << L"Command line: '" << command_line << "'" << std::endl;

        for( int i = 0; i < argc; ++i )
        {
            std::wcerr << i << ": '" << argv[i] << "'" << std::endl;
        }

        std::wstring stdin_contents;
        std::getline( std::wcin, stdin_contents, L'\r' );

        //MessageBox( NULL, stdin_contents.c_str( ), L"STDIN", MB_OK );

        std::wcerr << L"STDIN: '" << stdin_contents << "'" << std::endl;

        return 0;
    }


    if( lstrcmpiW( argv[1], L"v" ) == 0 ) // "v" -- get version
    {
        return DoGetVersion( );
    }


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
}


int DoGetVersion( )
{
    PWSTR v;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString( NULL, &v );

    if( hr != S_OK )
    {
        std::wcerr << L"Failed to get version" << std::endl;

        return 1;
    }

    std::wcout << L"{ \"Version\": \"" << v << "\" }" << std::endl;

    return 0;
}


int DoMatch( HINSTANCE hInstance, LPCWSTR pattern, LPCWSTR flags, LPCWSTR text )
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
        std::cerr << "RegisterClassEx failed" << std::endl;
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
        std::cerr << "CreateWindow failed" << std::endl;
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
            [hWnd, pattern, flags, text, &exit_code]( HRESULT result, ICoreWebView2Environment* env ) -> HRESULT
            {
                if( result != S_OK )
                {
                    std::cerr << "CreateCoreWebView2EnvironmentWithOptions failed" << std::endl;
                    exit_code = 1;
                    DestroyWindow( hWnd );

                    return S_FALSE;
                }

                if( !env )
                {
                    std::cerr << "env is null" << std::endl;
                    exit_code = 1;
                    DestroyWindow( hWnd );

                    return S_FALSE;
                }

                // Create a CoreWebView2Controller and get the associated CoreWebView2 whose parent is the main window hWnd
                env->CreateCoreWebView2Controller( hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hWnd, pattern, flags, text, &exit_code]( HRESULT result, ICoreWebView2Controller* controller ) -> HRESULT
                    {
                        if( result != S_OK )
                        {
                            std::cerr << "CreateCoreWebView2Controller failed" << std::endl;
                            exit_code = 1;
                            DestroyWindow( hWnd );

                            return S_FALSE;
                        }

                        if( !controller )
                        {
                            std::cerr << "controller is null" << std::endl;
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

                        bool use_exec = flags_adjusted.erase( std::remove( flags_adjusted.begin( ), flags_adjusted.end( ), L'E' ), flags_adjusted.end( ) ) != flags_adjusted.end( );

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
                                L"  let pattern = \"" + pattern + L"\";" EOL
                                L"  let text = \"" + text + L"\";" EOL
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
                                L"  let pattern = \"" + pattern + L"\";" EOL
                                L"  let text = \"" + text + L"\";" EOL
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
                                [hWnd, &exit_code]( HRESULT errorCode, LPCWSTR resultObjectAsJson ) -> HRESULT
                                {
                                    if( errorCode != S_OK )
                                    {
                                        std::cerr << "JavaScript failed" << std::endl;

                                        exit_code = 1;
                                        DestroyWindow( hWnd );

                                        return S_FALSE;
                                    }

                                    LPCWSTR json = resultObjectAsJson;
                                    //MessageBox( hWnd, json, L"Result", MB_OKCANCEL );

                                    //std::wcout << json << std::endl;
                                    std::cout << WStringToUtf8( json ) << std::endl;

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
