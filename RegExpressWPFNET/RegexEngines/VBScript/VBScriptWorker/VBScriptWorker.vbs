
Dim fso, stdin, stdout, stderr

Set fso = CreateObject("Scripting.FileSystemObject")
Set stdin = fso.GetStandardStream(0)
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

If command = "m" Or command = "e" Or command = "x" Then

    If (command = "m" Or command = "e") And WScript.Arguments.Count < 4 Then
        stderr.WriteLine "Bad arguments"
        WScript.Quit
    End If

    Dim pattern, text, options

    If command = "m" Or command = "e" Then 
        pattern = WScript.Arguments(1)
        text = WScript.Arguments(2)
        options = WScript.Arguments(3)
    Else
        Dim inp
        inp = stdin.ReadAll()
        Dim arr
        arr = Split(inp, ChrW(&H1F))
        pattern = arr(0)
        text = arr(1)
        options = arr(2)
    End If

    If command = "e" Or command = "x" Then
        pattern = Eval(Replace(pattern, "'", """"))
        text = Eval(Replace(text, "'", """"))
    End If

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

        stdout.WriteLine "m " & m.FirstIndex & " " & m.Length '& " '" & m.Value & "'"

        Dim sm
        For Each sm in m.SubMatches
            Dim s, i
            s = ""
            For i = 1 To Len(sm)
                Dim c
                c = Mid(sm, i, 1)
                Dim a
                u = AscW(c)
                If (u >= AscW("a") And u <= AscW("z")) Or (u >= AscW("A") And u <= AscW("Z")) Or (u >= AscW("0") And u <= AscW("9")) Then
                    s = s & c
                Else
                    s = s & "\u" & Left("000", 4 - Len(Hex(u))) & Hex(u)
                End If
            Next
            stdout.WriteLine  "s """ & s & """"
        Next

    Next

    WScript.Quit
End If

stderr.WriteLine "Bad command"
WScript.Quit
