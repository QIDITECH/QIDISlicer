# This package loads all the non-GUI Slic3r perl packages.

package Slic3r;

# Copyright holder: Alessandro Ranellucci
# This application is licensed under the GNU Affero General Public License, version 3

use strict;
use warnings;
use Config;
require v5.10;

our $VERSION = VERSION();
our $BUILD = BUILD();
our $FORK_NAME = FORK_NAME();

our $debug = 0;
sub debugf {
    printf @_ if $debug;
}

our $loglevel = 0;

BEGIN {
    $debug = 1 if (defined($ENV{'SLIC3R_DEBUGOUT'}) && $ENV{'SLIC3R_DEBUGOUT'} == 1);
    print "Debugging output enabled\n" if $debug;
}

use FindBin;

use Moo 1.003001;

use Slic3r::XS;   # import all symbols (constants etc.) before they get parsed
use Slic3r::Config;
use Slic3r::GCode::Reader;
use Slic3r::Line;
use Slic3r::Model;
use Slic3r::Point;
use Slic3r::Polygon;
use Slic3r::Polyline;
our $build = eval "use Slic3r::Build; 1";

# Scaling between the float and integer coordinates.
# Floats are in mm.
use constant SCALING_FACTOR         => 0.000001;

# Set the logging level at the Slic3r XS module.
$Slic3r::loglevel = (defined($ENV{'SLIC3R_LOGLEVEL'}) && $ENV{'SLIC3R_LOGLEVEL'} =~ /^[1-9]/) ? $ENV{'SLIC3R_LOGLEVEL'} : 0;
set_logging_level($Slic3r::loglevel);

1;
