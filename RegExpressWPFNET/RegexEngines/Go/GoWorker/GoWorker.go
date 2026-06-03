package main

import (
    "fmt"
    "os"
    "bufio"
    "strings"
    "encoding/json"
    "regexp"
)

type Input struct {
    Pattern string
    Text    string
    Flags   string
}

type Output struct
{
    Names   []string
    Matches [][]int
}

func main() {
    reader := bufio.NewReader( os.Stdin)
    input_text, _ := reader.ReadString( 0)

    //fmt.Println("Input: ", input_text)

    var input Input
    var err error

    err = json.Unmarshal( []byte( input_text), &input)
    if err != nil {
        fmt.Fprintln( os.Stderr, err)

        return
    }

    //fmt.Printf( "Pattern: '%s'\n", input.Pattern)
    //fmt.Printf( "Text: '%s'\n", input.Text)

    is_POSIX := strings.Contains( input.Flags, "P")
    is_longest := strings.Contains( input.Flags, "L")
    is_literal := strings.Contains( input.Flags, "Q")

    pattern := input.Pattern

    if is_literal {
        pattern = regexp.QuoteMeta( pattern)
    }

    var re *regexp.Regexp

    if is_POSIX {
        re, err = regexp.CompilePOSIX( pattern)
    } else {
        re, err = regexp.Compile( pattern)
    }

    if err != nil {
        fmt.Fprintln( os.Stderr, err)

        return
    }

    if is_longest {
        re.Longest()
    }

    names := re.SubexpNames() // []string
    //fmt.Printf( "names: %q\n", names)

    matches := re.FindAllStringSubmatchIndex( input.Text, -1) // [][]int
    //fmt.Printf( "matches: %d\n", matches)

    output := &Output{ }
    output.Names = names
    output.Matches = matches

    //fmt.Printf( "output: %+v\n", output)

    output_json, err := json.Marshal(output)

    if err != nil {
        fmt.Fprintln( os.Stderr, "Error: ", err)

        return
    }

    fmt.Printf( "%s\n", output_json)
}
