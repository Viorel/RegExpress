    !  FortranForgexWorker.f90
    !
    !  FUNCTIONS:
    !  FortranForgexWorker - Entry point of console application.
    !

    !****************************************************************************
    !
    !  PROGRAM: FortranForgexWorker
    !
    !  PURPOSE:  Entry point for the console application.
    !
    !****************************************************************************

    program FortranForgexWorker
    use, intrinsic :: iso_fortran_env, &
        only: stdin => input_unit, &
        stdout => output_unit, &
        stderr => error_unit, &
        compiler_version
    implicit none

    character (10) :: command

    read (stdin, *) command

    if(command == "v") then
        block
            character (len=:), allocatable :: compiler_version0

            compiler_version0 = compiler_version()

            write(stdout, "(a,at)"), "Version=", compiler_version0

            deallocate (compiler_version0)

            stop
        end block
    end if

    if( command == "m" ) then
        block
            use :: forgex
            use :: forgex_syntax_tree_error_m

            character (:), allocatable :: pattern, text, options, result
            integer :: from, to, status, absolute_from
            character (256) err_msg
            logical :: overlapped

            call read_line(stdin, pattern)
            call read_line(stdin, text)
            call read_line(stdin, options)

            pattern = replace_all(pattern, CHAR(27) // "r", CHAR(13))
            pattern = replace_all(pattern, CHAR(27) // "n", CHAR(10))

            text = replace_all(text, CHAR(27) // "r", CHAR(13))
            text = replace_all(text, CHAR(27) // "n", CHAR(10))

            overlapped = (index(options, "o") > 0)

            absolute_from = 1

            do
                call regex(pattern, text(absolute_from:), result, from = from, to = to, status = status, err_msg = err_msg)

                if(status /= SYNTAX_VALID) then

                    write(stderr, "(at)"), err_msg

                    stop
                end if

                if( to == 0 .or. to < from ) exit

                write(stdout, "('m ', i0, ' ', i0)"), absolute_from + from - 1, absolute_from + to - 1

                if (overlapped) then
                    absolute_from = absolute_from + from
                else
                    absolute_from = absolute_from + to
                end if
            end do

            stop
        end block
    end if

    if( command == "t") then
        block
            character (:), allocatable :: str

            ! return the input string, for testing purposes

            call read_line(stdin, str)

            write(stdout, "(a)") str

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

    end program FortranForgexWorker

