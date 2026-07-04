    !  FortranRegexJeyemhexWorker.f90
    !
    !  FUNCTIONS:
    !  FortranRegexJeyemhexWorker - Entry point of console application.
    !

    !****************************************************************************
    !
    !  PROGRAM: FortranRegexJeyemhexWorker
    !
    !  PURPOSE:  Entry point for the console application.
    !
    !****************************************************************************

    program FortranRegexJeyemhexWorker

    use, intrinsic :: iso_fortran_env, &
        only: stdin => input_unit, &
        stdout => output_unit, &
        stderr => error_unit

    implicit none

    character (10) :: command

    read (stdin, *) command

    if( command == "t") then
        block
            character (:), allocatable :: str

            ! return the input string, for testing purposes

            call read_line(stdin, str)

            write(stdout, "(a)") str

            stop
        end block
    end if

    if( command == "m" ) then
        block

            use :: regex

            character (:), allocatable :: pattern, text, flags, result
            logical :: match_all, overlapped
            integer :: absolute_from, from

            call read_line(stdin, pattern)
            call read_line(stdin, text)
            call read_line(stdin, flags)

            pattern = replace_all(pattern, CHAR(27) // "r", CHAR(13))
            pattern = replace_all(pattern, CHAR(27) // "n", CHAR(10))

            text = replace_all(text, CHAR(27) // "r", CHAR(13))
            text = replace_all(text, CHAR(27) // "n", CHAR(10))

            match_all = (index(flags, "A") > 0)
            overlapped = (index(flags, "o") > 0)

            absolute_from = 1

            do
                result = trim( re_match_str( pattern, text(absolute_from:) ) )

                if( len(result) == 0 ) exit

                from = index( text(absolute_from:), result )

                if( from <= 0 ) exit !//...

                write(stdout, "('m ', i0, ' ', i0)"), absolute_from + from - 1, len(result)

                if( .not. match_all ) exit

                if( overlapped ) then
                    absolute_from = absolute_from + from
                else
                    if (len(result) .eq. 0) then ! empty match (not currently supported by 'regex')
                        absolute_from = absolute_from + from
                    else
                        absolute_from = absolute_from + from + len(result) - 1
                    end if
                end if

                if( absolute_from > len(text) ) exit

            end do

            stop
        end block
    end if

    write (stderr, "(a,at,a)") "Invalid command: '", command, "'"
    stop

    contains

    subroutine read_line(unit, line) ! https://stackoverflow.com/questions/31084087/using-a-deferred-length-character-string-to-read-user-input
    integer, intent(in) :: unit
    character(:), intent(out), allocatable :: line

    integer :: stat
    character(512) :: buffer
    integer :: size

    line = ""

    do
        read (unit, "(a)", advance='no', iostat=stat, size=size) buffer

        if (stat > 0) then
            write(stderr, *) "error reading line"
            stop
        end if

        line = line // buffer(:size)

        if (stat < 0) return ! end
    end do

    end subroutine read_line

    function replace_all(str, old, new) result(replaced) ! https://learnxbyexample.com/fortran/string-functions/
    character(len=*), intent(in) :: str, old, new
    character(len=:), allocatable :: replaced
    integer :: i
    replaced = str
    do i = 1, len(str) - len(old) + 1
        if (replaced(i:i+len(old)-1) == old) then
            replaced = replaced(:i-1) // new // replaced(i+len(old):)
        end if
    end do
    end function replace_all


    end program FortranRegexJeyemhexWorker

