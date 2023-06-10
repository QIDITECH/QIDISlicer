package Slic3r::Geometry;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);

# Exported by this module. The last section starting with convex_hull is exported by Geometry.xsp
our @EXPORT_OK = qw(
    PI epsilon 

    scale
    unscale
    scaled_epsilon

    X Y Z
    convex_hull
    chained_path_from
    deg2rad
    rad2deg
);

use constant PI => 4 * atan2(1, 1);
use constant A => 0;
use constant B => 1;
use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;

sub epsilon () { 1E-4 }
sub scaled_epsilon () { epsilon / &Slic3r::SCALING_FACTOR }

sub scale   ($) { $_[0] / &Slic3r::SCALING_FACTOR }
sub unscale ($) { $_[0] * &Slic3r::SCALING_FACTOR }

1;
