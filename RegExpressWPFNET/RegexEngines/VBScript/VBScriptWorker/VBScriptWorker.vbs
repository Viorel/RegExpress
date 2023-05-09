
Dim fso, stdout, stderr

Set fso = CreateObject("Scripting.FileSystemObject")
Set stdout = fso.GetStandardStream(1)
Set stderr = fso.GetStandardStream(2)

If WScript.Arguments.Count < 1 Then
    stderr.WriteLine "Bad arguments"
    WScript.Quit
End If

Dim command 
command = WScript.Arguments(0)

If command = "v" Then 

    stdout.WriteLine ScriptEngineMajorVersion & "." & ScriptEngineMinorVersion ' & "." & ScriptEngineBuildVersion

    WScript.Quit

End If

If command = "m" Then

    If WScript.Arguments.Count < 4 Then
        stderr.WriteLine "Bad arguments"
        WScript.Quit
    End If

    Dim pattern, text, options

    pattern = WScript.Arguments(1)
    text = WScript.Arguments(2)
    options = WScript.Arguments(3)

    Dim re

    Set re = New RegExp

    re.Pattern = pattern
    re.IgnoreCase = False
    re.Global = False

    If InStr(options, "i") > 0 Then re.IgnoreCase = True 
    If InStr(options, "g") > 0 Then re.Global = True

    Dim ms
    Set ms = re.Execute(text)

    Dim m
    For Each m In ms

        stdout.WriteLine "m " & m.FirstIndex & " " & m.Length ' & " '" & m.Value & "'"

        '  Dim sm
        '  For Each sm in m.SubMatches
        '        WScript.Echo "sm '" & sm & "'"
        '  Next

    Next

    WScript.Quit
End If

stderr.WriteLine "Bad command"
WScript.Quit
