    !  FortranRegexPerazzWorker.f90
    !
    !  FUNCTIONS:
    !  FortranRegexPerazzWorker - Entry point of console application.
    !

    !****************************************************************************
    !
    !  PROGRAM: FortranRegexPerazzWorker
    !
    !  PURPOSE:  Entry point for the console application.
    !
    !****************************************************************************

    program FortranRegexPerazzWorker

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

            use regex_module

            character (:), allocatable :: pattern, text, flags
            logical :: match_all, overlapped, back
            integer :: absolute_from, from, length
            type(regex_pattern) :: re

            call read_line(stdin, pattern)
            call read_line(stdin, text)
            call read_line(stdin, flags)

            pattern = replace_all(pattern, CHAR(27) // "r", CHAR(13))
            pattern = replace_all(pattern, CHAR(27) // "n", CHAR(10))

            text = replace_all(text, CHAR(27) // "r", CHAR(13))
            text = replace_all(text, CHAR(27) // "n", CHAR(10))

            match_all = (index(flags, "A") > 0)
            overlapped = (index(flags, "o") > 0)
            back = (index(flags, "B") > 0) ! // TODO: implement

            re = parse_pattern(pattern)

            absolute_from = 1

            do

                from = REGEX(text(absolute_from:), re, length)

                if( from <= 0 ) exit
                
                write(stdout, "('m ', i0, ' ', i0)"), absolute_from + from - 1, length

                if( .not. match_all ) exit

                if( overlapped ) then
                    absolute_from = absolute_from + from
                else
                    if (length .eq. 0) then
                        absolute_from = absolute_from + from
                    else
                        absolute_from = absolute_from + from + length - 1
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


    end program FortranRegexPerazzWorker

