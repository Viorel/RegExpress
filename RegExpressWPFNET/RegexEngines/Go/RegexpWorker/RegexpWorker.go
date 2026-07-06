package main

import (
    "bufio"
    "encoding/json"
    "fmt"
    "io"
    "os"

    regexp "regexp"
)

type inputStruct struct {
    Pattern string
    Text    string

    Posix       bool
    Longest     bool
    Literal     bool
}

type outputStruct struct {
    Names   []string
    Matches [][]int
}

func main() {
    var err error

    reader := bufio.NewReader(os.Stdin)
    input_text, err := reader.ReadString(0)
    if err != nil && err != io.EOF {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    //fmt.Printf("Input text: {%s}\n", input_text)

    var input inputStruct

    err = json.Unmarshal([]byte(input_text), &input)
    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    //fmt.Printf("Input struct: {%+v}\n", input)

    var output outputStruct

    pattern := input.Pattern
    text := input.Text

    is_POSIX := input.Posix
    is_longest := input.Longest
    is_literal := input.Literal

    if is_literal {
        pattern = regexp.QuoteMeta(pattern)
    }

    var re *regexp.Regexp

    if is_POSIX {
        re, err = regexp.CompilePOSIX(pattern)
    } else {
        re, err = regexp.Compile(pattern)
    }

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    if is_longest {
        re.Longest()
    }

    names := re.SubexpNames() // []string
    //fmt.Printf( "names: %q\n", names)

    matches := re.FindAllStringSubmatchIndex(text, -1) // [][]int
    //fmt.Printf( "matches: %d\n", matches)

    output.Names = names
    output.Matches = matches

    output_json, err := json.Marshal(output)

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    fmt.Printf("%s\n", output_json)
}
