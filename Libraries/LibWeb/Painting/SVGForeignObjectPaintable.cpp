/*
 * Copyright (c) 2024, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Painting/SVGForeignObjectPaintable.h>
#include <LibWeb/SVG/SVGSVGElement.h>

namespace Web::Painting {

GC_DEFINE_ALLOCATOR(SVGForeignObjectPaintable);

GC::Ref<SVGForeignObjectPaintable> SVGForeignObjectPaintable::create(Layout::SVGForeignObjectBox const& layout_box)
{
    return layout_box.heap().allocate<SVGForeignObjectPaintable>(layout_box);
}

SVGForeignObjectPaintable::SVGForeignObjectPaintable(Layout::SVGForeignObjectBox const& layout_box)
    : PaintableWithLines(layout_box)
{
}

Layout::SVGForeignObjectBox const& SVGForeignObjectPaintable::layout_box() const
{
    return static_cast<Layout::SVGForeignObjectBox const&>(layout_node());
}

TraversalDecision SVGForeignObjectPaintable::hit_test(CSSPixelPoint position, HitTestType type, Function<TraversalDecision(HitTestResult)> const& callback) const
{
    return PaintableWithLines::hit_test(position, type, callback);
}

void SVGForeignObjectPaintable::paint(DisplayListRecordingContext& context, PaintPhase phase) const
{
    if (!is_visible())
        return;

    PaintableWithLines::paint(context, phase);
}

}
