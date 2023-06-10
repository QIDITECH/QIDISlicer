package Slic3r::XS;
use warnings;
use strict;

our $VERSION = '0.01';

use Carp qw();
use XSLoader;
XSLoader::load(__PACKAGE__, $VERSION);

package Slic3r::Line;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Point;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Pointf;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Pointf3;
use overload
    '@{}' => sub { [ $_[0]->x, $_[0]->y, $_[0]->z ] },  #,
    'fallback' => 1;

sub pp {
    my ($self) = @_;
    return [ @$self ];
}

package Slic3r::ExPolygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polyline;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Polygon;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

package Slic3r::Surface;

sub new {
    my ($class, %args) = @_;
    
    # defensive programming: make sure no negative bridge_angle is supplied
    die "Error: invalid negative bridge_angle\n"
        if defined $args{bridge_angle} && $args{bridge_angle} < 0;
    
    return $class->_new(
        $args{expolygon}         // (die "Missing required expolygon\n"),
        $args{surface_type}      // (die "Missing required surface_type\n"),
        $args{thickness}         // -1,
        $args{thickness_layers}  // 1,
        $args{bridge_angle}      // -1,
        $args{extra_perimeters}  // 0,
    );
}

sub clone {
    my ($self, %args) = @_;
    
    return (ref $self)->_new(
        delete $args{expolygon}         // $self->expolygon,
        delete $args{surface_type}      // $self->surface_type,
        delete $args{thickness}         // $self->thickness,
        delete $args{thickness_layers}  // $self->thickness_layers,
        delete $args{bridge_angle}      // $self->bridge_angle,
        delete $args{extra_perimeters}  // $self->extra_perimeters,
    );
}

package Slic3r::Surface::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref },
    'fallback' => 1;

sub new {
    my ($class, @surfaces) = @_;
    
    my $self = $class->_new;
    $self->append($_) for @surfaces;
    return $self;
}

package main;
for my $class (qw(
        Slic3r::Config
        Slic3r::Config::GCode
        Slic3r::Config::Print
        Slic3r::Config::Static
        Slic3r::ExPolygon
        Slic3r::Line
        Slic3r::Model
        Slic3r::Model::Instance
        Slic3r::Model::Material
        Slic3r::Model::Object
        Slic3r::Model::Volume
        Slic3r::Point
        Slic3r::Point3
        Slic3r::Pointf
        Slic3r::Pointf3
        Slic3r::Polygon
        Slic3r::Polyline
        Slic3r::Polyline::Collection
        Slic3r::Print
        Slic3r::TriangleMesh
    ))
{
    no strict 'refs';
    my $ref_class = $class . "::Ref";
    eval "package $ref_class; our \@ISA = '$class'; sub DESTROY {};";
}

1;
