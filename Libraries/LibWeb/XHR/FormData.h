/*
 * Copyright (c) 2023-2024, Kenneth Myhra <kennethmyhra@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/FormDataPrototype.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/DOMURL/URLSearchParams.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/HTMLFormElement.h>
#include <LibWeb/WebIDL/ExceptionOr.h>
#include <LibWeb/XHR/FormDataEntry.h>

namespace Web::XHR {

// https://xhr.spec.whatwg.org/#interface-formdata
class FormData : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(FormData, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(FormData);

public:
    virtual ~FormData() override;

    static WebIDL::ExceptionOr<GC::Ref<FormData>> construct_impl(JS::Realm&, GC::Ptr<HTML::HTMLFormElement> form = {}, GC::Ptr<HTML::HTMLElement> submitter = nullptr);
    static WebIDL::ExceptionOr<GC::Ref<FormData>> construct_impl(JS::Realm&, Vector<FormDataEntry> entry_list);

    static WebIDL::ExceptionOr<GC::Ref<FormData>> create(JS::Realm&, Vector<DOMURL::QueryParam> entry_list);
    static WebIDL::ExceptionOr<GC::Ref<FormData>> create(JS::Realm&, Vector<FormDataEntry> entry_list);

    WebIDL::ExceptionOr<void> append(String const& name, String const& value);
    WebIDL::ExceptionOr<void> append(String const& name, GC::Ref<FileAPI::Blob> const& blob_value, Optional<String> const& filename = {});
    void delete_(String const& name);
    Variant<GC::Root<FileAPI::File>, String, Empty> get(String const& name);
    WebIDL::ExceptionOr<Vector<FormDataEntryValue>> get_all(String const& name);
    bool has(String const& name);
    WebIDL::ExceptionOr<void> set(String const& name, String const& value);
    WebIDL::ExceptionOr<void> set(String const& name, GC::Ref<FileAPI::Blob> const& blob_value, Optional<String> const& filename = {});

    Vector<FormDataEntry> const& entry_list() const { return m_entry_list; }

    using ForEachCallback = Function<JS::ThrowCompletionOr<void>(String const&, FormDataEntryValue const&)>;
    JS::ThrowCompletionOr<void> for_each(ForEachCallback);

private:
    friend class FormDataIterator;

    explicit FormData(JS::Realm&, Vector<FormDataEntry> entry_list = {});

    virtual void initialize(JS::Realm&) override;

    WebIDL::ExceptionOr<void> append_impl(String const& name, Variant<GC::Ref<FileAPI::Blob>, String> const& value, Optional<String> const& filename = {});
    WebIDL::ExceptionOr<void> set_impl(String const& name, Variant<GC::Ref<FileAPI::Blob>, String> const& value, Optional<String> const& filename = {});

    Vector<FormDataEntry> m_entry_list;
};

}
