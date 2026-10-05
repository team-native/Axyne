#ifndef AXYNE_GIT_GRAPH_GEOMETRY_H
#define AXYNE_GIT_GRAPH_GEOMETRY_H

/* Fit every logical lane in the reserved strip. Keep fractional cell widths:
 * a minimum pixel width would silently clip long-lived concurrent lanes. */
static inline double axyne_git_graph_lane_width(int lanes, double preferred,
                                               double strip_limit)
{
    if (lanes < 1) lanes = 1;
    if (strip_limit < 0) strip_limit = 0;
    return preferred * lanes <= strip_limit ? preferred : strip_limit / lanes;
}

#endif
