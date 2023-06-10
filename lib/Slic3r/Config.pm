# Extends C++ class Slic3r::DynamicPrintConfig
# This perl class does not keep any perl class variables,
# all the storage is handled by the underlying C++ code.
package Slic3r::Config;
use strict;
use warnings;
use utf8;

use List::Util qw(first max);

# C++ Slic3r::PrintConfigDef exported as a Perl hash of hashes.
# The C++ counterpart is a constant singleton.
our $Options = print_config_def();

# Generate accessors.
{
    no strict 'refs';
    for my $opt_key (keys %$Options) {
        *{$opt_key} = sub { 
            #print "Slic3r::Config::accessor $opt_key\n"; 
            $_[0]->get($opt_key)
        };
    }
}

package Slic3r::Config::Static;
use parent 'Slic3r::Config';

sub Slic3r::Config::GCode::new { Slic3r::Config::Static::new_GCodeConfig }
sub Slic3r::Config::Print::new { Slic3r::Config::Static::new_PrintConfig }

1;
