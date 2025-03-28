/*
 * Copyright (c) 2023, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <LibGfx/Bitmap.h>
#include <LibURL/Parser.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/FetchController.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>
#include <LibWeb/HTML/AnimatedBitmapDecodedImageData.h>
#include <LibWeb/HTML/DecodedImageData.h>
#include <LibWeb/HTML/ImageRequest.h>
#include <LibWeb/HTML/ListOfAvailableImages.h>
#include <LibWeb/HTML/SharedResourceRequest.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Platform/ImageCodecPlugin.h>
#include <LibWeb/SVG/SVGDecodedImageData.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(ImageRequest);

GC::Ref<ImageRequest> ImageRequest::create(JS::Realm& realm, GC::Ref<Page> page)
{
    return realm.create<ImageRequest>(page);
}

ImageRequest::ImageRequest(GC::Ref<Page> page)
    : m_page(page)
{
}

ImageRequest::~ImageRequest()
{
}

void ImageRequest::visit_edges(JS::Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_shared_resource_request);
    visitor.visit(m_page);
    visitor.visit(m_image_data);
}

// https://html.spec.whatwg.org/multipage/images.html#img-available
bool ImageRequest::is_available() const
{
    // When an image request's state is either partially available or completely available, the image request is said to be available.
    return m_state == State::PartiallyAvailable || m_state == State::CompletelyAvailable;
}

bool ImageRequest::is_fetching() const
{
    return m_shared_resource_request && m_shared_resource_request->is_fetching();
}

ImageRequest::State ImageRequest::state() const
{
    return m_state;
}

void ImageRequest::set_state(State state)
{
    m_state = state;
}

void ImageRequest::set_current_url(JS::Realm& realm, String url)
{
    m_current_url = move(url);
    if (auto url = URL::Parser::basic_parse(m_current_url); url.has_value())
        m_shared_resource_request = SharedResourceRequest::get_or_create(realm, m_page, url.release_value());
}

// https://html.spec.whatwg.org/multipage/images.html#abort-the-image-request
void abort_the_image_request(JS::Realm&, ImageRequest* image_request)
{
    // 1. If image request is null, then return.
    if (!image_request)
        return;

    // 2. Forget image request's image data, if any.
    image_request->set_image_data(nullptr);

    // 3. Abort any instance of the fetching algorithm for image request,
    //    discarding any pending tasks generated by that algorithm.
    // AD-HOC: We simply don't do this, since our SharedResourceRequest may be used by someone else.
}

GC::Ptr<DecodedImageData> ImageRequest::image_data() const
{
    return m_image_data;
}

void ImageRequest::set_image_data(GC::Ptr<DecodedImageData> data)
{
    m_image_data = data;
}

// https://html.spec.whatwg.org/multipage/images.html#prepare-an-image-for-presentation
void ImageRequest::prepare_for_presentation(HTMLImageElement&)
{
    // FIXME: 1. Let exifTagMap be the EXIF tags obtained from req's image data, as defined by the relevant codec. [EXIF]
    // FIXME: 2. Let physicalWidth and physicalHeight be the width and height obtained from req's image data, as defined by the relevant codec.
    // FIXME: 3. Let dimX be the value of exifTagMap's tag 0xA002 (PixelXDimension).
    // FIXME: 4. Let dimY be the value of exifTagMap's tag 0xA003 (PixelYDimension).
    // FIXME: 5. Let resX be the value of exifTagMap's tag 0x011A (XResolution).
    // FIXME: 6. Let resY be the value of exifTagMap's tag 0x011B (YResolution).
    // FIXME: 7. Let resUnit be the value of exifTagMap's tag 0x0128 (ResolutionUnit).
    // FIXME: 8. If either dimX or dimY is not a positive integer, then return.
    // FIXME: 9. If either resX or resY is not a positive floating-point number, then return.
    // FIXME: 10. If resUnit is not equal to 2 (Inch), then return.
    // FIXME: 11. Let widthFromDensity be the value of physicalWidth, multiplied by 72 and divided by resX.
    // FIXME: 12. Let heightFromDensity be the value of physicalHeight, multiplied by 72 and divided by resY.
    // FIXME: 13. If widthFromDensity is not equal to dimX or heightFromDensity is not equal to dimY, then return.
    // FIXME: 14. If req's image data is CORS-cross-origin, then set img's intrinsic dimensions to dimX and dimY, scale img's pixel data accordingly, and return.
    // FIXME: 15. Set req's preferred density-corrected dimensions to a struct with its width set to dimX and its height set to dimY.
    // FIXME: 16. Update req's img element's presentation appropriately.
}

void ImageRequest::fetch_image(JS::Realm& realm, GC::Ref<Fetch::Infrastructure::Request> request)
{
    VERIFY(m_shared_resource_request);
    m_shared_resource_request->fetch_resource(realm, request);
}

void ImageRequest::add_callbacks(Function<void()> on_finish, Function<void()> on_fail)
{
    VERIFY(m_shared_resource_request);
    m_shared_resource_request->add_callbacks(move(on_finish), move(on_fail));
}

}
