eval
{
    use strict; 
    use warnings; 

    use JSON qw(decode_json);
    use re;

    my $input = "";
    while(<STDIN>) { $input .= $_; }

    #print "$input\n";

    my $object = decode_json($input) or die "Invalid input";

    my $pattern = $object->{p};
    my $text = $object->{t};
    my $modifiers = $object->{m};

    #print "PATTERN:   '$pattern'\n";
    #print "TEXT:      '$text'\n";
    #print "MODIFIERS: '$modifiers'\n";

    undef $input;

    do
    {
        use re 'debug';
        print STDERR "\x1FDEBUG>\n";
        my $debug_re = qr/$pattern/;
        print STDERR "<\x1FDEBUG\n";
        undef $debug_re;
        #no re 'debug';
    };

    my $modifiers_without_g = ($modifiers =~ tr/g//dr);
    my $re = eval( 'qr/$pattern/' . $modifiers_without_g );

    while( $text =~ /$re/g ) 
    {
        print "\x1FM\n";
        for( my $i = 0; $i < scalar @+; ++$i)
        {
            my $success = defined $-[$i]; 
            if( $success )
            {
                my $index = $-[$i];
                my $length = $+[$i] - $-[$i];
                #my $val = @{^CAPTURE}[$i];
                print "\x1FG,$index,$length\n";
            }
            else
            {
                print "\x1FG,-1,0\n";
            }
        }

        last if index($modifiers, "g") < 0;
    }
};

if( $@ )
{
    print STDERR "\x1FERR>$@<\x1FERR\n";
}
