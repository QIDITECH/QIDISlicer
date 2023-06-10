use Test::More;
use strict;
use warnings;

plan tests => 10;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use Slic3r::Geometry qw(PI
    chained_path_from epsilon scale);

{
    # this test was failing on Windows (GH #1950)
    my $polygon = Slic3r::Polygon->new(
        [207802834,-57084522],[196528149,-37556190],[173626821,-25420928],[171285751,-21366123],
        [118673592,-21366123],[116332562,-25420928],[93431208,-37556191],[82156517,-57084523],
        [129714478,-84542120],[160244873,-84542120],
    );
    my $point = Slic3r::Point->new(95706562, -57294774);
    ok $polygon->contains_point($point), 'contains_point';
}

#==========================================================

my $polygons = [
    Slic3r::Polygon->new( # contour, ccw
        [45919000, 515273900], [14726100, 461246400], [14726100, 348753500], [33988700, 315389800], 
        [43749700, 343843000], [45422300, 352251500], [52362100, 362637800], [62748400, 369577600], 
        [75000000, 372014700], [87251500, 369577600], [97637800, 362637800], [104577600, 352251500], 
        [107014700, 340000000], [104577600, 327748400], [97637800, 317362100], [87251500, 310422300], 
        [82789200, 309534700], [69846100, 294726100], [254081000, 294726100], [285273900, 348753500], 
        [285273900, 461246400], [254081000, 515273900],

    ),
    Slic3r::Polygon->new( # hole, cw
        [75000000, 502014700], [87251500, 499577600], [97637800, 492637800], [104577600, 482251500], 
        [107014700, 470000000], [104577600, 457748400], [97637800, 447362100], [87251500, 440422300], 
        [75000000, 437985300], [62748400, 440422300], [52362100, 447362100], [45422300, 457748400], 
        [42985300, 470000000], [45422300, 482251500], [52362100, 492637800], [62748400, 499577600],
    ),
];


#==========================================================

{
    my $polygon = Slic3r::Polygon->new([0, 0], [10, 0], [5, 5]);
    my $result = $polygon->split_at_index(1);
    is ref($result), 'Slic3r::Polyline', 'split_at_index returns polyline';
    is_deeply $result->pp, [ [10, 0], [5, 5], [0, 0], [10, 0] ], 'split_at_index';
}

#==========================================================

#{
#    my $bb = Slic3r::Geometry::BoundingBox->new_from_points([ map Slic3r::Point->new(@$_), [0, 1], [10, 2], [20, 2] ]);
#    $bb->scale(2);
#    is_deeply [ $bb->min_point->pp, $bb->max_point->pp ], [ [0,2], [40,4] ], 'bounding box is scaled correctly';
#}

#==========================================================

{
    # if chained_path() works correctly, these points should be joined with no diagonal paths
    # (thus 26 units long)
    my @points = map Slic3r::Point->new_scale(@$_), [26,26],[52,26],[0,26],[26,52],[26,0],[0,52],[52,52],[52,0];
    my @ordered = @points[@{chained_path_from(\@points, $points[0])}];
    ok !(grep { abs($ordered[$_]->distance_to($ordered[$_+1]) - scale 26) > epsilon } 0..$#ordered-1), 'chained_path';
}

#==========================================================

{
    my $line = Slic3r::Line->new([0, 0], [20, 0]);
    is +Slic3r::Point->new(10, 10)->distance_to_line($line), 10, 'distance_to';
    is +Slic3r::Point->new(50, 0)->distance_to_line($line), 30, 'distance_to';
    is +Slic3r::Point->new(0, 0)->distance_to_line($line), 0, 'distance_to';
    is +Slic3r::Point->new(20, 0)->distance_to_line($line), 0, 'distance_to';
    is +Slic3r::Point->new(10, 0)->distance_to_line($line), 0, 'distance_to';
}

{
    my $triangle = Slic3r::Polygon->new(
        [16000170,26257364], [714223,461012], [31286371,461008],
    );
    my $simplified = $triangle->simplify(250000)->[0];
    is scalar(@$simplified), 3, 'triangle is never simplified to less than 3 points';
}

__END__
