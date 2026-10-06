#ifndef AXYNE_GIT_GRAPH_GEOMETRY_H
#define AXYNE_GIT_GRAPH_GEOMETRY_H

#include "axyne/git_panel.h"

/* Fit every logical lane in the reserved strip. Keep fractional cell widths:
 * a minimum pixel width would silently clip long-lived concurrent lanes. */
static inline double axyne_git_graph_lane_width(int lanes, double preferred,
                                               double strip_limit)
{
    if (lanes < 1) lanes = 1;
    if (strip_limit < 0) strip_limit = 0;
    return preferred * lanes <= strip_limit ? preferred : strip_limit / lanes;
}

/* Rightmost lane of a row whose flags intersect `mask`; -1 when none. */
static inline int axyne_git_graph_last_cell(const AxyneGitGraphRow *row,
                                           unsigned mask)
{
    int i;
    for (i = row->lane_count - 1; i >= 0; --i)
        if ((row->lanes[i].flags & mask) != 0) return i;
    return -1;
}

/* Row text starts right after the row's own rightmost mark rather than after
 * the whole graph strip: whichever reaches further of the strokes up to
 * `stroke_cell` (half a stroke past the centre) and the dot in `dot_cell`
 * (its radius), then `margin`. A cell of -1 draws nothing. `left` is the
 * centre offset of cell 0 minus half a cell. */
static inline double axyne_git_graph_text_x(double left, double lane_width,
                                           int stroke_cell, double stroke_reach,
                                           int dot_cell, double dot_reach,
                                           double margin)
{
    double edge = left;
    double stroke = left + (stroke_cell + 0.5) * lane_width + stroke_reach;
    double dot = left + (dot_cell + 0.5) * lane_width + dot_reach;
    if (stroke_cell >= 0 && stroke > edge) edge = stroke;
    if (dot_cell >= 0 && dot > edge) edge = dot;
    return edge + margin;
}

#endif
