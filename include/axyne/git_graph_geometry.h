#ifndef AXYNE_GIT_GRAPH_GEOMETRY_H
#define AXYNE_GIT_GRAPH_GEOMETRY_H

#include "axyne/git_panel.h"

/* Lane cells keep one fixed width however many lanes the graph has: a wide
 * graph scrolls horizontally rather than compressing its lanes. Width of the
 * strip holding `lanes` cells (at least one). */
static inline double axyne_git_graph_strip_width(int lanes, double lane_width)
{
    if (lanes < 1) lanes = 1;
    if (lane_width < 0) lane_width = 0;
    return lanes * lane_width;
}

/* Legacy compressed cell width, kept only until the native renderers move to
 * fixed cells with horizontal scrolling. */
static inline double axyne_git_graph_lane_width(int lanes, double preferred,
                                               double strip_limit)
{
    if (lanes < 1) lanes = 1;
    if (strip_limit < 0) strip_limit = 0;
    return preferred * lanes <= strip_limit ? preferred : strip_limit / lanes;
}

/* Upper bound on the drawn width of one row text (subject, author line, file
 * name): longer text ends in an ellipsis there rather than widening the
 * scrollable graph without limit. */
#define AXYNE_GIT_GRAPH_TEXT_MAX 1000.0

/* Horizontal offset kept inside [0, content - visible]; 0 when it fits. */
static inline double axyne_git_graph_clamp_offset(double offset, double content,
                                                 double visible)
{
    double range = content - visible;
    if (range < 0 || offset < 0) return 0;
    return offset > range ? range : offset;
}

/* Thin scroll indicator: start and length of the thumb along a `track` for a
 * `visible` part of `content` scrolled by `offset`, at least `min_length`
 * long. Returns 0 (no thumb) when everything is visible. */
static inline int axyne_git_graph_thumb(double track, double visible, double content,
                                        double offset, double min_length,
                                        double *start, double *length)
{
    double range = content - visible, len;
    if (track <= 0 || visible <= 0 || range <= 0) return 0;
    len = track * visible / content;
    if (len < min_length) len = min_length;
    if (len > track) len = track;
    offset = axyne_git_graph_clamp_offset(offset, content, visible);
    *start = (track - len) * offset / range;
    *length = len;
    return 1;
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
