#ifndef BRANCHINGTREESLA_HPP
#define BRANCHINGTREESLA_HPP

#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "SupportTreeBuilder.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace sla {

void create_branching_tree(SupportTreeBuilder& builder, const SupportableMesh &sm);

}} // namespace Slic3r::sla

#endif // BRANCHINGTREESLA_HPP
