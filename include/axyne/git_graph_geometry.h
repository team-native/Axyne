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

/* Rightmost cell a row actually draws in: the dot column (when `with_dot`)
 * or the last lane whose flags intersect `mask`. -1 when nothing is drawn. */
static inline int axyne_git_graph_last_cell(const AxyneGitGraphRow *row,
                                           unsigned mask, int with_dot)
{
    int last = with_dot ? row->column : -1;
    int i;
    for (i = row->lane_count - 1; i > last; --i)
        if ((row->lanes[i].flags & mask) != 0) return i;
    return last;
}

/* Row text starts right after the row's own rightmost mark rather than after
 * the whole graph strip: `reach` is how far that mark extends past its cell
 * centre (dot radius or half a stroke), `margin` the gap before the text. */
static inline double axyne_git_graph_text_x(double left, double lane_width,
                                           int last_cell, double reach,
                                           double margin)
{
    if (last_cell < 0) return left + margin;
    return left + (last_cell + 0.5) * lane_width + reach + margin;
}

#endif
