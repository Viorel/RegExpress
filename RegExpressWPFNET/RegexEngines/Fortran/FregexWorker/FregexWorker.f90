!  FregexWorker.f90 
!
!  FUNCTIONS:
!  FregexWorker - Entry point of console application.
!

!****************************************************************************
!
!  PROGRAM: FregexWorker
!
!  PURPOSE:  Entry point for the console application.
!
!****************************************************************************

    program FregexWorker

    use, intrinsic :: iso_fortran_env, &
        only: stdin => input_unit, &
        stdout => output_unit, &
        stderr => error_unit
    
    implicit none

    block
   
        use fregex

        character (:), allocatable :: pattern, text, flags, result
        character (256) err_msg

        integer :: re_flags = 0
        type(regex_t) :: re
        type(match_t) :: match
        type(match_t), allocatable :: matches(:)
        integer :: stat
        character(:), allocatable :: errmsg
        integer :: i, k

        call read_line( stdin, pattern)
        call read_line( stdin, text)
        call read_line( stdin, flags)

        pattern = replace_all( pattern, CHAR(27) // "r", CHAR(13))
        pattern = replace_all( pattern, CHAR(27) // "n", CHAR(10))

        text = replace_all( text, CHAR(27) // "r", CHAR(13))
        text = replace_all( text, CHAR(27) // "n", CHAR(10))

        !print *, pattern
        !print *, text
        !print *, flags
        
        if( index( flags, "i") > 0) re_flags = IOR( re_flags, RE_IGNORECASE)
        if( index( flags, "m") > 0) re_flags = IOR( re_flags, RE_MULTILINE)
        if( index( flags, "s") > 0) re_flags = IOR( re_flags, RE_DOTALL)
        
        re = regex( pattern, re_flags, stat, errmsg)
        
        if( stat /= RE_OK) then 
            write( stderr, "('Error #', i0, ': ', at)") stat, errmsg        
            stop
        end if 
        
        matches = re%find_all( text, stat, errmsg)
       
        if( stat /= RE_OK) then 
            write( stderr, "('Error #', i0, ': ', at)") stat, errmsg        
            stop
        end if 
        
        do i = 1, size( matches)
            match = matches( i)

            write( stdout, "('m ', i0, ' ', i0)") match%start(0), match%end(0)
            
            do k = 1, match%group_count()
                write( stdout, "('g ', i0, ' ', i0)") match%start(k), match%end(k)
            end do
        end do
        
        ! TODO: group names
        
    end block
    
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

    end program FregexWorker
