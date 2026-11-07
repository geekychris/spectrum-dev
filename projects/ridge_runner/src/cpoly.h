/* the polygon fill rule shared by the host reference (tools/view_host.c)
   and the Next's filler: column x of a convex polygon is drawn for
   xmin <= x < xmax; each edge from (x0, y0) to (x1, y1) with x0 != x1 gives,
   for the columns it spans (min(x0, x1) <= x < max(x0, x1)),
       y(x) = ((y0 << 16) + s * (x - x0) + 0x8000) >> 16,
       s = ((y1 - y0) << 16) / (x1 - x0)   (truncated towards zero),
   and the column is filled from the smallest y to below the largest. */
