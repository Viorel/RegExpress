module fregex_template
!! Replacement templates for substitution, e.g. `'\1-\g<name>'`, with Python `re.sub` syntax.
use fregex_string, only: regex_string_t, string_builder_t, to_str, is_alpha_code, is_digit_code
implicit none
private

public :: parse_template, expand_template

type, public :: template_t
    !! A sequence of pieces, each either literal text (`group == -1`) or a group reference.
    integer, allocatable :: group(:)
    type(regex_string_t), allocatable :: text(:)
end type template_t

contains

subroutine parse_template(repl, names, tmpl, errmsg)
    !! Parse `repl`. Supports `\1`..`\99`, `\g<n>`, `\g<name>`, the escapes `\n \t \r \f \v \a \b \\`,
    !! and octal `\0`, `\0oo`, `\ooo`. Other non-letter escapes are kept as written.
    character(len=*), intent(in) :: repl
    type(regex_string_t), intent(in) :: names(:)
    type(template_t), intent(out) :: tmpl
    character(len=:), allocatable, intent(out) :: errmsg

    type(string_builder_t) :: lit
    integer :: i, j, g, code, ngroups
    character :: d
    character(len=:), allocatable :: name

    ngroups = size(names)
    allocate (tmpl%group(0), tmpl%text(0))
    i = 1
    do while (i <= len(repl))
        if (repl(i:i) /= '\') then
            call lit%append(repl(i:i))
            i = i + 1
            cycle
        end if
        if (i == len(repl)) then
            errmsg = 'bad escape (end of pattern) at position '//to_str(i)
            return
        end if
        d = repl(i + 1:i + 1)
        select case (d)
        case ('g')
            if (i + 2 > len(repl)) then
                errmsg = 'missing < at position '//to_str(i + 2)
                return
            end if
            if (repl(i + 2:i + 2) /= '<') then
                errmsg = 'missing < at position '//to_str(i + 2)
                return
            end if
            j = index(repl(i + 3:), '>')
            if (j == 0) then
                errmsg = 'missing >, unterminated name at position '//to_str(i + 3)
                return
            end if
            name = repl(i + 3:i + j + 1)
            if (len(name) == 0) then
                errmsg = 'missing group name at position '//to_str(i + 3)
                return
            end if
            if (verify(name, '0123456789') == 0) then
                if (len(name) > 6) then
                    g = huge(1)
                else
                    read (name, *) g
                end if
            else
                do g = 1, ngroups
                    if (len(names(g)%s) == len(name)) then
                        if (names(g)%s == name) exit
                    end if
                end do
                if (g > ngroups) then
                    errmsg = "unknown group name '"//name//"' at position "//to_str(i + 3)
                    return
                end if
            end if
            if (g > ngroups) then
                errmsg = 'invalid group reference '//name//' at position '//to_str(i + 3)
                return
            end if
            call add_group(g)
            i = i + j + 3
        case ('0')
            code = 0
            i = i + 2
            do j = 1, 2
                if (i > len(repl)) exit
                if (repl(i:i) < '0' .or. repl(i:i) > '7') exit
                code = 8*code + iachar(repl(i:i)) - iachar('0')
                i = i + 1
            end do
            call lit%append(achar(code))
        case ('1':'9')
            if (i + 3 <= len(repl)) then
                if (verify(repl(i + 1:i + 3), '01234567') == 0) then
                    read (repl(i + 1:i + 3), '(o3)') code
                    if (code > 255) then
                        errmsg = 'octal escape value outside of range 0-0o377 at position '//to_str(i)
                        return
                    end if
                    call lit%append(achar(code))
                    i = i + 4
                    cycle
                end if
            end if
            g = iachar(d) - iachar('0')
            j = i + 2
            if (j <= len(repl)) then
                if (is_digit_code(iachar(repl(j:j)))) then
                    g = 10*g + iachar(repl(j:j)) - iachar('0')
                    j = j + 1
                end if
            end if
            if (g > ngroups) then
                errmsg = 'invalid group reference '//to_str(g)//' at position '//to_str(i + 1)
                return
            end if
            call add_group(g)
            i = j
        case default
            select case (d)
            case ('n')
                call lit%append(achar(10))
            case ('t')
                call lit%append(achar(9))
            case ('r')
                call lit%append(achar(13))
            case ('f')
                call lit%append(achar(12))
            case ('v')
                call lit%append(achar(11))
            case ('a')
                call lit%append(achar(7))
            case ('b')
                call lit%append(achar(8))
            case ('\')
                call lit%append('\')
            case default
                if (is_alpha_code(iachar(d))) then
                    errmsg = 'bad escape \'//d//' at position '//to_str(i)
                    return
                end if
                call lit%append('\'//d)
            end select
            i = i + 2
        end select
    end do
    call flush_literal()

contains

    subroutine flush_literal()
        character(len=:), allocatable :: text
        text = lit%get()
        if (len(text) == 0) return
        tmpl%group = [tmpl%group, -1]
        tmpl%text = [tmpl%text, regex_string_t(text)]
        call lit%clear()
    end subroutine flush_literal

    subroutine add_group(k)
        integer, intent(in) :: k
        call flush_literal()
        tmpl%group = [tmpl%group, k]
        tmpl%text = [tmpl%text, regex_string_t('')]
    end subroutine add_group

end subroutine parse_template

pure subroutine expand_template(tmpl, subject, caps, out)
    !! Append the replacement text for one match to `out`. `caps` holds VM capture positions
    !! (0 if unset).
    type(template_t), intent(in) :: tmpl
    character(len=*), intent(in) :: subject
    integer, intent(in) :: caps(0:)
    type(string_builder_t), intent(inout) :: out
    integer :: k, g

    do k = 1, size(tmpl%group)
        g = tmpl%group(k)
        if (g < 0) then
            call out%append(tmpl%text(k)%s)
        else if (caps(2*g) > 0 .and. caps(2*g + 1) > 0) then
            call out%append(subject(caps(2*g):caps(2*g + 1) - 1))
        end if
    end do
end subroutine expand_template

end module fregex_template
