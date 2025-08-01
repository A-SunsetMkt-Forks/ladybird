/*
 * Copyright (c) 2018-2025, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2020-2021, the SerenityOS developers.
 * Copyright (c) 2021-2025, Sam Atkins <sam@ladybird.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2022, MacDue <macdue@dueutil.tech>
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 * Copyright (c) 2024, Tommy van der Vorst <tommy@pixelspark.nl>
 * Copyright (c) 2024, Matthew Olsson <mattco@serenityos.org>
 * Copyright (c) 2024, Glenn Skrzypczak <glenn.skrzypczak@gmail.com>
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/QuickSort.h>
#include <LibWeb/CSS/CSSStyleValue.h>
#include <LibWeb/CSS/CharacterTypes.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/StyleValues/AngleStyleValue.h>
#include <LibWeb/CSS/StyleValues/BackgroundRepeatStyleValue.h>
#include <LibWeb/CSS/StyleValues/BackgroundSizeStyleValue.h>
#include <LibWeb/CSS/StyleValues/BorderImageSliceStyleValue.h>
#include <LibWeb/CSS/StyleValues/BorderRadiusStyleValue.h>
#include <LibWeb/CSS/StyleValues/CSSColorValue.h>
#include <LibWeb/CSS/StyleValues/CSSKeywordValue.h>
#include <LibWeb/CSS/StyleValues/ColorSchemeStyleValue.h>
#include <LibWeb/CSS/StyleValues/ContentStyleValue.h>
#include <LibWeb/CSS/StyleValues/CounterDefinitionsStyleValue.h>
#include <LibWeb/CSS/StyleValues/CursorStyleValue.h>
#include <LibWeb/CSS/StyleValues/CustomIdentStyleValue.h>
#include <LibWeb/CSS/StyleValues/DisplayStyleValue.h>
#include <LibWeb/CSS/StyleValues/EasingStyleValue.h>
#include <LibWeb/CSS/StyleValues/EdgeStyleValue.h>
#include <LibWeb/CSS/StyleValues/FilterValueListStyleValue.h>
#include <LibWeb/CSS/StyleValues/FitContentStyleValue.h>
#include <LibWeb/CSS/StyleValues/FlexStyleValue.h>
#include <LibWeb/CSS/StyleValues/FontStyleStyleValue.h>
#include <LibWeb/CSS/StyleValues/FrequencyStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridAutoFlowStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTemplateAreaStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTrackPlacementStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTrackSizeListStyleValue.h>
#include <LibWeb/CSS/StyleValues/IntegerStyleValue.h>
#include <LibWeb/CSS/StyleValues/LengthStyleValue.h>
#include <LibWeb/CSS/StyleValues/MathDepthStyleValue.h>
#include <LibWeb/CSS/StyleValues/NumberStyleValue.h>
#include <LibWeb/CSS/StyleValues/OpenTypeTaggedStyleValue.h>
#include <LibWeb/CSS/StyleValues/PercentageStyleValue.h>
#include <LibWeb/CSS/StyleValues/PositionStyleValue.h>
#include <LibWeb/CSS/StyleValues/ResolutionStyleValue.h>
#include <LibWeb/CSS/StyleValues/ScrollbarColorStyleValue.h>
#include <LibWeb/CSS/StyleValues/ScrollbarGutterStyleValue.h>
#include <LibWeb/CSS/StyleValues/ShadowStyleValue.h>
#include <LibWeb/CSS/StyleValues/ShorthandStyleValue.h>
#include <LibWeb/CSS/StyleValues/StringStyleValue.h>
#include <LibWeb/CSS/StyleValues/StyleValueList.h>
#include <LibWeb/CSS/StyleValues/TimeStyleValue.h>
#include <LibWeb/CSS/StyleValues/TransformationStyleValue.h>
#include <LibWeb/CSS/StyleValues/TransitionStyleValue.h>
#include <LibWeb/CSS/StyleValues/URLStyleValue.h>
#include <LibWeb/CSS/StyleValues/UnresolvedStyleValue.h>
#include <LibWeb/Dump.h>
#include <LibWeb/Infra/Strings.h>

namespace Web::CSS::Parser {

static void remove_property(Vector<PropertyID>& properties, PropertyID property_to_remove)
{
    properties.remove_first_matching([&](auto it) { return it == property_to_remove; });
}

RefPtr<CSSStyleValue const> Parser::parse_all_as_single_keyword_value(TokenStream<ComponentValue>& tokens, Keyword keyword)
{
    auto transaction = tokens.begin_transaction();
    tokens.discard_whitespace();
    auto keyword_value = parse_keyword_value(tokens);
    tokens.discard_whitespace();

    if (tokens.has_next_token() || !keyword_value || keyword_value->to_keyword() != keyword)
        return {};

    transaction.commit();
    return keyword_value;
}

RefPtr<CSSStyleValue const> Parser::parse_simple_comma_separated_value_list(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    return parse_comma_separated_value_list(tokens, [this, property_id](auto& tokens) -> RefPtr<CSSStyleValue const> {
        if (auto value = parse_css_value_for_property(property_id, tokens))
            return value;
        tokens.reconsume_current_input_token();
        return nullptr;
    });
}

RefPtr<CSSStyleValue const> Parser::parse_css_value_for_property(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    return parse_css_value_for_properties({ &property_id, 1 }, tokens)
        .map([](auto& it) { return it.style_value; })
        .value_or(nullptr);
}

Optional<Parser::PropertyAndValue> Parser::parse_css_value_for_properties(ReadonlySpan<PropertyID> property_ids, TokenStream<ComponentValue>& tokens)
{
    auto any_property_accepts_type = [](ReadonlySpan<PropertyID> property_ids, ValueType value_type) -> Optional<PropertyID> {
        for (auto const& property : property_ids) {
            if (property_accepts_type(property, value_type))
                return property;
        }
        return {};
    };
    auto any_property_accepts_keyword = [](ReadonlySpan<PropertyID> property_ids, Keyword keyword) -> Optional<PropertyID> {
        for (auto const& property : property_ids) {
            if (property_accepts_keyword(property, keyword))
                return property;
        }
        return {};
    };

    auto& peek_token = tokens.next_token();

    auto parse_for_type = [&](ValueType const type) -> Optional<PropertyAndValue> {
        if (auto property = any_property_accepts_type(property_ids, type); property.has_value()) {
            auto context_guard = push_temporary_value_parsing_context(*property);
            if (auto maybe_easing_function = parse_value(type, tokens))
                return PropertyAndValue { *property, maybe_easing_function };
        }
        return OptionalNone {};
    };

    if (auto parsed = parse_for_type(ValueType::EasingFunction); parsed.has_value())
        return parsed.release_value();

    if (peek_token.is(Token::Type::Ident)) {
        // NOTE: We do not try to parse "CSS-wide keywords" here. https://www.w3.org/TR/css-values-4/#common-keywords
        //       These are only valid on their own, and so should be parsed directly in `parse_css_value()`.
        auto keyword = keyword_from_string(peek_token.token().ident());
        if (keyword.has_value()) {
            if (auto property = any_property_accepts_keyword(property_ids, keyword.value()); property.has_value()) {
                tokens.discard_a_token();
                if (auto resolved_keyword = resolve_legacy_value_alias(property.value(), keyword.value()); resolved_keyword.has_value())
                    return PropertyAndValue { *property, CSSKeywordValue::create(resolved_keyword.value()) };
                return PropertyAndValue { *property, CSSKeywordValue::create(keyword.value()) };
            }
        }

        // Custom idents
        if (auto property = any_property_accepts_type(property_ids, ValueType::CustomIdent); property.has_value()) {
            auto context_guard = push_temporary_value_parsing_context(*property);
            if (auto custom_ident = parse_custom_ident_value(tokens, property_custom_ident_blacklist(*property)))
                return PropertyAndValue { *property, custom_ident };
        }
    }

    if (auto parsed = parse_for_type(ValueType::Color); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::Counter); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::Image); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::Position); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::BackgroundPosition); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::BasicShape); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::Ratio); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::OpenTypeTag); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::Rect); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::String); parsed.has_value())
        return parsed.release_value();
    if (auto parsed = parse_for_type(ValueType::Url); parsed.has_value())
        return parsed.release_value();

    // <integer>/<number> come before <length>, so that 0 is not interpreted as a <length> in case both are allowed.
    if (auto property = any_property_accepts_type(property_ids, ValueType::Integer); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (auto value = parse_integer_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_integer() && property_accepts_integer(*property, value->as_integer().integer())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Number); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (auto value = parse_number_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_number() && property_accepts_number(*property, value->as_number().number())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Angle); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (property_accepts_type(*property, ValueType::Percentage)) {
            if (auto value = parse_angle_percentage_value(tokens)) {
                if (value->is_calculated()) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_angle() && property_accepts_angle(*property, value->as_angle().angle())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_percentage() && property_accepts_percentage(*property, value->as_percentage().percentage())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
            }
        }
        if (auto value = parse_angle_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_angle() && property_accepts_angle(*property, value->as_angle().angle())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Flex); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (auto value = parse_flex_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_flex() && property_accepts_flex(*property, value->as_flex().flex())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Frequency); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (property_accepts_type(*property, ValueType::Percentage)) {
            if (auto value = parse_frequency_percentage_value(tokens)) {
                if (value->is_calculated()) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_frequency() && property_accepts_frequency(*property, value->as_frequency().frequency())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_percentage() && property_accepts_percentage(*property, value->as_percentage().percentage())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
            }
        }
        if (auto value = parse_frequency_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_frequency() && property_accepts_frequency(*property, value->as_frequency().frequency())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto parsed = parse_for_type(ValueType::FitContent); parsed.has_value())
        return parsed.release_value();

    if (auto property = any_property_accepts_type(property_ids, ValueType::Length); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (property_accepts_type(*property, ValueType::Percentage)) {
            if (auto value = parse_length_percentage_value(tokens)) {
                if (value->is_calculated() || value->is_anchor_size()) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_length() && property_accepts_length(*property, value->as_length().length())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_percentage() && property_accepts_percentage(*property, value->as_percentage().percentage())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
            }
        }
        if (auto value = parse_length_value(tokens)) {
            if (value->is_calculated() || value->is_anchor_size()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_length() && property_accepts_length(*property, value->as_length().length())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Resolution); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (auto value = parse_resolution_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_resolution() && property_accepts_resolution(*property, value->as_resolution().resolution())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Time); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (property_accepts_type(*property, ValueType::Percentage)) {
            if (auto value = parse_time_percentage_value(tokens)) {
                if (value->is_calculated()) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_time() && property_accepts_time(*property, value->as_time().time())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
                if (value->is_percentage() && property_accepts_percentage(*property, value->as_percentage().percentage())) {
                    transaction.commit();
                    return PropertyAndValue { *property, value };
                }
            }
        }
        if (auto value = parse_time_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_time() && property_accepts_time(*property, value->as_time().time())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    // <percentage> is checked after the <foo-percentage> types.
    if (auto property = any_property_accepts_type(property_ids, ValueType::Percentage); property.has_value()) {
        auto context_guard = push_temporary_value_parsing_context(*property);
        auto transaction = tokens.begin_transaction();
        if (auto value = parse_percentage_value(tokens)) {
            if (value->is_calculated()) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
            if (value->is_percentage() && property_accepts_percentage(*property, value->as_percentage().percentage())) {
                transaction.commit();
                return PropertyAndValue { *property, value };
            }
        }
    }

    if (auto parsed = parse_for_type(ValueType::Paint); parsed.has_value())
        return parsed.release_value();

    return OptionalNone {};
}

Parser::ParseErrorOr<NonnullRefPtr<CSSStyleValue const>> Parser::parse_css_value(PropertyID property_id, TokenStream<ComponentValue>& unprocessed_tokens, Optional<String> original_source_text)
{
    auto context_guard = push_temporary_value_parsing_context(property_id);

    // FIXME: Stop removing whitespace here. It's less helpful than it seems.
    Vector<ComponentValue> component_values;
    SubstitutionFunctionsPresence substitution_presence;
    bool const property_accepts_custom_ident = property_accepts_type(property_id, ValueType::CustomIdent);

    while (unprocessed_tokens.has_next_token()) {
        auto const& token = unprocessed_tokens.consume_a_token();

        if (token.is(Token::Type::Semicolon)) {
            unprocessed_tokens.reconsume_current_input_token();
            return ParseError::SyntaxError;
        }

        if (property_id != PropertyID::Custom) {
            if (token.is(Token::Type::Whitespace))
                continue;

            if (!property_accepts_custom_ident && token.is(Token::Type::Ident) && has_ignored_vendor_prefix(token.token().ident()))
                return ParseError::IncludesIgnoredVendorPrefix;
        }

        if (token.is_function())
            token.function().contains_arbitrary_substitution_function(substitution_presence);
        else if (token.is_block())
            token.block().contains_arbitrary_substitution_function(substitution_presence);

        component_values.append(token);
    }

    if (component_values.size() == 1) {
        auto tokens = TokenStream { component_values };
        if (auto parsed_value = parse_builtin_value(tokens))
            return parsed_value.release_nonnull();
    }

    if (property_id == PropertyID::Custom || substitution_presence.has_any())
        return UnresolvedStyleValue::create(move(component_values), substitution_presence, original_source_text);

    if (component_values.is_empty())
        return ParseError::SyntaxError;

    auto tokens = TokenStream { component_values };

    // Special-case property handling
    switch (property_id) {
    case PropertyID::All:
        // NOTE: The 'all' property, unlike some other shorthands, doesn't support directly listing sub-property
        //       values, only the CSS-wide keywords - this is handled above, and thus, if we have gotten to here, there
        //       is an invalid value which is a syntax error.
        return ParseError::SyntaxError;
    case PropertyID::AspectRatio:
        if (auto parsed_value = parse_aspect_ratio_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackdropFilter:
    case PropertyID::Filter:
        if (auto parsed_value = parse_filter_value_list_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Background:
        if (auto parsed_value = parse_background_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundAttachment:
    case PropertyID::BackgroundBlendMode:
    case PropertyID::BackgroundClip:
    case PropertyID::BackgroundImage:
    case PropertyID::BackgroundOrigin:
        if (auto parsed_value = parse_simple_comma_separated_value_list(property_id, tokens))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundPosition:
        if (auto parsed_value = parse_comma_separated_value_list(tokens, [this](auto& tokens) { return parse_position_value(tokens, PositionParsingMode::BackgroundPosition); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundPositionX:
    case PropertyID::BackgroundPositionY:
        if (auto parsed_value = parse_comma_separated_value_list(tokens, [this, property_id](auto& tokens) { return parse_single_background_position_x_or_y_value(tokens, property_id); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundRepeat:
        if (auto parsed_value = parse_comma_separated_value_list(tokens, [this](auto& tokens) { return parse_single_background_repeat_value(tokens); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundSize:
        if (auto parsed_value = parse_comma_separated_value_list(tokens, [this](auto& tokens) { return parse_single_background_size_value(tokens); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Border:
    case PropertyID::BorderBlock:
    case PropertyID::BorderInline:
        if (auto parsed_value = parse_border_value(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BorderImage:
        if (auto parsed_value = parse_border_image_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BorderImageSlice:
        if (auto parsed_value = parse_border_image_slice_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BorderTopLeftRadius:
    case PropertyID::BorderTopRightRadius:
    case PropertyID::BorderBottomRightRadius:
    case PropertyID::BorderBottomLeftRadius:
        if (auto parsed_value = parse_border_radius_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BorderRadius:
        if (auto parsed_value = parse_border_radius_shorthand_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BoxShadow:
        if (auto parsed_value = parse_shadow_value(tokens, AllowInsetKeyword::Yes); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::ColorScheme:
        if (auto parsed_value = parse_color_scheme_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Columns:
        if (auto parsed_value = parse_columns_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Content:
        if (auto parsed_value = parse_content_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::CounterIncrement:
        if (auto parsed_value = parse_counter_increment_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::CounterReset:
        if (auto parsed_value = parse_counter_reset_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::CounterSet:
        if (auto parsed_value = parse_counter_set_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Cursor:
        if (auto parsed_value = parse_cursor_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Display:
        if (auto parsed_value = parse_display_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Flex:
        if (auto parsed_value = parse_flex_shorthand_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FlexFlow:
        if (auto parsed_value = parse_flex_flow_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Font:
        if (auto parsed_value = parse_font_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontFamily:
        if (auto parsed_value = parse_font_family_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontFeatureSettings:
        if (auto parsed_value = parse_font_feature_settings_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontLanguageOverride:
        if (auto parsed_value = parse_font_language_override_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontStyle:
        if (auto parsed_value = parse_font_style_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontVariationSettings:
        if (auto parsed_value = parse_font_variation_settings_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontVariant:
        if (auto parsed_value = parse_font_variant(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontVariantAlternates:
        if (auto parsed_value = parse_font_variant_alternates_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontVariantEastAsian:
        if (auto parsed_value = parse_font_variant_east_asian_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontVariantLigatures:
        if (auto parsed_value = parse_font_variant_ligatures_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontVariantNumeric:
        if (auto parsed_value = parse_font_variant_numeric_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridArea:
        if (auto parsed_value = parse_grid_area_shorthand_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridAutoFlow:
        if (auto parsed_value = parse_grid_auto_flow_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridColumn:
        if (auto parsed_value = parse_grid_track_placement_shorthand_value(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridColumnEnd:
        if (auto parsed_value = parse_grid_track_placement(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridColumnStart:
        if (auto parsed_value = parse_grid_track_placement(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridRow:
        if (auto parsed_value = parse_grid_track_placement_shorthand_value(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridRowEnd:
        if (auto parsed_value = parse_grid_track_placement(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridRowStart:
        if (auto parsed_value = parse_grid_track_placement(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Grid:
        if (auto parsed_value = parse_grid_shorthand_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplate:
        if (auto parsed_value = parse_grid_track_size_list_shorthand_value(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplateAreas:
        if (auto parsed_value = parse_grid_template_areas_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplateColumns:
        if (auto parsed_value = parse_grid_track_size_list(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplateRows:
        if (auto parsed_value = parse_grid_track_size_list(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridAutoColumns:
        if (auto parsed_value = parse_grid_auto_track_sizes(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridAutoRows:
        if (auto parsed_value = parse_grid_auto_track_sizes(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::ListStyle:
        if (auto parsed_value = parse_list_style_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::MathDepth:
        if (auto parsed_value = parse_math_depth_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Opacity:
    case PropertyID::FillOpacity:
    case PropertyID::FloodOpacity:
    case PropertyID::StopOpacity:
    case PropertyID::StrokeOpacity:
        if (auto parsed_value = parse_opacity_value(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    // FIXME: This can be removed once we have generic logic for parsing "positional-value-list-shorthand"s
    case PropertyID::Overflow:
        if (auto parsed_value = parse_overflow_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::PlaceContent:
        if (auto parsed_value = parse_place_content_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::PlaceItems:
        if (auto parsed_value = parse_place_items_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::PlaceSelf:
        if (auto parsed_value = parse_place_self_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Quotes:
        if (auto parsed_value = parse_quotes_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Rotate:
        if (auto parsed_value = parse_rotate_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::ScrollbarColor:
        if (auto parsed_value = parse_scrollbar_color_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::ScrollbarGutter:
        if (auto parsed_value = parse_scrollbar_gutter_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::StrokeDasharray:
        if (auto parsed_value = parse_stroke_dasharray_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TextDecoration:
        if (auto parsed_value = parse_text_decoration_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TextDecorationLine:
        if (auto parsed_value = parse_text_decoration_line_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TextShadow:
        if (auto parsed_value = parse_shadow_value(tokens, AllowInsetKeyword::No); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TouchAction:
        if (auto parsed_value = parse_touch_action_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Transform:
        if (auto parsed_value = parse_transform_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TransformOrigin:
        if (auto parsed_value = parse_transform_origin_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Transition:
        if (auto parsed_value = parse_transition_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TransitionDelay:
        if (auto parsed_value = parse_list_of_time_values(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TransitionDuration:
        if (auto parsed_value = parse_list_of_time_values(property_id, tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TransitionProperty:
        if (auto parsed_value = parse_transition_property_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TransitionTimingFunction:
        if (auto parsed_value = parse_comma_separated_value_list(tokens, [this](auto& tokens) { return parse_easing_value(tokens); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Translate:
        if (auto parsed_value = parse_translate_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Scale:
        if (auto parsed_value = parse_scale_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Contain:
        if (auto parsed_value = parse_contain_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::WhiteSpace:
        if (auto parsed_value = parse_white_space_shorthand(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::WhiteSpaceTrim:
        if (auto parsed_value = parse_white_space_trim_value(tokens); parsed_value && !tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    default:
        break;
    }

    // If there's only 1 ComponentValue, we can only produce a single CSSStyleValue.
    if (component_values.size() == 1) {
        auto stream = TokenStream { component_values };
        if (auto parsed_value = parse_css_value_for_property(property_id, stream))
            return parsed_value.release_nonnull();
    } else {
        StyleValueVector parsed_values;
        auto stream = TokenStream { component_values };
        while (auto parsed_value = parse_css_value_for_property(property_id, stream)) {
            parsed_values.append(parsed_value.release_nonnull());
            if (!stream.has_next_token())
                break;
        }

        if (!stream.has_next_token()) {
            // Some types (such as <ratio>) can be made from multiple ComponentValues, so if we only made 1 CSSStyleValue, return it directly.
            if (parsed_values.size() == 1)
                return *parsed_values.take_first();

            if (!parsed_values.is_empty() && parsed_values.size() <= property_maximum_value_count(property_id))
                return StyleValueList::create(move(parsed_values), StyleValueList::Separator::Space);
        }
    }

    // We have multiple values, but the property claims to accept only a single one, check if it's a shorthand property.
    auto unassigned_properties = longhands_for_shorthand(property_id);
    if (unassigned_properties.is_empty())
        return ParseError::SyntaxError;

    auto stream = TokenStream { component_values };

    HashMap<UnderlyingType<PropertyID>, Vector<ValueComparingNonnullRefPtr<CSSStyleValue const>>> assigned_values;

    while (stream.has_next_token() && !unassigned_properties.is_empty()) {
        auto property_and_value = parse_css_value_for_properties(unassigned_properties, stream);
        if (property_and_value.has_value()) {
            auto property = property_and_value->property;
            auto value = property_and_value->style_value;
            auto& values = assigned_values.ensure(to_underlying(property));
            if (values.size() + 1 == property_maximum_value_count(property)) {
                // We're done with this property, move on to the next one.
                unassigned_properties.remove_first_matching([&](auto& unassigned_property) { return unassigned_property == property; });
            }

            values.append(value.release_nonnull());
            continue;
        }

        // No property matched, so we're done.
        if constexpr (CSS_PARSER_DEBUG) {
            dbgln("No property (from {} properties) matched {}", unassigned_properties.size(), stream.next_token().to_debug_string());
            for (auto id : unassigned_properties)
                dbgln("    {}", string_from_property_id(id));
        }
        break;
    }

    for (auto& property : unassigned_properties)
        assigned_values.ensure(to_underlying(property)).append(property_initial_value(property));

    stream.discard_whitespace();
    if (stream.has_next_token())
        return ParseError::SyntaxError;

    Vector<PropertyID> longhand_properties;
    longhand_properties.ensure_capacity(assigned_values.size());
    for (auto& it : assigned_values)
        longhand_properties.unchecked_append(static_cast<PropertyID>(it.key));

    StyleValueVector longhand_values;
    longhand_values.ensure_capacity(assigned_values.size());
    for (auto& it : assigned_values) {
        if (it.value.size() == 1)
            longhand_values.unchecked_append(it.value.take_first());
        else
            longhand_values.unchecked_append(StyleValueList::create(move(it.value), StyleValueList::Separator::Space));
    }

    return { ShorthandStyleValue::create(property_id, move(longhand_properties), move(longhand_values)) };
}

RefPtr<CSSStyleValue const> Parser::parse_color_scheme_value(TokenStream<ComponentValue>& tokens)
{
    // normal | [ light | dark | <custom-ident> ]+ && only?

    // normal
    {
        auto transaction = tokens.begin_transaction();
        tokens.discard_whitespace();
        if (tokens.consume_a_token().is_ident("normal"sv)) {
            if (tokens.has_next_token())
                return {};
            transaction.commit();
            return ColorSchemeStyleValue::normal();
        }
    }

    bool only = false;
    Vector<String> schemes;

    // only? && (..)
    {
        auto transaction = tokens.begin_transaction();
        tokens.discard_whitespace();
        if (tokens.consume_a_token().is_ident("only"sv)) {
            only = true;
            transaction.commit();
        }
    }

    // [ light | dark | <custom-ident> ]+
    tokens.discard_whitespace();
    while (tokens.has_next_token()) {
        auto transaction = tokens.begin_transaction();

        // The 'normal', 'light', 'dark', and 'only' keywords are not valid <custom-ident>s in this property.
        // Note: only 'normal' is blacklisted here because 'light' and 'dark' aren't parsed differently and 'only' is checked for afterwards
        auto ident = parse_custom_ident_value(tokens, { { "normal"sv } });
        if (!ident)
            return {};

        if (ident->custom_ident() == "only"_fly_string)
            break;

        schemes.append(ident->custom_ident().to_string());
        tokens.discard_whitespace();
        transaction.commit();
    }

    // (..) && only?
    if (!only) {
        auto transaction = tokens.begin_transaction();
        tokens.discard_whitespace();
        if (tokens.consume_a_token().is_ident("only"sv)) {
            only = true;
            transaction.commit();
        }
    }

    tokens.discard_whitespace();
    if (tokens.has_next_token() || schemes.is_empty())
        return {};

    return ColorSchemeStyleValue::create(schemes, only);
}

RefPtr<CSSStyleValue const> Parser::parse_counter_definitions_value(TokenStream<ComponentValue>& tokens, AllowReversed allow_reversed, i32 default_value_if_not_reversed)
{
    // If AllowReversed is Yes, parses:
    //   [ <counter-name> <integer>? | <reversed-counter-name> <integer>? ]+
    // Otherwise parses:
    //   [ <counter-name> <integer>? ]+

    // FIXME: This disabled parsing of `reversed()` counters. Remove this line once they're supported.
    allow_reversed = AllowReversed::No;

    auto transaction = tokens.begin_transaction();
    tokens.discard_whitespace();

    Vector<CounterDefinition> counter_definitions;
    while (tokens.has_next_token()) {
        auto per_item_transaction = tokens.begin_transaction();
        CounterDefinition definition {};

        // <counter-name> | <reversed-counter-name>
        auto& token = tokens.next_token();

        // A <counter-name> name cannot match the keyword none; such an identifier is invalid as a <counter-name>.
        if (auto counter_name = parse_custom_ident_value(tokens, { { "none"sv } })) {
            definition.name = counter_name->custom_ident();
            definition.is_reversed = false;
        } else if (allow_reversed == AllowReversed::Yes && token.is_function("reversed"sv)) {
            TokenStream function_tokens { token.function().value };
            tokens.discard_a_token();
            function_tokens.discard_whitespace();
            auto& name_token = function_tokens.consume_a_token();
            if (!name_token.is(Token::Type::Ident))
                break;
            function_tokens.discard_whitespace();
            if (function_tokens.has_next_token())
                break;

            definition.name = name_token.token().ident();
            definition.is_reversed = true;
        } else {
            break;
        }
        tokens.discard_whitespace();

        // <integer>?
        definition.value = parse_integer_value(tokens);
        if (!definition.value && !definition.is_reversed)
            definition.value = IntegerStyleValue::create(default_value_if_not_reversed);

        counter_definitions.append(move(definition));
        tokens.discard_whitespace();
        per_item_transaction.commit();
    }

    if (counter_definitions.is_empty())
        return {};

    transaction.commit();
    return CounterDefinitionsStyleValue::create(move(counter_definitions));
}

// https://drafts.csswg.org/css-lists-3/#propdef-counter-increment
RefPtr<CSSStyleValue const> Parser::parse_counter_increment_value(TokenStream<ComponentValue>& tokens)
{
    // [ <counter-name> <integer>? ]+ | none
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    return parse_counter_definitions_value(tokens, AllowReversed::No, 1);
}

// https://drafts.csswg.org/css-lists-3/#propdef-counter-reset
RefPtr<CSSStyleValue const> Parser::parse_counter_reset_value(TokenStream<ComponentValue>& tokens)
{
    // [ <counter-name> <integer>? | <reversed-counter-name> <integer>? ]+ | none
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    return parse_counter_definitions_value(tokens, AllowReversed::Yes, 0);
}

// https://drafts.csswg.org/css-lists-3/#propdef-counter-set
RefPtr<CSSStyleValue const> Parser::parse_counter_set_value(TokenStream<ComponentValue>& tokens)
{
    // [ <counter-name> <integer>? ]+ | none
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    return parse_counter_definitions_value(tokens, AllowReversed::No, 0);
}

// https://drafts.csswg.org/css-ui-4/#cursor
RefPtr<CSSStyleValue const> Parser::parse_cursor_value(TokenStream<ComponentValue>& tokens)
{
    // <cursor-image>#? <cursor-predefined>
    // <cursor-image> = <url> <number>{2}?
    // So, any number of custom cursor definitions, and then a mandatory cursor name keyword, all comma-separated.

    auto transaction = tokens.begin_transaction();

    StyleValueVector cursors;

    auto parts = parse_a_comma_separated_list_of_component_values(tokens);
    for (auto i = 0u; i < parts.size(); ++i) {
        auto& part = parts[i];
        TokenStream part_tokens { part };

        if (i == parts.size() - 1) {
            // Cursor keyword
            part_tokens.discard_whitespace();
            auto keyword_value = parse_keyword_value(part_tokens);
            if (!keyword_value || !keyword_to_cursor_predefined(keyword_value->to_keyword()).has_value())
                return {};

            part_tokens.discard_whitespace();
            if (part_tokens.has_next_token())
                return {};

            cursors.append(keyword_value.release_nonnull());
        } else {
            // Custom cursor definition
            // <cursor-image> = <url> <number>{2}?
            // "Conforming user agents may, instead of <url>, support <image> which is a superset."

            part_tokens.discard_whitespace();
            auto image_value = parse_image_value(part_tokens);
            if (!image_value)
                return {};

            part_tokens.discard_whitespace();

            if (part_tokens.has_next_token()) {
                // <number>{2}, which are the x and y coordinates of the hotspot
                auto x = parse_number(part_tokens);
                part_tokens.discard_whitespace();
                auto y = parse_number(part_tokens);
                part_tokens.discard_whitespace();
                if (!x.has_value() || !y.has_value() || part_tokens.has_next_token())
                    return nullptr;

                cursors.append(CursorStyleValue::create(image_value.release_nonnull(), x.release_value(), y.release_value()));
                continue;
            }

            cursors.append(CursorStyleValue::create(image_value.release_nonnull(), {}, {}));
        }
    }

    if (cursors.is_empty())
        return nullptr;

    transaction.commit();
    if (cursors.size() == 1)
        return *cursors.first();

    return StyleValueList::create(move(cursors), StyleValueList::Separator::Comma);
}

// https://www.w3.org/TR/css-sizing-4/#aspect-ratio
RefPtr<CSSStyleValue const> Parser::parse_aspect_ratio_value(TokenStream<ComponentValue>& tokens)
{
    // `auto || <ratio>`
    RefPtr<CSSStyleValue const> auto_value;
    RefPtr<CSSStyleValue const> ratio_value;

    auto transaction = tokens.begin_transaction();
    while (tokens.has_next_token()) {
        auto maybe_value = parse_css_value_for_property(PropertyID::AspectRatio, tokens);
        if (!maybe_value)
            return nullptr;

        if (maybe_value->is_ratio()) {
            if (ratio_value)
                return nullptr;
            ratio_value = maybe_value.release_nonnull();
            continue;
        }

        if (maybe_value->is_keyword() && maybe_value->as_keyword().keyword() == Keyword::Auto) {
            if (auto_value)
                return nullptr;
            auto_value = maybe_value.release_nonnull();
            continue;
        }

        return nullptr;
    }

    if (auto_value && ratio_value) {
        transaction.commit();
        return StyleValueList::create(
            StyleValueVector { auto_value.release_nonnull(), ratio_value.release_nonnull() },
            StyleValueList::Separator::Space);
    }

    if (ratio_value) {
        transaction.commit();
        return ratio_value.release_nonnull();
    }

    if (auto_value) {
        transaction.commit();
        return auto_value.release_nonnull();
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_background_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto make_background_shorthand = [&](auto background_color, auto background_image, auto background_position, auto background_size, auto background_repeat, auto background_attachment, auto background_origin, auto background_clip) {
        return ShorthandStyleValue::create(PropertyID::Background,
            { PropertyID::BackgroundColor, PropertyID::BackgroundImage, PropertyID::BackgroundPosition, PropertyID::BackgroundSize, PropertyID::BackgroundRepeat, PropertyID::BackgroundAttachment, PropertyID::BackgroundOrigin, PropertyID::BackgroundClip },
            { move(background_color), move(background_image), move(background_position), move(background_size), move(background_repeat), move(background_attachment), move(background_origin), move(background_clip) });
    };

    StyleValueVector background_images;
    StyleValueVector background_position_xs;
    StyleValueVector background_position_ys;
    StyleValueVector background_sizes;
    StyleValueVector background_repeats;
    StyleValueVector background_attachments;
    StyleValueVector background_clips;
    StyleValueVector background_origins;
    RefPtr<CSSStyleValue const> background_color;

    auto initial_background_image = property_initial_value(PropertyID::BackgroundImage);
    auto initial_background_position_x = property_initial_value(PropertyID::BackgroundPositionX);
    auto initial_background_position_y = property_initial_value(PropertyID::BackgroundPositionY);
    auto initial_background_size = property_initial_value(PropertyID::BackgroundSize);
    auto initial_background_repeat = property_initial_value(PropertyID::BackgroundRepeat);
    auto initial_background_attachment = property_initial_value(PropertyID::BackgroundAttachment);
    auto initial_background_clip = property_initial_value(PropertyID::BackgroundClip);
    auto initial_background_origin = property_initial_value(PropertyID::BackgroundOrigin);
    auto initial_background_color = property_initial_value(PropertyID::BackgroundColor);

    // Per-layer values
    RefPtr<CSSStyleValue const> background_image;
    RefPtr<CSSStyleValue const> background_position_x;
    RefPtr<CSSStyleValue const> background_position_y;
    RefPtr<CSSStyleValue const> background_size;
    RefPtr<CSSStyleValue const> background_repeat;
    RefPtr<CSSStyleValue const> background_attachment;
    RefPtr<CSSStyleValue const> background_clip;
    RefPtr<CSSStyleValue const> background_origin;

    bool has_multiple_layers = false;
    // BackgroundSize is always parsed as part of BackgroundPosition, so we don't include it here.
    Vector<PropertyID> remaining_layer_properties {
        PropertyID::BackgroundAttachment,
        PropertyID::BackgroundClip,
        PropertyID::BackgroundColor,
        PropertyID::BackgroundImage,
        PropertyID::BackgroundOrigin,
        PropertyID::BackgroundPosition,
        PropertyID::BackgroundRepeat,
    };

    auto background_layer_is_valid = [&](bool allow_background_color) -> bool {
        if (allow_background_color) {
            if (background_color)
                return true;
        } else {
            if (background_color)
                return false;
        }
        return background_image || background_position_x || background_position_y || background_size || background_repeat || background_attachment || background_clip || background_origin;
    };

    auto complete_background_layer = [&]() {
        background_images.append(background_image ? background_image.release_nonnull() : initial_background_image);
        background_position_xs.append(background_position_x ? background_position_x.release_nonnull() : initial_background_position_x);
        background_position_ys.append(background_position_y ? background_position_y.release_nonnull() : initial_background_position_y);
        background_sizes.append(background_size ? background_size.release_nonnull() : initial_background_size);
        background_repeats.append(background_repeat ? background_repeat.release_nonnull() : initial_background_repeat);
        background_attachments.append(background_attachment ? background_attachment.release_nonnull() : initial_background_attachment);

        if (!background_origin && !background_clip) {
            background_origin = initial_background_origin;
            background_clip = initial_background_clip;
        } else if (!background_clip) {
            background_clip = background_origin;
        }
        background_origins.append(background_origin.release_nonnull());
        background_clips.append(background_clip.release_nonnull());

        background_image = nullptr;
        background_position_x = nullptr;
        background_position_y = nullptr;
        background_size = nullptr;
        background_repeat = nullptr;
        background_attachment = nullptr;
        background_clip = nullptr;
        background_origin = nullptr;

        remaining_layer_properties.clear_with_capacity();
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundAttachment);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundClip);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundColor);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundImage);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundOrigin);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundPosition);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundRepeat);
    };

    while (tokens.has_next_token()) {
        if (tokens.next_token().is(Token::Type::Comma)) {
            has_multiple_layers = true;
            if (!background_layer_is_valid(false))
                return nullptr;
            complete_background_layer();
            tokens.discard_a_token();
            continue;
        }

        auto value_and_property = parse_css_value_for_properties(remaining_layer_properties, tokens);
        if (!value_and_property.has_value())
            return nullptr;
        auto& value = value_and_property->style_value;
        remove_property(remaining_layer_properties, value_and_property->property);

        switch (value_and_property->property) {
        case PropertyID::BackgroundAttachment:
            VERIFY(!background_attachment);
            background_attachment = value.release_nonnull();
            continue;
        case PropertyID::BackgroundColor:
            VERIFY(!background_color);
            background_color = value.release_nonnull();
            continue;
        case PropertyID::BackgroundImage:
            VERIFY(!background_image);
            background_image = value.release_nonnull();
            continue;
        case PropertyID::BackgroundClip:
        case PropertyID::BackgroundOrigin: {
            // background-origin and background-clip accept the same values. From the spec:
            //   "If one <box> value is present then it sets both background-origin and background-clip to that value.
            //    If two values are present, then the first sets background-origin and the second background-clip."
            //        - https://www.w3.org/TR/css-backgrounds-3/#background
            // So, we put the first one in background-origin, then if we get a second, we put it in background-clip.
            // If we only get one, we copy the value before creating the ShorthandStyleValue.
            if (!background_origin) {
                background_origin = value.release_nonnull();
            } else if (!background_clip) {
                background_clip = value.release_nonnull();
            } else {
                VERIFY_NOT_REACHED();
            }
            continue;
        }
        case PropertyID::BackgroundPosition: {
            VERIFY(!background_position_x && !background_position_y);
            auto position = value.release_nonnull();
            background_position_x = position->as_position().edge_x();
            background_position_y = position->as_position().edge_y();

            // Attempt to parse `/ <background-size>`
            auto background_size_transaction = tokens.begin_transaction();
            auto& maybe_slash = tokens.consume_a_token();
            if (maybe_slash.is_delim('/')) {
                if (auto maybe_background_size = parse_single_background_size_value(tokens)) {
                    background_size_transaction.commit();
                    background_size = maybe_background_size.release_nonnull();
                    continue;
                }
                return nullptr;
            }
            continue;
        }
        case PropertyID::BackgroundRepeat: {
            VERIFY(!background_repeat);
            tokens.reconsume_current_input_token();
            if (auto maybe_repeat = parse_single_background_repeat_value(tokens)) {
                background_repeat = maybe_repeat.release_nonnull();
                continue;
            }
            return nullptr;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        return nullptr;
    }

    if (!background_layer_is_valid(true))
        return nullptr;

    // We only need to create StyleValueLists if there are multiple layers.
    // Otherwise, we can pass the single StyleValues directly.
    if (has_multiple_layers) {
        complete_background_layer();

        if (!background_color)
            background_color = initial_background_color;
        transaction.commit();
        return make_background_shorthand(
            background_color.release_nonnull(),
            StyleValueList::create(move(background_images), StyleValueList::Separator::Comma),
            ShorthandStyleValue::create(PropertyID::BackgroundPosition,
                { PropertyID::BackgroundPositionX, PropertyID::BackgroundPositionY },
                { StyleValueList::create(move(background_position_xs), StyleValueList::Separator::Comma),
                    StyleValueList::create(move(background_position_ys), StyleValueList::Separator::Comma) }),
            StyleValueList::create(move(background_sizes), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_repeats), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_attachments), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_origins), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_clips), StyleValueList::Separator::Comma));
    }

    if (!background_color)
        background_color = initial_background_color;
    if (!background_image)
        background_image = initial_background_image;
    if (!background_position_x)
        background_position_x = initial_background_position_x;
    if (!background_position_y)
        background_position_y = initial_background_position_y;
    if (!background_size)
        background_size = initial_background_size;
    if (!background_repeat)
        background_repeat = initial_background_repeat;
    if (!background_attachment)
        background_attachment = initial_background_attachment;

    if (!background_origin && !background_clip) {
        background_origin = initial_background_origin;
        background_clip = initial_background_clip;
    } else if (!background_clip) {
        background_clip = background_origin;
    }

    transaction.commit();
    return make_background_shorthand(
        background_color.release_nonnull(),
        background_image.release_nonnull(),
        ShorthandStyleValue::create(PropertyID::BackgroundPosition,
            { PropertyID::BackgroundPositionX, PropertyID::BackgroundPositionY },
            { background_position_x.release_nonnull(), background_position_y.release_nonnull() }),
        background_size.release_nonnull(),
        background_repeat.release_nonnull(),
        background_attachment.release_nonnull(),
        background_origin.release_nonnull(),
        background_clip.release_nonnull());
}

static Optional<LengthPercentage> style_value_to_length_percentage(auto value)
{
    if (value->is_percentage())
        return LengthPercentage { value->as_percentage().percentage() };
    if (value->is_length())
        return LengthPercentage { value->as_length().length() };
    if (value->is_calculated())
        return LengthPercentage { value->as_calculated() };
    return {};
}

RefPtr<CSSStyleValue const> Parser::parse_single_background_position_x_or_y_value(TokenStream<ComponentValue>& tokens, PropertyID property)
{
    Optional<PositionEdge> relative_edge {};

    auto transaction = tokens.begin_transaction();
    if (!tokens.has_next_token())
        return nullptr;

    auto value = parse_css_value_for_property(property, tokens);
    if (!value)
        return nullptr;

    if (value->is_keyword()) {
        auto keyword = value->to_keyword();
        if (keyword == Keyword::Center) {
            transaction.commit();
            return EdgeStyleValue::create(PositionEdge::Center, {});
        }
        if (auto edge = keyword_to_position_edge(keyword); edge.has_value()) {
            relative_edge = edge;
        } else {
            return nullptr;
        }
        if (tokens.has_next_token()) {
            value = parse_css_value_for_property(property, tokens);
            if (!value) {
                transaction.commit();
                return EdgeStyleValue::create(relative_edge, {});
            }
            if (value->is_keyword())
                return {};
        }
    }

    auto offset = style_value_to_length_percentage(value);
    if (offset.has_value()) {
        transaction.commit();
        return EdgeStyleValue::create(relative_edge, *offset);
    }

    if (!relative_edge.has_value()) {
        if (property == PropertyID::BackgroundPositionX) {
            // [ center | [ [ left | right | x-start | x-end ]? <length-percentage>? ]! ]#
            relative_edge = PositionEdge::Left;
        } else if (property == PropertyID::BackgroundPositionY) {
            // [ center | [ [ top | bottom | y-start | y-end ]? <length-percentage>? ]! ]#
            relative_edge = PositionEdge::Top;
        } else {
            VERIFY_NOT_REACHED();
        }
    }

    // If no offset is provided create this element but with an offset of default value of zero
    transaction.commit();
    return EdgeStyleValue::create(relative_edge, {});
}

RefPtr<CSSStyleValue const> Parser::parse_single_background_repeat_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto is_directional_repeat = [](CSSStyleValue const& value) -> bool {
        auto keyword = value.to_keyword();
        return keyword == Keyword::RepeatX || keyword == Keyword::RepeatY;
    };

    auto as_repeat = [](Keyword keyword) -> Optional<Repeat> {
        switch (keyword) {
        case Keyword::NoRepeat:
            return Repeat::NoRepeat;
        case Keyword::Repeat:
            return Repeat::Repeat;
        case Keyword::Round:
            return Repeat::Round;
        case Keyword::Space:
            return Repeat::Space;
        default:
            return {};
        }
    };

    auto maybe_x_value = parse_css_value_for_property(PropertyID::BackgroundRepeat, tokens);
    if (!maybe_x_value)
        return nullptr;
    auto x_value = maybe_x_value.release_nonnull();

    if (is_directional_repeat(*x_value)) {
        auto keyword = x_value->to_keyword();
        transaction.commit();
        return BackgroundRepeatStyleValue::create(
            keyword == Keyword::RepeatX ? Repeat::Repeat : Repeat::NoRepeat,
            keyword == Keyword::RepeatX ? Repeat::NoRepeat : Repeat::Repeat);
    }

    auto x_repeat = as_repeat(x_value->to_keyword());
    if (!x_repeat.has_value())
        return nullptr;

    // See if we have a second value for Y
    auto maybe_y_value = parse_css_value_for_property(PropertyID::BackgroundRepeat, tokens);
    if (!maybe_y_value) {
        // We don't have a second value, so use x for both
        transaction.commit();
        return BackgroundRepeatStyleValue::create(x_repeat.value(), x_repeat.value());
    }
    auto y_value = maybe_y_value.release_nonnull();
    if (is_directional_repeat(*y_value))
        return nullptr;

    auto y_repeat = as_repeat(y_value->to_keyword());
    if (!y_repeat.has_value())
        return nullptr;

    transaction.commit();
    return BackgroundRepeatStyleValue::create(x_repeat.value(), y_repeat.value());
}

RefPtr<CSSStyleValue const> Parser::parse_single_background_size_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto get_length_percentage = [](CSSStyleValue const& style_value) -> Optional<LengthPercentage> {
        if (style_value.has_auto())
            return LengthPercentage { Length::make_auto() };
        if (style_value.is_percentage())
            return LengthPercentage { style_value.as_percentage().percentage() };
        if (style_value.is_length())
            return LengthPercentage { style_value.as_length().length() };
        if (style_value.is_calculated())
            return LengthPercentage { style_value.as_calculated() };
        return {};
    };

    auto maybe_x_value = parse_css_value_for_property(PropertyID::BackgroundSize, tokens);
    if (!maybe_x_value)
        return nullptr;
    auto x_value = maybe_x_value.release_nonnull();

    if (x_value->to_keyword() == Keyword::Cover || x_value->to_keyword() == Keyword::Contain) {
        transaction.commit();
        return x_value;
    }

    auto maybe_y_value = parse_css_value_for_property(PropertyID::BackgroundSize, tokens);
    if (!maybe_y_value) {
        auto y_value = LengthPercentage { Length::make_auto() };
        auto x_size = get_length_percentage(*x_value);
        if (!x_size.has_value())
            return nullptr;

        transaction.commit();
        return BackgroundSizeStyleValue::create(x_size.value(), y_value);
    }

    auto y_value = maybe_y_value.release_nonnull();
    auto x_size = get_length_percentage(*x_value);
    auto y_size = get_length_percentage(*y_value);

    if (!x_size.has_value() || !y_size.has_value())
        return nullptr;

    transaction.commit();
    return BackgroundSizeStyleValue::create(x_size.release_value(), y_size.release_value());
}

RefPtr<CSSStyleValue const> Parser::parse_border_value(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    RefPtr<CSSStyleValue const> border_width;
    RefPtr<CSSStyleValue const> border_color;
    RefPtr<CSSStyleValue const> border_style;

    auto color_property = PropertyID::Invalid;
    auto style_property = PropertyID::Invalid;
    auto width_property = PropertyID::Invalid;

    switch (property_id) {
    case PropertyID::Border:
        color_property = PropertyID::BorderColor;
        style_property = PropertyID::BorderStyle;
        width_property = PropertyID::BorderWidth;
        break;
    case PropertyID::BorderBlock:
        color_property = PropertyID::BorderBlockColor;
        style_property = PropertyID::BorderBlockStyle;
        width_property = PropertyID::BorderBlockWidth;
        break;
    case PropertyID::BorderInline:
        color_property = PropertyID::BorderInlineColor;
        style_property = PropertyID::BorderInlineStyle;
        width_property = PropertyID::BorderInlineWidth;
        break;

    default:
        VERIFY_NOT_REACHED();
    }

    auto remaining_longhands = Vector { width_property, color_property, style_property };
    auto transaction = tokens.begin_transaction();

    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        if (property_and_value->property == width_property) {
            VERIFY(!border_width);
            border_width = value.release_nonnull();
        } else if (property_and_value->property == color_property) {
            VERIFY(!border_color);
            border_color = value.release_nonnull();
        } else if (property_and_value->property == style_property) {
            VERIFY(!border_style);
            border_style = value.release_nonnull();
        } else {
            VERIFY_NOT_REACHED();
        }
    }

    if (!border_width)
        border_width = property_initial_value(width_property);
    if (!border_style)
        border_style = property_initial_value(style_property);
    if (!border_color)
        border_color = property_initial_value(color_property);

    // FIXME: Also reset border-image, in line with the spec: https://www.w3.org/TR/css-backgrounds-3/#border-shorthands
    transaction.commit();
    return ShorthandStyleValue::create(property_id,
        { width_property, style_property, color_property },
        { border_width.release_nonnull(), border_style.release_nonnull(), border_color.release_nonnull() });
}

// https://drafts.csswg.org/css-backgrounds/#border-image
RefPtr<CSSStyleValue const> Parser::parse_border_image_value(TokenStream<ComponentValue>& tokens)
{
    // <'border-image-source'> || <'border-image-slice'> [ / <'border-image-width'> | / <'border-image-width'>? / <'border-image-outset'> ]? || <'border-image-repeat'>
    auto transaction = tokens.begin_transaction();

    RefPtr<CSSStyleValue const> source_value;
    RefPtr<CSSStyleValue const> slice_value;
    RefPtr<CSSStyleValue const> width_value;
    RefPtr<CSSStyleValue const> outset_value;
    RefPtr<CSSStyleValue const> repeat_value;

    auto make_border_image_shorthand = [&]() {
        transaction.commit();
        if (!source_value)
            source_value = property_initial_value(PropertyID::BorderImageSource);
        if (!slice_value)
            slice_value = property_initial_value(PropertyID::BorderImageSlice);
        if (!width_value)
            width_value = property_initial_value(PropertyID::BorderImageWidth);
        if (!outset_value)
            outset_value = property_initial_value(PropertyID::BorderImageOutset);
        if (!repeat_value)
            repeat_value = property_initial_value(PropertyID::BorderImageRepeat);

        return ShorthandStyleValue::create(PropertyID::BorderImage,
            { PropertyID::BorderImageSource, PropertyID::BorderImageSlice, PropertyID::BorderImageWidth, PropertyID::BorderImageOutset, PropertyID::BorderImageRepeat },
            { source_value.release_nonnull(), slice_value.release_nonnull(), width_value.release_nonnull(), outset_value.release_nonnull(), repeat_value.release_nonnull() });
    };

    auto parse_value_list = [&](PropertyID property_id, TokenStream<ComponentValue>& inner_tokens, Optional<char> delimiter = {}) -> RefPtr<CSSStyleValue const> {
        auto inner_transaction = inner_tokens.begin_transaction();

        auto remaining_values = property_maximum_value_count(property_id);
        StyleValueVector values;
        while (inner_tokens.has_next_token() && remaining_values > 0) {
            if (delimiter.has_value() && inner_tokens.next_token().is_delim(*delimiter))
                break;
            auto value = parse_css_value_for_property(property_id, inner_tokens);
            if (!value)
                break;
            values.append(*value);
            --remaining_values;
        }
        if (values.is_empty())
            return nullptr;

        inner_transaction.commit();
        if (values.size() == 1)
            return values[0];

        return StyleValueList::create(move(values), StyleValueList::Separator::Space);
    };

    auto parse_slice_portion = [&]() {
        // <'border-image-slice'> [ / <'border-image-width'> | / <'border-image-width'>? / <'border-image-outset'> ]?
        auto transaction = tokens.begin_transaction();
        tokens.discard_whitespace();
        if (auto value = parse_border_image_slice_value(tokens)) {
            slice_value = move(value);
        } else {
            return false;
        }
        tokens.discard_whitespace();
        if (tokens.next_token().is_delim('/')) {
            tokens.discard_a_token();
            tokens.discard_whitespace();
            if (auto width = parse_value_list(PropertyID::BorderImageWidth, tokens)) {
                width_value = move(width);
                tokens.discard_whitespace();
                if (!tokens.next_token().is_delim('/')) {
                    transaction.commit();
                    return true;
                }
            }

            if (!tokens.next_token().is_delim('/'))
                return false;

            tokens.discard_a_token();
            tokens.discard_whitespace();
            if (auto outset = parse_value_list(PropertyID::BorderImageOutset, tokens)) {
                outset_value = move(outset);
                tokens.discard_whitespace();
            } else {
                return false;
            }
        }

        tokens.discard_whitespace();
        transaction.commit();
        return true;
    };

    auto has_source = false;
    auto has_repeat = false;
    auto has_slice_portion = false;
    while (tokens.has_next_token()) {
        if (auto source = parse_css_value_for_property(PropertyID::BorderImageSource, tokens)) {
            if (has_source)
                return nullptr;

            has_source = true;
            source_value = move(source);
            continue;
        }
        if (auto repeat = parse_value_list(PropertyID::BorderImageRepeat, tokens)) {
            if (has_repeat)
                return nullptr;

            has_repeat = true;
            repeat_value = move(repeat);
            continue;
        }
        if (parse_slice_portion()) {
            if (has_slice_portion)
                return nullptr;

            has_slice_portion = true;
            continue;
        }
        return nullptr;
    }

    return make_border_image_shorthand();
}

// https://drafts.csswg.org/css-backgrounds/#border-image-slice
RefPtr<CSSStyleValue const> Parser::parse_border_image_slice_value(TokenStream<ComponentValue>& tokens)
{
    // [<number [0,∞]> | <percentage [0,∞]>]{1,4} && fill?
    auto transaction = tokens.begin_transaction();
    auto fill = false;
    RefPtr<CSSStyleValue const> top;
    RefPtr<CSSStyleValue const> right;
    RefPtr<CSSStyleValue const> bottom;
    RefPtr<CSSStyleValue const> left;

    auto parse_fill = [](TokenStream<ComponentValue>& fill_tokens) {
        if (fill_tokens.next_token().is_ident("fill"sv)) {
            fill_tokens.discard_a_token();
            return true;
        }
        return false;
    };

    if (parse_fill(tokens))
        fill = true;

    Vector<ValueComparingNonnullRefPtr<CSSStyleValue const>> number_percentages;
    while (number_percentages.size() <= 4 && tokens.has_next_token()) {
        auto number_percentage = parse_number_percentage_value(tokens);
        if (!number_percentage)
            break;

        if (number_percentage->is_number() && !property_accepts_number(PropertyID::BorderImageSlice, number_percentage->as_number().number()))
            return nullptr;
        if (number_percentage->is_percentage() && !property_accepts_percentage(PropertyID::BorderImageSlice, number_percentage->as_percentage().percentage()))
            return nullptr;
        number_percentages.append(number_percentage.release_nonnull());
    }

    switch (number_percentages.size()) {
    case 1:
        top = number_percentages[0];
        right = number_percentages[0];
        bottom = number_percentages[0];
        left = number_percentages[0];
        break;
    case 2:
        top = number_percentages[0];
        bottom = number_percentages[0];
        right = number_percentages[1];
        left = number_percentages[1];
        break;
    case 3:
        top = number_percentages[0];
        right = number_percentages[1];
        left = number_percentages[1];
        bottom = number_percentages[2];
        break;
    case 4:
        top = number_percentages[0];
        right = number_percentages[1];
        bottom = number_percentages[2];
        left = number_percentages[3];
        break;
    default:
        return nullptr;
    }

    if (tokens.has_next_token() && parse_fill(tokens)) {
        if (fill)
            return nullptr;

        fill = true;
    }

    transaction.commit();
    return BorderImageSliceStyleValue::create(
        top.release_nonnull(),
        right.release_nonnull(),
        bottom.release_nonnull(),
        left.release_nonnull(),
        fill);
}

RefPtr<CSSStyleValue const> Parser::parse_border_radius_value(TokenStream<ComponentValue>& tokens)
{
    if (tokens.remaining_token_count() == 2) {
        auto transaction = tokens.begin_transaction();
        auto horizontal = parse_length_percentage(tokens);
        auto vertical = parse_length_percentage(tokens);
        if (horizontal.has_value() && vertical.has_value()) {
            transaction.commit();
            return BorderRadiusStyleValue::create(horizontal.release_value(), vertical.release_value());
        }
    }

    if (tokens.remaining_token_count() == 1) {
        auto transaction = tokens.begin_transaction();
        auto radius = parse_length_percentage(tokens);
        if (radius.has_value()) {
            transaction.commit();
            return BorderRadiusStyleValue::create(radius.value(), radius.value());
        }
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_border_radius_shorthand_value(TokenStream<ComponentValue>& tokens)
{
    auto top_left = [&](Vector<LengthPercentage>& radii) { return radii[0]; };
    auto top_right = [&](Vector<LengthPercentage>& radii) {
        switch (radii.size()) {
        case 4:
        case 3:
        case 2:
            return radii[1];
        case 1:
            return radii[0];
        default:
            VERIFY_NOT_REACHED();
        }
    };
    auto bottom_right = [&](Vector<LengthPercentage>& radii) {
        switch (radii.size()) {
        case 4:
        case 3:
            return radii[2];
        case 2:
        case 1:
            return radii[0];
        default:
            VERIFY_NOT_REACHED();
        }
    };
    auto bottom_left = [&](Vector<LengthPercentage>& radii) {
        switch (radii.size()) {
        case 4:
            return radii[3];
        case 3:
        case 2:
            return radii[1];
        case 1:
            return radii[0];
        default:
            VERIFY_NOT_REACHED();
        }
    };

    Vector<LengthPercentage> horizontal_radii;
    Vector<LengthPercentage> vertical_radii;
    bool reading_vertical = false;
    auto transaction = tokens.begin_transaction();

    while (tokens.has_next_token()) {
        if (tokens.next_token().is_delim('/')) {
            if (reading_vertical || horizontal_radii.is_empty())
                return nullptr;

            reading_vertical = true;
            tokens.discard_a_token(); // `/`
            continue;
        }

        auto maybe_dimension = parse_length_percentage(tokens);
        if (!maybe_dimension.has_value())
            return nullptr;
        if (maybe_dimension->is_length() && !property_accepts_length(PropertyID::BorderRadius, maybe_dimension->length()))
            return nullptr;
        if (maybe_dimension->is_percentage() && !property_accepts_percentage(PropertyID::BorderRadius, maybe_dimension->percentage()))
            return nullptr;
        if (reading_vertical) {
            vertical_radii.append(maybe_dimension.release_value());
        } else {
            horizontal_radii.append(maybe_dimension.release_value());
        }
    }

    if (horizontal_radii.size() > 4 || vertical_radii.size() > 4
        || horizontal_radii.is_empty()
        || (reading_vertical && vertical_radii.is_empty()))
        return nullptr;

    auto top_left_radius = BorderRadiusStyleValue::create(top_left(horizontal_radii),
        vertical_radii.is_empty() ? top_left(horizontal_radii) : top_left(vertical_radii));
    auto top_right_radius = BorderRadiusStyleValue::create(top_right(horizontal_radii),
        vertical_radii.is_empty() ? top_right(horizontal_radii) : top_right(vertical_radii));
    auto bottom_right_radius = BorderRadiusStyleValue::create(bottom_right(horizontal_radii),
        vertical_radii.is_empty() ? bottom_right(horizontal_radii) : bottom_right(vertical_radii));
    auto bottom_left_radius = BorderRadiusStyleValue::create(bottom_left(horizontal_radii),
        vertical_radii.is_empty() ? bottom_left(horizontal_radii) : bottom_left(vertical_radii));

    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::BorderRadius,
        { PropertyID::BorderTopLeftRadius, PropertyID::BorderTopRightRadius, PropertyID::BorderBottomRightRadius, PropertyID::BorderBottomLeftRadius },
        { move(top_left_radius), move(top_right_radius), move(bottom_right_radius), move(bottom_left_radius) });
}

RefPtr<CSSStyleValue const> Parser::parse_columns_value(TokenStream<ComponentValue>& tokens)
{
    if (tokens.remaining_token_count() > 2)
        return nullptr;

    RefPtr<CSSStyleValue const> column_count;
    RefPtr<CSSStyleValue const> column_width;

    Vector<PropertyID> remaining_longhands { PropertyID::ColumnCount, PropertyID::ColumnWidth };
    int found_autos = 0;

    auto transaction = tokens.begin_transaction();
    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;

        // since the values can be in either order, we want to skip over autos
        if (value->has_auto()) {
            found_autos++;
            continue;
        }

        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::ColumnCount: {
            VERIFY(!column_count);
            column_count = value.release_nonnull();
            continue;
        }
        case PropertyID::ColumnWidth: {
            VERIFY(!column_width);
            column_width = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (found_autos > 2)
        return nullptr;

    if (found_autos == 2) {
        column_count = CSSKeywordValue::create(Keyword::Auto);
        column_width = CSSKeywordValue::create(Keyword::Auto);
    }

    if (found_autos == 1) {
        if (!column_count)
            column_count = CSSKeywordValue::create(Keyword::Auto);
        if (!column_width)
            column_width = CSSKeywordValue::create(Keyword::Auto);
    }

    if (!column_count)
        column_count = property_initial_value(PropertyID::ColumnCount);
    if (!column_width)
        column_width = property_initial_value(PropertyID::ColumnWidth);

    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::Columns,
        { PropertyID::ColumnCount, PropertyID::ColumnWidth },
        { column_count.release_nonnull(), column_width.release_nonnull() });
}

RefPtr<CSSStyleValue const> Parser::parse_shadow_value(TokenStream<ComponentValue>& tokens, AllowInsetKeyword allow_inset_keyword)
{
    // "none"
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    return parse_comma_separated_value_list(tokens, [this, allow_inset_keyword](auto& tokens) {
        return parse_single_shadow_value(tokens, allow_inset_keyword);
    });
}

RefPtr<CSSStyleValue const> Parser::parse_single_shadow_value(TokenStream<ComponentValue>& tokens, AllowInsetKeyword allow_inset_keyword)
{
    auto transaction = tokens.begin_transaction();

    RefPtr<CSSStyleValue const> color;
    RefPtr<CSSStyleValue const> offset_x;
    RefPtr<CSSStyleValue const> offset_y;
    RefPtr<CSSStyleValue const> blur_radius;
    RefPtr<CSSStyleValue const> spread_distance;
    Optional<ShadowPlacement> placement;

    auto possibly_dynamic_length = [&](ComponentValue const& token) -> RefPtr<CSSStyleValue const> {
        auto tokens = TokenStream<ComponentValue>::of_single_token(token);
        auto maybe_length = parse_length(tokens);
        if (!maybe_length.has_value())
            return nullptr;
        return maybe_length->as_style_value();
    };

    while (tokens.has_next_token()) {
        if (auto maybe_color = parse_color_value(tokens); maybe_color) {
            if (color)
                return nullptr;
            color = maybe_color.release_nonnull();
            continue;
        }

        auto const& token = tokens.next_token();
        if (auto maybe_offset_x = possibly_dynamic_length(token); maybe_offset_x) {
            // horizontal offset
            if (offset_x)
                return nullptr;
            offset_x = maybe_offset_x;
            tokens.discard_a_token();

            // vertical offset
            if (!tokens.has_next_token())
                return nullptr;
            auto maybe_offset_y = possibly_dynamic_length(tokens.next_token());
            if (!maybe_offset_y)
                return nullptr;
            offset_y = maybe_offset_y;
            tokens.discard_a_token();

            // blur radius (optional)
            if (!tokens.has_next_token())
                break;
            auto maybe_blur_radius = possibly_dynamic_length(tokens.next_token());
            if (!maybe_blur_radius)
                continue;
            blur_radius = maybe_blur_radius;
            if (blur_radius->is_length() && blur_radius->as_length().length().raw_value() < 0)
                return nullptr;
            if (blur_radius->is_percentage() && blur_radius->as_percentage().value() < 0)
                return nullptr;
            tokens.discard_a_token();

            // spread distance (optional)
            if (!tokens.has_next_token())
                break;
            auto maybe_spread_distance = possibly_dynamic_length(tokens.next_token());
            if (!maybe_spread_distance)
                continue;
            spread_distance = maybe_spread_distance;
            tokens.discard_a_token();

            continue;
        }

        if (allow_inset_keyword == AllowInsetKeyword::Yes && token.is_ident("inset"sv)) {
            if (placement.has_value())
                return nullptr;
            placement = ShadowPlacement::Inner;
            tokens.discard_a_token();
            continue;
        }

        if (token.is(Token::Type::Comma))
            break;

        return nullptr;
    }

    // x/y offsets are required
    if (!offset_x || !offset_y)
        return nullptr;

    // Placement is outer by default
    if (!placement.has_value())
        placement = ShadowPlacement::Outer;

    transaction.commit();
    return ShadowStyleValue::create(color, offset_x.release_nonnull(), offset_y.release_nonnull(), blur_radius, spread_distance, placement.release_value());
}

RefPtr<CSSStyleValue const> Parser::parse_rotate_value(TokenStream<ComponentValue>& tokens)
{
    // Value:	none | <angle> | [ x | y | z | <number>{3} ] && <angle>

    if (tokens.remaining_token_count() == 1) {
        // "none"
        if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
            return none;

        // <angle>
        if (auto angle = parse_angle_value(tokens))
            return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::Rotate, { angle.release_nonnull() });
    }

    auto parse_one_of_xyz = [&]() -> Optional<ComponentValue const&> {
        auto transaction = tokens.begin_transaction();
        auto const& axis = tokens.consume_a_token();

        if (axis.is_ident("x"sv) || axis.is_ident("y"sv) || axis.is_ident("z"sv)) {
            transaction.commit();
            return axis;
        }

        return {};
    };

    // [ x | y | z ] && <angle>
    if (tokens.remaining_token_count() == 2) {
        // Try parsing `x <angle>`
        if (auto axis = parse_one_of_xyz(); axis.has_value()) {
            if (auto angle = parse_angle_value(tokens); angle) {
                if (axis->is_ident("x"sv))
                    return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::RotateX, { angle.release_nonnull() });
                if (axis->is_ident("y"sv))
                    return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::RotateY, { angle.release_nonnull() });
                if (axis->is_ident("z"sv))
                    return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::RotateZ, { angle.release_nonnull() });
            }
        }

        // Try parsing `<angle> x`
        if (auto angle = parse_angle_value(tokens); angle) {
            if (auto axis = parse_one_of_xyz(); axis.has_value()) {
                if (axis->is_ident("x"sv))
                    return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::RotateX, { angle.release_nonnull() });
                if (axis->is_ident("y"sv))
                    return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::RotateY, { angle.release_nonnull() });
                if (axis->is_ident("z"sv))
                    return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::RotateZ, { angle.release_nonnull() });
            }
        }
    }

    auto parse_three_numbers = [&]() -> Optional<StyleValueVector> {
        auto transaction = tokens.begin_transaction();
        StyleValueVector numbers;
        for (size_t i = 0; i < 3; ++i) {
            if (auto number = parse_number_value(tokens); number) {
                numbers.append(number.release_nonnull());
            } else {
                return {};
            }
        }
        transaction.commit();
        return numbers;
    };

    // <number>{3} && <angle>
    if (tokens.remaining_token_count() == 4) {
        // Try parsing <number>{3} <angle>
        if (auto maybe_numbers = parse_three_numbers(); maybe_numbers.has_value()) {
            if (auto angle = parse_angle_value(tokens); angle) {
                auto numbers = maybe_numbers.release_value();
                return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::Rotate3d, { numbers[0], numbers[1], numbers[2], angle.release_nonnull() });
            }
        }

        // Try parsing <angle> <number>{3}
        if (auto angle = parse_angle_value(tokens); angle) {
            if (auto maybe_numbers = parse_three_numbers(); maybe_numbers.has_value()) {
                auto numbers = maybe_numbers.release_value();
                return TransformationStyleValue::create(PropertyID::Rotate, TransformFunction::Rotate3d, { numbers[0], numbers[1], numbers[2], angle.release_nonnull() });
            }
        }
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_stroke_dasharray_value(TokenStream<ComponentValue>& tokens)
{
    // https://svgwg.org/svg2-draft/painting.html#StrokeDashing
    // Value: none | <dasharray>
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    // https://svgwg.org/svg2-draft/painting.html#DataTypeDasharray
    // <dasharray> = [ [ <length-percentage> | <number> ]+ ]#
    Vector<ValueComparingNonnullRefPtr<CSSStyleValue const>> dashes;
    while (tokens.has_next_token()) {
        tokens.discard_whitespace();

        // A <dasharray> is a list of comma and/or white space separated <number> or <length-percentage> values. A <number> value represents a value in user units.
        auto value = parse_number_value(tokens);
        if (value && value->is_number() && value->as_number().value() < 0)
            return {};

        if (value) {
            dashes.append(value.release_nonnull());
        } else {
            auto value = parse_length_percentage_value(tokens);
            if (!value)
                return {};
            if (value->is_percentage() && value->as_percentage().value() < 0)
                return {};
            if (value->is_length() && value->as_length().length().raw_value() < 0)
                return {};
            dashes.append(value.release_nonnull());
        }

        tokens.discard_whitespace();
        if (tokens.has_next_token() && tokens.next_token().is(Token::Type::Comma))
            tokens.discard_a_token();
    }

    return StyleValueList::create(move(dashes), StyleValueList::Separator::Comma);
}

RefPtr<CSSStyleValue const> Parser::parse_content_value(TokenStream<ComponentValue>& tokens)
{
    // FIXME: `content` accepts several kinds of function() type, which we don't handle in property_accepts_value() yet.

    auto is_single_value_keyword = [](Keyword keyword) -> bool {
        switch (keyword) {
        case Keyword::None:
        case Keyword::Normal:
            return true;
        default:
            return false;
        }
    };

    if (tokens.remaining_token_count() == 1) {
        auto transaction = tokens.begin_transaction();
        if (auto keyword = parse_keyword_value(tokens)) {
            if (is_single_value_keyword(keyword->to_keyword())) {
                transaction.commit();
                return keyword;
            }
        }
    }

    auto transaction = tokens.begin_transaction();

    StyleValueVector content_values;
    StyleValueVector alt_text_values;
    bool in_alt_text = false;

    while (tokens.has_next_token()) {
        auto& next = tokens.next_token();
        if (next.is_delim('/')) {
            if (in_alt_text || content_values.is_empty())
                return nullptr;
            in_alt_text = true;
            tokens.discard_a_token();
            continue;
        }

        if (auto style_value = parse_css_value_for_property(PropertyID::Content, tokens)) {
            if (is_single_value_keyword(style_value->to_keyword()))
                return nullptr;

            if (in_alt_text) {
                alt_text_values.append(style_value.release_nonnull());
            } else {
                content_values.append(style_value.release_nonnull());
            }
            continue;
        }

        return nullptr;
    }

    if (content_values.is_empty())
        return nullptr;
    if (in_alt_text && alt_text_values.is_empty())
        return nullptr;

    RefPtr<StyleValueList> alt_text;
    if (!alt_text_values.is_empty())
        alt_text = StyleValueList::create(move(alt_text_values), StyleValueList::Separator::Space);

    transaction.commit();
    return ContentStyleValue::create(StyleValueList::create(move(content_values), StyleValueList::Separator::Space), move(alt_text));
}

// https://www.w3.org/TR/css-display-3/#the-display-properties
RefPtr<CSSStyleValue const> Parser::parse_display_value(TokenStream<ComponentValue>& tokens)
{
    auto parse_single_component_display = [this](TokenStream<ComponentValue>& tokens) -> Optional<Display> {
        auto transaction = tokens.begin_transaction();
        if (auto keyword_value = parse_keyword_value(tokens)) {
            auto keyword = keyword_value->to_keyword();
            if (keyword == Keyword::ListItem) {
                transaction.commit();
                return Display::from_short(Display::Short::ListItem);
            }

            if (auto display_outside = keyword_to_display_outside(keyword); display_outside.has_value()) {
                transaction.commit();
                switch (display_outside.value()) {
                case DisplayOutside::Block:
                    return Display::from_short(Display::Short::Block);
                case DisplayOutside::Inline:
                    return Display::from_short(Display::Short::Inline);
                case DisplayOutside::RunIn:
                    return Display::from_short(Display::Short::RunIn);
                }
            }

            if (auto display_inside = keyword_to_display_inside(keyword); display_inside.has_value()) {
                transaction.commit();
                switch (display_inside.value()) {
                case DisplayInside::Flow:
                    return Display::from_short(Display::Short::Flow);
                case DisplayInside::FlowRoot:
                    return Display::from_short(Display::Short::FlowRoot);
                case DisplayInside::Table:
                    return Display::from_short(Display::Short::Table);
                case DisplayInside::Flex:
                    return Display::from_short(Display::Short::Flex);
                case DisplayInside::Grid:
                    return Display::from_short(Display::Short::Grid);
                case DisplayInside::Ruby:
                    return Display::from_short(Display::Short::Ruby);
                case DisplayInside::Math:
                    return Display::from_short(Display::Short::Math);
                }
            }

            if (auto display_internal = keyword_to_display_internal(keyword); display_internal.has_value()) {
                transaction.commit();
                return Display { display_internal.value() };
            }

            if (auto display_box = keyword_to_display_box(keyword); display_box.has_value()) {
                transaction.commit();
                switch (display_box.value()) {
                case DisplayBox::Contents:
                    return Display::from_short(Display::Short::Contents);
                case DisplayBox::None:
                    return Display::from_short(Display::Short::None);
                }
            }

            if (auto display_legacy = keyword_to_display_legacy(keyword); display_legacy.has_value()) {
                transaction.commit();
                switch (display_legacy.value()) {
                case DisplayLegacy::InlineBlock:
                    return Display::from_short(Display::Short::InlineBlock);
                case DisplayLegacy::InlineTable:
                    return Display::from_short(Display::Short::InlineTable);
                case DisplayLegacy::InlineFlex:
                    return Display::from_short(Display::Short::InlineFlex);
                case DisplayLegacy::InlineGrid:
                    return Display::from_short(Display::Short::InlineGrid);
                }
            }
        }
        return OptionalNone {};
    };

    auto parse_multi_component_display = [this](TokenStream<ComponentValue>& tokens) -> Optional<Display> {
        auto list_item = Display::ListItem::No;
        Optional<DisplayInside> inside;
        Optional<DisplayOutside> outside;

        auto transaction = tokens.begin_transaction();
        while (tokens.has_next_token()) {
            if (auto value = parse_keyword_value(tokens)) {
                auto keyword = value->to_keyword();
                if (keyword == Keyword::ListItem) {
                    if (list_item == Display::ListItem::Yes)
                        return {};
                    list_item = Display::ListItem::Yes;
                    continue;
                }
                if (auto inside_value = keyword_to_display_inside(keyword); inside_value.has_value()) {
                    if (inside.has_value())
                        return {};
                    inside = inside_value.value();
                    continue;
                }
                if (auto outside_value = keyword_to_display_outside(keyword); outside_value.has_value()) {
                    if (outside.has_value())
                        return {};
                    outside = outside_value.value();
                    continue;
                }
            }

            // Not a display value, abort.
            dbgln_if(CSS_PARSER_DEBUG, "Unrecognized display value: `{}`", tokens.next_token().to_string());
            return {};
        }

        // The spec does not allow any other inside values to be combined with list-item
        // <display-outside>? && [ flow | flow-root ]? && list-item
        if (list_item == Display::ListItem::Yes && inside.has_value() && inside != DisplayInside::Flow && inside != DisplayInside::FlowRoot)
            return {};

        transaction.commit();
        return Display { outside.value_or(DisplayOutside::Block), inside.value_or(DisplayInside::Flow), list_item };
    };

    Optional<Display> display;
    if (tokens.remaining_token_count() == 1)
        display = parse_single_component_display(tokens);
    else
        display = parse_multi_component_display(tokens);

    if (display.has_value())
        return DisplayStyleValue::create(display.value());

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_flex_shorthand_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto make_flex_shorthand = [&](NonnullRefPtr<CSSStyleValue const> flex_grow, NonnullRefPtr<CSSStyleValue const> flex_shrink, NonnullRefPtr<CSSStyleValue const> flex_basis) {
        transaction.commit();
        return ShorthandStyleValue::create(PropertyID::Flex,
            { PropertyID::FlexGrow, PropertyID::FlexShrink, PropertyID::FlexBasis },
            { move(flex_grow), move(flex_shrink), move(flex_basis) });
    };

    if (tokens.remaining_token_count() == 1) {
        // One-value syntax: <flex-grow> | <flex-basis> | none
        auto properties = Array { PropertyID::FlexGrow, PropertyID::FlexBasis, PropertyID::Flex };
        auto property_and_value = parse_css_value_for_properties(properties, tokens);
        if (!property_and_value.has_value())
            return nullptr;

        auto& value = property_and_value->style_value;
        switch (property_and_value->property) {
        case PropertyID::FlexGrow: {
            // NOTE: The spec says that flex-basis should be 0 here, but other engines currently use 0%.
            // https://github.com/w3c/csswg-drafts/issues/5742
            auto flex_basis = PercentageStyleValue::create(Percentage(0));
            auto one = NumberStyleValue::create(1);
            return make_flex_shorthand(*value, one, flex_basis);
        }
        case PropertyID::FlexBasis: {
            auto one = NumberStyleValue::create(1);
            return make_flex_shorthand(one, one, *value);
        }
        case PropertyID::Flex: {
            if (value->is_keyword() && value->to_keyword() == Keyword::None) {
                auto zero = NumberStyleValue::create(0);
                return make_flex_shorthand(zero, zero, CSSKeywordValue::create(Keyword::Auto));
            }
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        return nullptr;
    }

    RefPtr<CSSStyleValue const> flex_grow;
    RefPtr<CSSStyleValue const> flex_shrink;
    RefPtr<CSSStyleValue const> flex_basis;

    // NOTE: FlexGrow has to be before FlexBasis. `0` is a valid FlexBasis, but only
    //       if FlexGrow (along with optional FlexShrink) have already been specified.
    auto remaining_longhands = Vector { PropertyID::FlexGrow, PropertyID::FlexBasis };

    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::FlexGrow: {
            VERIFY(!flex_grow);
            flex_grow = value.release_nonnull();

            // Flex-shrink may optionally follow directly after.
            auto maybe_flex_shrink = parse_css_value_for_property(PropertyID::FlexShrink, tokens);
            if (maybe_flex_shrink)
                flex_shrink = maybe_flex_shrink.release_nonnull();
            continue;
        }
        case PropertyID::FlexBasis: {
            VERIFY(!flex_basis);
            flex_basis = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!flex_grow)
        flex_grow = property_initial_value(PropertyID::FlexGrow);
    if (!flex_shrink)
        flex_shrink = property_initial_value(PropertyID::FlexShrink);
    if (!flex_basis) {
        // NOTE: The spec says that flex-basis should be 0 here, but other engines currently use 0%.
        // https://github.com/w3c/csswg-drafts/issues/5742
        flex_basis = PercentageStyleValue::create(Percentage(0));
    }

    return make_flex_shorthand(flex_grow.release_nonnull(), flex_shrink.release_nonnull(), flex_basis.release_nonnull());
}

RefPtr<CSSStyleValue const> Parser::parse_flex_flow_value(TokenStream<ComponentValue>& tokens)
{
    RefPtr<CSSStyleValue const> flex_direction;
    RefPtr<CSSStyleValue const> flex_wrap;

    auto remaining_longhands = Vector { PropertyID::FlexDirection, PropertyID::FlexWrap };
    auto transaction = tokens.begin_transaction();

    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::FlexDirection:
            VERIFY(!flex_direction);
            flex_direction = value.release_nonnull();
            continue;
        case PropertyID::FlexWrap:
            VERIFY(!flex_wrap);
            flex_wrap = value.release_nonnull();
            continue;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!flex_direction)
        flex_direction = property_initial_value(PropertyID::FlexDirection);
    if (!flex_wrap)
        flex_wrap = property_initial_value(PropertyID::FlexWrap);

    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::FlexFlow,
        { PropertyID::FlexDirection, PropertyID::FlexWrap },
        { flex_direction.release_nonnull(), flex_wrap.release_nonnull() });
}

// https://drafts.csswg.org/css-fonts-4/#font-prop
RefPtr<CSSStyleValue const> Parser::parse_font_value(TokenStream<ComponentValue>& tokens)
{
    // [ [ <'font-style'> || <font-variant-css2> || <'font-weight'> || <font-width-css3> ]? <'font-size'> [ / <'line-height'> ]? <'font-family'># ] | <system-family-name>
    RefPtr<CSSStyleValue const> font_style;
    RefPtr<CSSStyleValue const> font_variant;
    RefPtr<CSSStyleValue const> font_weight;
    RefPtr<CSSStyleValue const> font_width;
    RefPtr<CSSStyleValue const> font_size;
    RefPtr<CSSStyleValue const> line_height;
    RefPtr<CSSStyleValue const> font_families;

    // FIXME: Handle <system-family-name>. (caption, icon, menu, message-box, small-caption, status-bar)

    // Several sub-properties can be "normal", and appear in any order: style, variant, weight, stretch
    // So, we have to handle that separately.
    int normal_count = 0;

    // font-variant and font-width aren't included because we have special parsing rules for them in font.
    auto remaining_longhands = Vector { PropertyID::FontSize, PropertyID::FontStyle, PropertyID::FontWeight };
    auto transaction = tokens.begin_transaction();

    while (tokens.has_next_token()) {
        if (tokens.next_token().is_ident("normal"sv)) {
            normal_count++;
            tokens.discard_a_token();
            continue;
        }

        // <font-variant-css2> = normal | small-caps
        // So, we handle that manually instead of trying to parse the font-variant property.
        if (!font_variant && tokens.peek_token().is_ident("small-caps"sv)) {
            tokens.discard_a_token(); // small-caps

            font_variant = ShorthandStyleValue::create(PropertyID::FontVariant,
                { PropertyID::FontVariantAlternates,
                    PropertyID::FontVariantCaps,
                    PropertyID::FontVariantEastAsian,
                    PropertyID::FontVariantEmoji,
                    PropertyID::FontVariantLigatures,
                    PropertyID::FontVariantNumeric,
                    PropertyID::FontVariantPosition },
                {
                    property_initial_value(PropertyID::FontVariantAlternates),
                    CSSKeywordValue::create(Keyword::SmallCaps),
                    property_initial_value(PropertyID::FontVariantEastAsian),
                    property_initial_value(PropertyID::FontVariantEmoji),
                    property_initial_value(PropertyID::FontVariantLigatures),
                    property_initial_value(PropertyID::FontVariantNumeric),
                    property_initial_value(PropertyID::FontVariantPosition),
                });
            continue;
        }

        // <font-width-css3> = normal | ultra-condensed | extra-condensed | condensed | semi-condensed | semi-expanded | expanded | extra-expanded | ultra-expanded
        // So again, we do this manually.
        if (!font_width && tokens.peek_token().is(Token::Type::Ident)) {
            auto font_width_transaction = tokens.begin_transaction();
            if (auto keyword = parse_keyword_value(tokens)) {
                if (keyword_to_font_width(keyword->to_keyword()).has_value()) {
                    font_width_transaction.commit();
                    font_width = keyword.release_nonnull();
                    continue;
                }
            }
        }

        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::FontSize: {
            VERIFY(!font_size);
            font_size = value.release_nonnull();

            // Consume `/ line-height` if present
            if (tokens.next_token().is_delim('/')) {
                tokens.discard_a_token();
                auto maybe_line_height = parse_css_value_for_property(PropertyID::LineHeight, tokens);
                if (!maybe_line_height)
                    return nullptr;
                line_height = maybe_line_height.release_nonnull();
            }

            // Consume font-families
            auto maybe_font_families = parse_font_family_value(tokens);
            // font-family comes last, so we must not have any tokens left over.
            if (!maybe_font_families || tokens.has_next_token())
                return nullptr;
            font_families = maybe_font_families.release_nonnull();
            continue;
        }
        case PropertyID::FontStyle: {
            VERIFY(!font_style);
            font_style = FontStyleStyleValue::create(*keyword_to_font_style(value.release_nonnull()->to_keyword()));
            continue;
        }
        case PropertyID::FontWeight: {
            VERIFY(!font_weight);
            font_weight = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        return nullptr;
    }

    // Since normal is the default value for all the properties that can have it, we don't have to actually
    // set anything to normal here. It'll be set when we create the ShorthandStyleValue below.
    // We just need to make sure we were not given more normals than will fit.
    int unset_value_count = (font_style ? 0 : 1) + (font_weight ? 0 : 1) + (font_variant ? 0 : 1) + (font_width ? 0 : 1);
    if (unset_value_count < normal_count)
        return nullptr;

    if (!font_size || !font_families)
        return nullptr;

    if (!font_style)
        font_style = property_initial_value(PropertyID::FontStyle);
    if (!font_variant)
        font_variant = property_initial_value(PropertyID::FontVariant);
    if (!font_weight)
        font_weight = property_initial_value(PropertyID::FontWeight);
    if (!font_width)
        font_width = property_initial_value(PropertyID::FontWidth);
    if (!line_height)
        line_height = property_initial_value(PropertyID::LineHeight);

    transaction.commit();
    auto initial_value = CSSKeywordValue::create(Keyword::Initial);
    return ShorthandStyleValue::create(PropertyID::Font,
        {
            // Set explicitly https://drafts.csswg.org/css-fonts/#set-explicitly
            PropertyID::FontFamily,
            PropertyID::FontSize,
            PropertyID::FontWidth,
            PropertyID::FontStyle,
            PropertyID::FontVariant,
            PropertyID::FontWeight,
            PropertyID::LineHeight,

            // Reset implicitly https://drafts.csswg.org/css-fonts/#reset-implicitly
            PropertyID::FontFeatureSettings,
            PropertyID::FontKerning,
            PropertyID::FontLanguageOverride,
            // FIXME: PropertyID::FontOpticalSizing,
            // FIXME: PropertyID::FontSizeAdjust,
            PropertyID::FontVariationSettings,
        },
        {
            // Set explicitly
            font_families.release_nonnull(),
            font_size.release_nonnull(),
            font_width.release_nonnull(),
            font_style.release_nonnull(),
            font_variant.release_nonnull(),
            font_weight.release_nonnull(),
            line_height.release_nonnull(),

            // Reset implicitly
            initial_value,                                   // font-feature-settings
            property_initial_value(PropertyID::FontKerning), // font-kerning,
            initial_value,                                   // font-language-override
                                                             // FIXME: font-optical-sizing,
                                                             // FIXME: font-size-adjust,
            initial_value,                                   // font-variation-settings
        });
}

// https://drafts.csswg.org/css-fonts-4/#font-family-prop
RefPtr<CSSStyleValue const> Parser::parse_font_family_value(TokenStream<ComponentValue>& tokens)
{
    // [ <family-name> | <generic-family> ]#
    // FIXME: We currently require font-family to always be a list, even with one item.
    //        Maybe change that?
    auto result = parse_comma_separated_value_list(tokens, [this](auto& inner_tokens) -> RefPtr<CSSStyleValue const> {
        inner_tokens.discard_whitespace();

        // <generic-family>
        if (inner_tokens.next_token().is(Token::Type::Ident)) {
            auto maybe_keyword = keyword_from_string(inner_tokens.next_token().token().ident());
            if (maybe_keyword.has_value() && keyword_to_generic_font_family(maybe_keyword.value()).has_value()) {
                inner_tokens.discard_a_token(); // Ident
                inner_tokens.discard_whitespace();
                return CSSKeywordValue::create(maybe_keyword.value());
            }
        }

        // <family-name>
        return parse_family_name_value(inner_tokens);
    });

    if (!result)
        return nullptr;

    if (result->is_value_list())
        return result.release_nonnull();

    // It's a single value, so wrap it in a list - see FIXME above.
    return StyleValueList::create(StyleValueVector { result.release_nonnull() }, StyleValueList::Separator::Comma);
}

RefPtr<CSSStyleValue const> Parser::parse_font_language_override_value(TokenStream<ComponentValue>& tokens)
{
    // https://drafts.csswg.org/css-fonts/#propdef-font-language-override
    // This is `normal | <string>` but with the constraint that the string has to be 4 characters long:
    // Shorter strings are right-padded with spaces, and longer strings are invalid.

    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal;

    auto transaction = tokens.begin_transaction();
    tokens.discard_whitespace();
    if (auto string = parse_string_value(tokens)) {
        auto string_value = string->string_value();
        tokens.discard_whitespace();
        if (tokens.has_next_token()) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Failed to parse font-language-override: unexpected trailing tokens");
            return nullptr;
        }
        auto length = string_value.bytes().size();
        if (length == 0) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Failed to parse font-language-override: <string> value is empty");
            return nullptr;
        }
        if (!string_value.is_ascii()) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Failed to parse font-language-override: <string> value \"{}\" contains non-ascii characters", string_value);
            return nullptr;
        }
        if (length > 4) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Failed to parse font-language-override: <string> value \"{}\" is too long", string_value);
            return nullptr;
        }
        transaction.commit();
        if (length < 4)
            return StringStyleValue::create(MUST(String::formatted("{:<4}", string_value)));
        return string;
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_font_feature_settings_value(TokenStream<ComponentValue>& tokens)
{
    // https://drafts.csswg.org/css-fonts/#propdef-font-feature-settings
    // normal | <feature-tag-value>#

    // normal
    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal;

    // <feature-tag-value>#
    auto transaction = tokens.begin_transaction();
    auto tag_values = parse_a_comma_separated_list_of_component_values(tokens);

    // "The computed value of font-feature-settings is a map, so any duplicates in the specified value must not be preserved.
    // If the same feature tag appears more than once, the value associated with the last appearance supersedes any previous
    // value for that axis."
    // So, we deduplicate them here using a HashSet.

    OrderedHashMap<FlyString, NonnullRefPtr<OpenTypeTaggedStyleValue const>> feature_tags_map;
    for (auto const& values : tag_values) {
        // <feature-tag-value> = <opentype-tag> [ <integer [0,∞]> | on | off ]?
        TokenStream tag_tokens { values };
        tag_tokens.discard_whitespace();
        auto opentype_tag = parse_opentype_tag_value(tag_tokens);
        tag_tokens.discard_whitespace();
        RefPtr<CSSStyleValue const> value;
        if (tag_tokens.has_next_token()) {
            if (auto integer = parse_integer_value(tag_tokens)) {
                if (integer->is_integer() && integer->as_integer().value() < 0)
                    return nullptr;
                value = integer;
            } else {
                // A value of on is synonymous with 1 and off is synonymous with 0.
                auto keyword = parse_keyword_value(tag_tokens);
                if (!keyword)
                    return nullptr;
                switch (keyword->to_keyword()) {
                case Keyword::On:
                    value = IntegerStyleValue::create(1);
                    break;
                case Keyword::Off:
                    value = IntegerStyleValue::create(0);
                    break;
                default:
                    return nullptr;
                }
            }
            tag_tokens.discard_whitespace();
        } else {
            // "If the value is omitted, a value of 1 is assumed."
            value = IntegerStyleValue::create(1);
        }

        if (!opentype_tag || !value || tag_tokens.has_next_token())
            return nullptr;

        feature_tags_map.set(opentype_tag->string_value(), OpenTypeTaggedStyleValue::create(OpenTypeTaggedStyleValue::Mode::FontFeatureSettings, opentype_tag->string_value(), value.release_nonnull()));
    }

    // "The computed value contains the de-duplicated feature tags, sorted in ascending order by code unit."
    StyleValueVector feature_tags;
    feature_tags.ensure_capacity(feature_tags_map.size());
    for (auto const& [key, feature_tag] : feature_tags_map)
        feature_tags.append(feature_tag);

    quick_sort(feature_tags, [](auto& a, auto& b) {
        return a->as_open_type_tagged().tag() < b->as_open_type_tagged().tag();
    });

    transaction.commit();
    return StyleValueList::create(move(feature_tags), StyleValueList::Separator::Comma);
}

RefPtr<CSSStyleValue const> Parser::parse_font_style_value(TokenStream<ComponentValue>& tokens)
{
    // https://drafts.csswg.org/css-fonts/#font-style-prop
    // normal | italic | left | right | oblique <angle [-90deg,90deg]>?
    auto transaction = tokens.begin_transaction();
    auto keyword_value = parse_css_value_for_property(PropertyID::FontStyle, tokens);
    if (!keyword_value)
        return nullptr;
    auto font_style = keyword_to_font_style(keyword_value->to_keyword());
    VERIFY(font_style.has_value());
    if (tokens.has_next_token() && keyword_value->to_keyword() == Keyword::Oblique) {
        if (auto angle_value = parse_angle_value(tokens)) {
            if (angle_value->is_angle()) {
                auto angle = angle_value->as_angle().angle();
                auto angle_degrees = angle.to_degrees();
                if (angle_degrees < -90 || angle_degrees > 90)
                    return nullptr;
            }

            transaction.commit();
            return FontStyleStyleValue::create(*font_style, angle_value);
        }
    }

    transaction.commit();
    return FontStyleStyleValue::create(*font_style);
}

RefPtr<CSSStyleValue const> Parser::parse_font_variation_settings_value(TokenStream<ComponentValue>& tokens)
{
    // https://drafts.csswg.org/css-fonts/#propdef-font-variation-settings
    // normal | [ <opentype-tag> <number> ]#

    // normal
    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal;

    // [ <opentype-tag> <number>]#
    auto transaction = tokens.begin_transaction();
    auto tag_values = parse_a_comma_separated_list_of_component_values(tokens);

    // "If the same axis name appears more than once, the value associated with the last appearance supersedes any
    // previous value for that axis. This deduplication is observable by accessing the computed value of this property."
    // So, we deduplicate them here using a HashSet.

    OrderedHashMap<FlyString, NonnullRefPtr<OpenTypeTaggedStyleValue const>> axis_tags_map;
    for (auto const& values : tag_values) {
        TokenStream tag_tokens { values };
        tag_tokens.discard_whitespace();
        auto opentype_tag = parse_opentype_tag_value(tag_tokens);
        tag_tokens.discard_whitespace();
        auto number = parse_number_value(tag_tokens);
        tag_tokens.discard_whitespace();

        if (!opentype_tag || !number || tag_tokens.has_next_token())
            return nullptr;

        axis_tags_map.set(opentype_tag->string_value(), OpenTypeTaggedStyleValue::create(OpenTypeTaggedStyleValue::Mode::FontVariationSettings, opentype_tag->string_value(), number.release_nonnull()));
    }

    // "The computed value contains the de-duplicated axis names, sorted in ascending order by code unit."
    StyleValueVector axis_tags;
    axis_tags.ensure_capacity(axis_tags_map.size());
    for (auto const& [key, axis_tag] : axis_tags_map)
        axis_tags.append(axis_tag);

    quick_sort(axis_tags, [](auto& a, auto& b) {
        return a->as_open_type_tagged().tag() < b->as_open_type_tagged().tag();
    });

    transaction.commit();
    return StyleValueList::create(move(axis_tags), StyleValueList::Separator::Comma);
}

RefPtr<CSSStyleValue const> Parser::parse_font_variant(TokenStream<ComponentValue>& tokens)
{
    // 6.11 https://drafts.csswg.org/css-fonts/#propdef-font-variant
    // normal | none |
    // [ [ <common-lig-values> || <discretionary-lig-values> || <historical-lig-values> || <contextual-alt-values> ]
    // || [ small-caps | all-small-caps | petite-caps | all-petite-caps | unicase | titling-caps ] ||
    // [ FIXME: stylistic(<feature-value-name>) ||
    // historical-forms ||
    // FIXME: styleset(<feature-value-name>#) ||
    // FIXME: character-variant(<feature-value-name>#) ||
    // FIXME: swash(<feature-value-name>) ||
    // FIXME: ornaments(<feature-value-name>) ||
    // FIXME: annotation(<feature-value-name>) ] ||
    // [ <numeric-figure-values> || <numeric-spacing-values> || <numeric-fraction-values> ||
    // ordinal || slashed-zero ] || [ <east-asian-variant-values> || <east-asian-width-values> || ruby ] ||
    // [ sub | super ] || [ text | emoji | unicode ] ]

    bool has_common_ligatures = false;
    bool has_discretionary_ligatures = false;
    bool has_historical_ligatures = false;
    bool has_contextual = false;
    bool has_numeric_figures = false;
    bool has_numeric_spacing = false;
    bool has_numeric_fractions = false;
    bool has_numeric_ordinals = false;
    bool has_numeric_slashed_zero = false;
    bool has_east_asian_variant = false;
    bool has_east_asian_width = false;
    bool has_east_asian_ruby = false;
    RefPtr<CSSStyleValue const> alternates_value {};
    RefPtr<CSSStyleValue const> caps_value {};
    RefPtr<CSSStyleValue const> emoji_value {};
    RefPtr<CSSStyleValue const> position_value {};
    StyleValueVector east_asian_values;
    StyleValueVector ligatures_values;
    StyleValueVector numeric_values;

    if (auto parsed_value = parse_all_as_single_keyword_value(tokens, Keyword::Normal)) {
        // normal, do nothing
    } else if (auto parsed_value = parse_all_as_single_keyword_value(tokens, Keyword::None)) {
        // none
        ligatures_values.append(parsed_value.release_nonnull());
    } else {

        while (tokens.has_next_token()) {
            auto maybe_value = parse_keyword_value(tokens);
            if (!maybe_value)
                break;
            auto value = maybe_value.release_nonnull();
            if (!value->is_keyword()) {
                // FIXME: alternate functions such as stylistic()
                return nullptr;
            }
            auto keyword = value->to_keyword();

            switch (keyword) {
            // <common-lig-values>       = [ common-ligatures | no-common-ligatures ]
            case Keyword::CommonLigatures:
            case Keyword::NoCommonLigatures:
                if (has_common_ligatures)
                    return nullptr;
                ligatures_values.append(move(value));
                has_common_ligatures = true;
                break;
            // <discretionary-lig-values> = [ discretionary-ligatures | no-discretionary-ligatures ]
            case Keyword::DiscretionaryLigatures:
            case Keyword::NoDiscretionaryLigatures:
                if (has_discretionary_ligatures)
                    return nullptr;
                ligatures_values.append(move(value));
                has_discretionary_ligatures = true;
                break;
            // <historical-lig-values>   = [ historical-ligatures | no-historical-ligatures ]
            case Keyword::HistoricalLigatures:
            case Keyword::NoHistoricalLigatures:
                if (has_historical_ligatures)
                    return nullptr;
                ligatures_values.append(move(value));
                has_historical_ligatures = true;
                break;
            // <contextual-alt-values>   = [ contextual | no-contextual ]
            case Keyword::Contextual:
            case Keyword::NoContextual:
                if (has_contextual)
                    return nullptr;
                ligatures_values.append(move(value));
                has_contextual = true;
                break;
            // historical-forms
            case Keyword::HistoricalForms:
                if (alternates_value != nullptr)
                    return nullptr;
                alternates_value = value.ptr();
                break;
            // [ small-caps | all-small-caps | petite-caps | all-petite-caps | unicase | titling-caps ]
            case Keyword::SmallCaps:
            case Keyword::AllSmallCaps:
            case Keyword::PetiteCaps:
            case Keyword::AllPetiteCaps:
            case Keyword::Unicase:
            case Keyword::TitlingCaps:
                if (caps_value != nullptr)
                    return nullptr;
                caps_value = value.ptr();
                break;
            // <numeric-figure-values>       = [ lining-nums | oldstyle-nums ]
            case Keyword::LiningNums:
            case Keyword::OldstyleNums:
                if (has_numeric_figures)
                    return nullptr;
                numeric_values.append(move(value));
                has_numeric_figures = true;
                break;
            // <numeric-spacing-values>      = [ proportional-nums | tabular-nums ]
            case Keyword::ProportionalNums:
            case Keyword::TabularNums:
                if (has_numeric_spacing)
                    return nullptr;
                numeric_values.append(move(value));
                has_numeric_spacing = true;
                break;
            // <numeric-fraction-values>     = [ diagonal-fractions | stacked-fractions]
            case Keyword::DiagonalFractions:
            case Keyword::StackedFractions:
                if (has_numeric_fractions)
                    return nullptr;
                numeric_values.append(move(value));
                has_numeric_fractions = true;
                break;
            // ordinal
            case Keyword::Ordinal:
                if (has_numeric_ordinals)
                    return nullptr;
                numeric_values.append(move(value));
                has_numeric_ordinals = true;
                break;
            case Keyword::SlashedZero:
                if (has_numeric_slashed_zero)
                    return nullptr;
                numeric_values.append(move(value));
                has_numeric_slashed_zero = true;
                break;
            // <east-asian-variant-values> = [ jis78 | jis83 | jis90 | jis04 | simplified | traditional ]
            case Keyword::Jis78:
            case Keyword::Jis83:
            case Keyword::Jis90:
            case Keyword::Jis04:
            case Keyword::Simplified:
            case Keyword::Traditional:
                if (has_east_asian_variant)
                    return nullptr;
                east_asian_values.append(move(value));
                has_east_asian_variant = true;
                break;
            // <east-asian-width-values>   = [ full-width | proportional-width ]
            case Keyword::FullWidth:
            case Keyword::ProportionalWidth:
                if (has_east_asian_width)
                    return nullptr;
                east_asian_values.append(move(value));
                has_east_asian_width = true;
                break;
            // ruby
            case Keyword::Ruby:
                if (has_east_asian_ruby)
                    return nullptr;
                east_asian_values.append(move(value));
                has_east_asian_ruby = true;
                break;
            // text | emoji | unicode
            case Keyword::Text:
            case Keyword::Emoji:
            case Keyword::Unicode:
                if (emoji_value != nullptr)
                    return nullptr;
                emoji_value = value.ptr();
                break;
            // sub | super
            case Keyword::Sub:
            case Keyword::Super:
                if (position_value != nullptr)
                    return nullptr;
                position_value = value.ptr();
                break;
            default:
                return nullptr;
            }
        }
    }

    auto normal_value = CSSKeywordValue::create(Keyword::Normal);
    auto resolve_list = [&normal_value](StyleValueVector values) -> NonnullRefPtr<CSSStyleValue const> {
        if (values.is_empty())
            return normal_value;
        if (values.size() == 1)
            return *values.first();
        return StyleValueList::create(move(values), StyleValueList::Separator::Space);
    };

    if (!alternates_value)
        alternates_value = normal_value;
    if (!caps_value)
        caps_value = normal_value;
    if (!emoji_value)
        emoji_value = normal_value;
    if (!position_value)
        position_value = normal_value;

    quick_sort(east_asian_values, [](auto& left, auto& right) { return *keyword_to_font_variant_east_asian(left->to_keyword()) < *keyword_to_font_variant_east_asian(right->to_keyword()); });
    auto east_asian_value = resolve_list(east_asian_values);

    quick_sort(ligatures_values, [](auto& left, auto& right) { return *keyword_to_font_variant_ligatures(left->to_keyword()) < *keyword_to_font_variant_ligatures(right->to_keyword()); });
    auto ligatures_value = resolve_list(ligatures_values);

    quick_sort(numeric_values, [](auto& left, auto& right) { return *keyword_to_font_variant_numeric(left->to_keyword()) < *keyword_to_font_variant_numeric(right->to_keyword()); });
    auto numeric_value = resolve_list(numeric_values);

    return ShorthandStyleValue::create(PropertyID::FontVariant,
        { PropertyID::FontVariantAlternates,
            PropertyID::FontVariantCaps,
            PropertyID::FontVariantEastAsian,
            PropertyID::FontVariantEmoji,
            PropertyID::FontVariantLigatures,
            PropertyID::FontVariantNumeric,
            PropertyID::FontVariantPosition },
        {
            alternates_value.release_nonnull(),
            caps_value.release_nonnull(),
            move(east_asian_value),
            emoji_value.release_nonnull(),
            move(ligatures_value),
            move(numeric_value),
            position_value.release_nonnull(),
        });
}

RefPtr<CSSStyleValue const> Parser::parse_font_variant_alternates_value(TokenStream<ComponentValue>& tokens)
{
    // 6.8 https://drafts.csswg.org/css-fonts/#font-variant-alternates-prop
    // normal |
    // [ FIXME: stylistic(<feature-value-name>) ||
    //   historical-forms ||
    //   FIXME: styleset(<feature-value-name>#) ||
    //   FIXME: character-variant(<feature-value-name>#) ||
    //   FIXME: swash(<feature-value-name>) ||
    //   FIXME: ornaments(<feature-value-name>) ||
    //   FIXME: annotation(<feature-value-name>) ]

    // normal
    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal;

    // historical-forms
    // FIXME: Support this together with other values when we parse them.
    if (auto historical_forms = parse_all_as_single_keyword_value(tokens, Keyword::HistoricalForms))
        return historical_forms;

    dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-variant-alternate: parsing {} not implemented.", tokens.next_token().to_debug_string());
    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_font_variant_east_asian_value(TokenStream<ComponentValue>& tokens)
{
    // 6.10 https://drafts.csswg.org/css-fonts/#propdef-font-variant-east-asian
    // normal | [ <east-asian-variant-values> || <east-asian-width-values> || ruby ]
    // <east-asian-variant-values> = [ jis78 | jis83 | jis90 | jis04 | simplified | traditional ]
    // <east-asian-width-values>   = [ full-width | proportional-width ]

    // normal
    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal.release_nonnull();

    // [ <east-asian-variant-values> || <east-asian-width-values> || ruby ]
    RefPtr<CSSStyleValue const> ruby_value;
    RefPtr<CSSStyleValue const> variant_value;
    RefPtr<CSSStyleValue const> width_value;

    while (tokens.has_next_token()) {
        auto maybe_value = parse_keyword_value(tokens);
        if (!maybe_value)
            break;
        auto font_variant_east_asian = keyword_to_font_variant_east_asian(maybe_value->to_keyword());
        if (!font_variant_east_asian.has_value())
            return nullptr;

        switch (font_variant_east_asian.value()) {
        case FontVariantEastAsian::Ruby:
            if (ruby_value)
                return nullptr;
            ruby_value = maybe_value.release_nonnull();
            break;
        case FontVariantEastAsian::FullWidth:
        case FontVariantEastAsian::ProportionalWidth:
            if (width_value)
                return nullptr;
            width_value = maybe_value.release_nonnull();
            break;
        case FontVariantEastAsian::Jis78:
        case FontVariantEastAsian::Jis83:
        case FontVariantEastAsian::Jis90:
        case FontVariantEastAsian::Jis04:
        case FontVariantEastAsian::Simplified:
        case FontVariantEastAsian::Traditional:
            if (variant_value)
                return nullptr;
            variant_value = maybe_value.release_nonnull();
            break;
        case FontVariantEastAsian::Normal:
            return nullptr;
        }
    }

    StyleValueVector values;
    if (variant_value)
        values.append(variant_value.release_nonnull());
    if (width_value)
        values.append(width_value.release_nonnull());
    if (ruby_value)
        values.append(ruby_value.release_nonnull());

    if (values.is_empty())
        return nullptr;
    if (values.size() == 1)
        return *values.first();

    return StyleValueList::create(move(values), StyleValueList::Separator::Space);
}

RefPtr<CSSStyleValue const> Parser::parse_font_variant_ligatures_value(TokenStream<ComponentValue>& tokens)
{
    // 6.4 https://drafts.csswg.org/css-fonts/#propdef-font-variant-ligatures
    // normal | none | [ <common-lig-values> || <discretionary-lig-values> || <historical-lig-values> || <contextual-alt-values> ]
    // <common-lig-values>       = [ common-ligatures | no-common-ligatures ]
    // <discretionary-lig-values> = [ discretionary-ligatures | no-discretionary-ligatures ]
    // <historical-lig-values>   = [ historical-ligatures | no-historical-ligatures ]
    // <contextual-alt-values>   = [ contextual | no-contextual ]

    // normal
    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal.release_nonnull();

    // none
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none.release_nonnull();

    // [ <common-lig-values> || <discretionary-lig-values> || <historical-lig-values> || <contextual-alt-values> ]
    RefPtr<CSSStyleValue const> common_ligatures_value;
    RefPtr<CSSStyleValue const> discretionary_ligatures_value;
    RefPtr<CSSStyleValue const> historical_ligatures_value;
    RefPtr<CSSStyleValue const> contextual_value;

    while (tokens.has_next_token()) {
        auto maybe_value = parse_keyword_value(tokens);
        if (!maybe_value)
            break;
        auto font_variant_ligatures = keyword_to_font_variant_ligatures(maybe_value->to_keyword());
        if (!font_variant_ligatures.has_value())
            return nullptr;

        switch (font_variant_ligatures.value()) {
        // <common-lig-values>       = [ common-ligatures | no-common-ligatures ]
        case FontVariantLigatures::CommonLigatures:
        case FontVariantLigatures::NoCommonLigatures:
            if (common_ligatures_value)
                return nullptr;
            common_ligatures_value = maybe_value.release_nonnull();
            break;
        // <discretionary-lig-values> = [ discretionary-ligatures | no-discretionary-ligatures ]
        case FontVariantLigatures::DiscretionaryLigatures:
        case FontVariantLigatures::NoDiscretionaryLigatures:
            if (discretionary_ligatures_value)
                return nullptr;
            discretionary_ligatures_value = maybe_value.release_nonnull();
            break;
        // <historical-lig-values> = [ historical-ligatures | no-historical-ligatures ]
        case FontVariantLigatures::HistoricalLigatures:
        case FontVariantLigatures::NoHistoricalLigatures:
            if (historical_ligatures_value)
                return nullptr;
            historical_ligatures_value = maybe_value.release_nonnull();
            break;
        // <contextual-alt-values> = [ contextual | no-contextual ]
        case FontVariantLigatures::Contextual:
        case FontVariantLigatures::NoContextual:
            if (contextual_value)
                return nullptr;
            contextual_value = maybe_value.release_nonnull();
            break;
        case FontVariantLigatures::Normal:
        case FontVariantLigatures::None:
            return nullptr;
        }
    }

    StyleValueVector values;
    if (common_ligatures_value)
        values.append(common_ligatures_value.release_nonnull());
    if (discretionary_ligatures_value)
        values.append(discretionary_ligatures_value.release_nonnull());
    if (historical_ligatures_value)
        values.append(historical_ligatures_value.release_nonnull());
    if (contextual_value)
        values.append(contextual_value.release_nonnull());

    if (values.is_empty())
        return nullptr;
    if (values.size() == 1)
        return *values.first();

    return StyleValueList::create(move(values), StyleValueList::Separator::Space);
}

RefPtr<CSSStyleValue const> Parser::parse_font_variant_numeric_value(TokenStream<ComponentValue>& tokens)
{
    // 6.7 https://drafts.csswg.org/css-fonts/#propdef-font-variant-numeric
    // normal | [ <numeric-figure-values> || <numeric-spacing-values> || <numeric-fraction-values> || ordinal || slashed-zero]
    // <numeric-figure-values>       = [ lining-nums | oldstyle-nums ]
    // <numeric-spacing-values>      = [ proportional-nums | tabular-nums ]
    // <numeric-fraction-values>     = [ diagonal-fractions | stacked-fractions ]

    // normal
    if (auto normal = parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return normal.release_nonnull();

    RefPtr<CSSStyleValue const> figures_value;
    RefPtr<CSSStyleValue const> spacing_value;
    RefPtr<CSSStyleValue const> fractions_value;
    RefPtr<CSSStyleValue const> ordinals_value;
    RefPtr<CSSStyleValue const> slashed_zero_value;

    // [ <numeric-figure-values> || <numeric-spacing-values> || <numeric-fraction-values> || ordinal || slashed-zero]
    while (tokens.has_next_token()) {
        auto maybe_value = parse_keyword_value(tokens);
        if (!maybe_value)
            break;
        auto font_variant_numeric = keyword_to_font_variant_numeric(maybe_value->to_keyword());
        if (!font_variant_numeric.has_value())
            return nullptr;
        switch (font_variant_numeric.value()) {
        // ... || ordinal
        case FontVariantNumeric::Ordinal:
            if (ordinals_value)
                return nullptr;
            ordinals_value = maybe_value.release_nonnull();
            break;
        // ... || slashed-zero
        case FontVariantNumeric::SlashedZero:
            if (slashed_zero_value)
                return nullptr;
            slashed_zero_value = maybe_value.release_nonnull();
            break;
        // <numeric-figure-values> = [ lining-nums | oldstyle-nums ]
        case FontVariantNumeric::LiningNums:
        case FontVariantNumeric::OldstyleNums:
            if (figures_value)
                return nullptr;
            figures_value = maybe_value.release_nonnull();
            break;
        // <numeric-spacing-values> = [ proportional-nums | tabular-nums ]
        case FontVariantNumeric::ProportionalNums:
        case FontVariantNumeric::TabularNums:
            if (spacing_value)
                return nullptr;
            spacing_value = maybe_value.release_nonnull();
            break;
        // <numeric-fraction-values> = [ diagonal-fractions | stacked-fractions ]
        case FontVariantNumeric::DiagonalFractions:
        case FontVariantNumeric::StackedFractions:
            if (fractions_value)
                return nullptr;
            fractions_value = maybe_value.release_nonnull();
            break;
        case FontVariantNumeric::Normal:
            return nullptr;
        }
    }

    StyleValueVector values;
    if (figures_value)
        values.append(figures_value.release_nonnull());
    if (spacing_value)
        values.append(spacing_value.release_nonnull());
    if (fractions_value)
        values.append(fractions_value.release_nonnull());
    if (ordinals_value)
        values.append(ordinals_value.release_nonnull());
    if (slashed_zero_value)
        values.append(slashed_zero_value.release_nonnull());

    if (values.is_empty())
        return nullptr;
    if (values.size() == 1)
        return *values.first();

    return StyleValueList::create(move(values), StyleValueList::Separator::Space);
}

RefPtr<CSSStyleValue const> Parser::parse_list_style_value(TokenStream<ComponentValue>& tokens)
{
    RefPtr<CSSStyleValue const> list_position;
    RefPtr<CSSStyleValue const> list_image;
    RefPtr<CSSStyleValue const> list_type;
    int found_nones = 0;

    Vector<PropertyID> remaining_longhands { PropertyID::ListStyleImage, PropertyID::ListStylePosition, PropertyID::ListStyleType };

    auto transaction = tokens.begin_transaction();
    while (tokens.has_next_token()) {
        if (auto const& peek = tokens.next_token(); peek.is_ident("none"sv)) {
            tokens.discard_a_token();
            found_nones++;
            continue;
        }

        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::ListStylePosition: {
            VERIFY(!list_position);
            list_position = value.release_nonnull();
            continue;
        }
        case PropertyID::ListStyleImage: {
            VERIFY(!list_image);
            list_image = value.release_nonnull();
            continue;
        }
        case PropertyID::ListStyleType: {
            VERIFY(!list_type);
            list_type = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (found_nones > 2)
        return nullptr;

    if (found_nones == 2) {
        if (list_image || list_type)
            return nullptr;
        auto none = CSSKeywordValue::create(Keyword::None);
        list_image = none;
        list_type = none;

    } else if (found_nones == 1) {
        if (list_image && list_type)
            return nullptr;
        auto none = CSSKeywordValue::create(Keyword::None);
        if (!list_image)
            list_image = none;
        if (!list_type)
            list_type = none;
    }

    if (!list_position)
        list_position = property_initial_value(PropertyID::ListStylePosition);
    if (!list_image)
        list_image = property_initial_value(PropertyID::ListStyleImage);
    if (!list_type)
        list_type = property_initial_value(PropertyID::ListStyleType);

    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::ListStyle,
        { PropertyID::ListStylePosition, PropertyID::ListStyleImage, PropertyID::ListStyleType },
        { list_position.release_nonnull(), list_image.release_nonnull(), list_type.release_nonnull() });
}

RefPtr<CSSStyleValue const> Parser::parse_math_depth_value(TokenStream<ComponentValue>& tokens)
{
    // https://w3c.github.io/mathml-core/#propdef-math-depth
    // auto-add | add(<integer>) | <integer>
    auto transaction = tokens.begin_transaction();

    // auto-add
    if (tokens.next_token().is_ident("auto-add"sv)) {
        tokens.discard_a_token(); // auto-add
        transaction.commit();
        return MathDepthStyleValue::create_auto_add();
    }

    // add(<integer>)
    if (tokens.next_token().is_function("add"sv)) {
        auto const& function = tokens.next_token().function();
        auto context_guard = push_temporary_value_parsing_context(FunctionContext { function.name });

        auto add_tokens = TokenStream { function.value };
        add_tokens.discard_whitespace();
        if (auto integer_value = parse_integer_value(add_tokens)) {
            add_tokens.discard_whitespace();
            if (add_tokens.has_next_token())
                return nullptr;
            tokens.discard_a_token(); // add()
            transaction.commit();
            return MathDepthStyleValue::create_add(integer_value.release_nonnull());
        }
        return nullptr;
    }

    // <integer>
    if (auto integer_value = parse_integer_value(tokens)) {
        transaction.commit();
        return MathDepthStyleValue::create_integer(integer_value.release_nonnull());
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_opacity_value(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    auto value = parse_css_value_for_property(property_id, tokens);
    if (!value)
        return nullptr;

    // Percentages map to the range [0,1] for opacity values
    if (value->is_percentage())
        value = NumberStyleValue::create(value->as_percentage().percentage().as_fraction());
    if (value->is_calculated() && value->as_calculated().resolves_to_percentage()) {
        auto maybe_percentage = value->as_calculated().resolve_percentage_deprecated({});
        if (maybe_percentage.has_value()) {
            auto resolved_percentage = maybe_percentage->as_fraction();
            CalculationContext context {};
            auto calc_node = NumericCalculationNode::create(Number { Number::Type::Number, resolved_percentage }, context);
            value = CalculatedStyleValue::create(move(calc_node), CSSNumericType { CSSNumericType::BaseType::Length, 1 }, context);
        }
    }

    return value;
}

RefPtr<CSSStyleValue const> Parser::parse_overflow_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    auto maybe_x_value = parse_css_value_for_property(PropertyID::OverflowX, tokens);
    if (!maybe_x_value)
        return nullptr;
    auto maybe_y_value = parse_css_value_for_property(PropertyID::OverflowY, tokens);
    transaction.commit();
    if (maybe_y_value) {
        return ShorthandStyleValue::create(PropertyID::Overflow,
            { PropertyID::OverflowX, PropertyID::OverflowY },
            { maybe_x_value.release_nonnull(), maybe_y_value.release_nonnull() });
    }
    return ShorthandStyleValue::create(PropertyID::Overflow,
        { PropertyID::OverflowX, PropertyID::OverflowY },
        { *maybe_x_value, *maybe_x_value });
}

RefPtr<CSSStyleValue const> Parser::parse_place_content_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    auto maybe_align_content_value = parse_css_value_for_property(PropertyID::AlignContent, tokens);
    if (!maybe_align_content_value)
        return nullptr;

    if (!tokens.has_next_token()) {
        if (!property_accepts_keyword(PropertyID::JustifyContent, maybe_align_content_value->to_keyword()))
            return nullptr;
        transaction.commit();
        return ShorthandStyleValue::create(PropertyID::PlaceContent,
            { PropertyID::AlignContent, PropertyID::JustifyContent },
            { *maybe_align_content_value, *maybe_align_content_value });
    }

    auto maybe_justify_content_value = parse_css_value_for_property(PropertyID::JustifyContent, tokens);
    if (!maybe_justify_content_value)
        return nullptr;
    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::PlaceContent,
        { PropertyID::AlignContent, PropertyID::JustifyContent },
        { maybe_align_content_value.release_nonnull(), maybe_justify_content_value.release_nonnull() });
}

RefPtr<CSSStyleValue const> Parser::parse_place_items_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    auto maybe_align_items_value = parse_css_value_for_property(PropertyID::AlignItems, tokens);
    if (!maybe_align_items_value)
        return nullptr;

    if (!tokens.has_next_token()) {
        if (!property_accepts_keyword(PropertyID::JustifyItems, maybe_align_items_value->to_keyword()))
            return nullptr;
        transaction.commit();
        return ShorthandStyleValue::create(PropertyID::PlaceItems,
            { PropertyID::AlignItems, PropertyID::JustifyItems },
            { *maybe_align_items_value, *maybe_align_items_value });
    }

    auto maybe_justify_items_value = parse_css_value_for_property(PropertyID::JustifyItems, tokens);
    if (!maybe_justify_items_value)
        return nullptr;
    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::PlaceItems,
        { PropertyID::AlignItems, PropertyID::JustifyItems },
        { *maybe_align_items_value, *maybe_justify_items_value });
}

RefPtr<CSSStyleValue const> Parser::parse_place_self_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    auto maybe_align_self_value = parse_css_value_for_property(PropertyID::AlignSelf, tokens);
    if (!maybe_align_self_value)
        return nullptr;

    if (!tokens.has_next_token()) {
        if (!property_accepts_keyword(PropertyID::JustifySelf, maybe_align_self_value->to_keyword()))
            return nullptr;
        transaction.commit();
        return ShorthandStyleValue::create(PropertyID::PlaceSelf,
            { PropertyID::AlignSelf, PropertyID::JustifySelf },
            { *maybe_align_self_value, *maybe_align_self_value });
    }

    auto maybe_justify_self_value = parse_css_value_for_property(PropertyID::JustifySelf, tokens);
    if (!maybe_justify_self_value)
        return nullptr;
    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::PlaceSelf,
        { PropertyID::AlignSelf, PropertyID::JustifySelf },
        { *maybe_align_self_value, *maybe_justify_self_value });
}

RefPtr<CSSStyleValue const> Parser::parse_quotes_value(TokenStream<ComponentValue>& tokens)
{
    // https://www.w3.org/TR/css-content-3/#quotes-property
    // auto | none | [ <string> <string> ]+
    auto transaction = tokens.begin_transaction();

    if (tokens.remaining_token_count() == 1) {
        auto keyword = parse_keyword_value(tokens);
        if (keyword && property_accepts_keyword(PropertyID::Quotes, keyword->to_keyword())) {
            transaction.commit();
            return keyword;
        }
        return nullptr;
    }

    // Parse an even number of <string> values.
    if (tokens.remaining_token_count() % 2 != 0)
        return nullptr;

    StyleValueVector string_values;
    while (tokens.has_next_token()) {
        auto maybe_string = parse_string_value(tokens);
        if (!maybe_string)
            return nullptr;

        string_values.append(maybe_string.release_nonnull());
    }

    transaction.commit();
    return StyleValueList::create(move(string_values), StyleValueList::Separator::Space);
}

RefPtr<CSSStyleValue const> Parser::parse_text_decoration_value(TokenStream<ComponentValue>& tokens)
{
    RefPtr<CSSStyleValue const> decoration_line;
    RefPtr<CSSStyleValue const> decoration_thickness;
    RefPtr<CSSStyleValue const> decoration_style;
    RefPtr<CSSStyleValue const> decoration_color;

    auto remaining_longhands = Vector { PropertyID::TextDecorationColor, PropertyID::TextDecorationLine, PropertyID::TextDecorationStyle, PropertyID::TextDecorationThickness };

    auto transaction = tokens.begin_transaction();
    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::TextDecorationColor: {
            VERIFY(!decoration_color);
            decoration_color = value.release_nonnull();
            continue;
        }
        case PropertyID::TextDecorationLine: {
            VERIFY(!decoration_line);
            tokens.reconsume_current_input_token();
            auto parsed_decoration_line = parse_text_decoration_line_value(tokens);
            if (!parsed_decoration_line)
                return nullptr;
            decoration_line = parsed_decoration_line.release_nonnull();
            continue;
        }
        case PropertyID::TextDecorationThickness: {
            VERIFY(!decoration_thickness);
            decoration_thickness = value.release_nonnull();
            continue;
        }
        case PropertyID::TextDecorationStyle: {
            VERIFY(!decoration_style);
            decoration_style = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!decoration_line)
        decoration_line = property_initial_value(PropertyID::TextDecorationLine);
    if (!decoration_thickness)
        decoration_thickness = property_initial_value(PropertyID::TextDecorationThickness);
    if (!decoration_style)
        decoration_style = property_initial_value(PropertyID::TextDecorationStyle);
    if (!decoration_color)
        decoration_color = property_initial_value(PropertyID::TextDecorationColor);

    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::TextDecoration,
        { PropertyID::TextDecorationLine, PropertyID::TextDecorationThickness, PropertyID::TextDecorationStyle, PropertyID::TextDecorationColor },
        { decoration_line.release_nonnull(), decoration_thickness.release_nonnull(), decoration_style.release_nonnull(), decoration_color.release_nonnull() });
}

RefPtr<CSSStyleValue const> Parser::parse_text_decoration_line_value(TokenStream<ComponentValue>& tokens)
{
    StyleValueVector style_values;

    bool includes_spelling_or_grammar_error_value = false;

    while (tokens.has_next_token()) {
        auto maybe_value = parse_css_value_for_property(PropertyID::TextDecorationLine, tokens);
        if (!maybe_value)
            break;
        auto value = maybe_value.release_nonnull();

        if (auto maybe_line = keyword_to_text_decoration_line(value->to_keyword()); maybe_line.has_value()) {
            if (maybe_line == TextDecorationLine::None) {
                if (!style_values.is_empty())
                    break;
                return value;
            }
            if (first_is_one_of(*maybe_line, TextDecorationLine::SpellingError, TextDecorationLine::GrammarError)) {
                includes_spelling_or_grammar_error_value = true;
            }
            if (style_values.contains_slow(value))
                return nullptr;
            style_values.append(move(value));
            continue;
        }

        break;
    }

    if (style_values.is_empty())
        return nullptr;

    // These can only appear on their own.
    if (style_values.size() > 1 && includes_spelling_or_grammar_error_value)
        return nullptr;

    if (style_values.size() == 1)
        return *style_values.first();

    quick_sort(style_values, [](auto& left, auto& right) {
        return *keyword_to_text_decoration_line(left->to_keyword()) < *keyword_to_text_decoration_line(right->to_keyword());
    });

    return StyleValueList::create(move(style_values), StyleValueList::Separator::Space);
}

// https://www.w3.org/TR/pointerevents/#the-touch-action-css-property
RefPtr<CSSStyleValue const> Parser::parse_touch_action_value(TokenStream<ComponentValue>& tokens)
{
    // auto | none | [ [ pan-x | pan-left | pan-right ] || [ pan-y | pan-up | pan-down ] ] | manipulation

    if (auto value = parse_all_as_single_keyword_value(tokens, Keyword::Auto))
        return value;
    if (auto value = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return value;
    if (auto value = parse_all_as_single_keyword_value(tokens, Keyword::Manipulation))
        return value;

    StyleValueVector parsed_values;
    auto transaction = tokens.begin_transaction();

    // We will verify that we have up to one vertical and one horizontal value
    bool has_horizontal = false;
    bool has_vertical = false;

    // Were the values specified in y/x order? (we need to store them in canonical x/y order)
    bool swap_order = false;

    while (auto parsed_value = parse_css_value_for_property(PropertyID::TouchAction, tokens)) {
        switch (parsed_value->as_keyword().keyword()) {
        case Keyword::PanX:
        case Keyword::PanLeft:
        case Keyword::PanRight:
            if (has_horizontal)
                return {};
            if (has_vertical)
                swap_order = true;
            has_horizontal = true;
            break;
        case Keyword::PanY:
        case Keyword::PanUp:
        case Keyword::PanDown:
            if (has_vertical)
                return {};
            has_vertical = true;
            break;
        case Keyword::Auto:
        case Keyword::None:
        case Keyword::Manipulation:
            // Not valid as part of a list
            return {};
        default:
            VERIFY_NOT_REACHED();
        }

        parsed_values.append(parsed_value.release_nonnull());
        if (!tokens.has_next_token())
            break;
    }

    if (swap_order)
        swap(parsed_values[0], parsed_values[1]);

    transaction.commit();
    return StyleValueList::create(move(parsed_values), StyleValueList::Separator::Space);
}

// https://www.w3.org/TR/css-transforms-1/#transform-property
RefPtr<CSSStyleValue const> Parser::parse_transform_value(TokenStream<ComponentValue>& tokens)
{
    // <transform> = none | <transform-list>
    // <transform-list> = <transform-function>+

    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    StyleValueVector transformations;
    auto transaction = tokens.begin_transaction();
    while (tokens.has_next_token()) {
        auto const& part = tokens.consume_a_token();
        if (!part.is_function())
            return nullptr;
        auto maybe_function = transform_function_from_string(part.function().name);
        if (!maybe_function.has_value())
            return nullptr;

        auto context_guard = push_temporary_value_parsing_context(FunctionContext { part.function().name });

        auto function = maybe_function.release_value();
        auto function_metadata = transform_function_metadata(function);

        auto function_tokens = TokenStream { part.function().value };
        auto arguments = parse_a_comma_separated_list_of_component_values(function_tokens);

        if (arguments.size() > function_metadata.parameters.size()) {
            dbgln_if(CSS_PARSER_DEBUG, "Too many arguments to {}. max: {}", part.function().name, function_metadata.parameters.size());
            return nullptr;
        }

        if (arguments.size() < function_metadata.parameters.size() && function_metadata.parameters[arguments.size()].required) {
            dbgln_if(CSS_PARSER_DEBUG, "Required parameter at position {} is missing", arguments.size());
            return nullptr;
        }

        StyleValueVector values;
        for (auto argument_index = 0u; argument_index < arguments.size(); ++argument_index) {
            TokenStream argument_tokens { arguments[argument_index] };
            argument_tokens.discard_whitespace();

            switch (function_metadata.parameters[argument_index].type) {
            case TransformFunctionParameterType::Angle: {
                // These are `<angle> | <zero>` in the spec, so we have to check for both kinds.
                if (auto angle_value = parse_angle_value(argument_tokens)) {
                    values.append(angle_value.release_nonnull());
                    break;
                }
                if (argument_tokens.next_token().is(Token::Type::Number) && argument_tokens.next_token().token().number_value() == 0) {
                    argument_tokens.discard_a_token(); // 0
                    values.append(AngleStyleValue::create(Angle::make_degrees(0)));
                    break;
                }
                return nullptr;
            }
            case TransformFunctionParameterType::Length:
            case TransformFunctionParameterType::LengthNone: {
                if (auto length_value = parse_length_value(argument_tokens)) {
                    values.append(length_value.release_nonnull());
                    break;
                }
                if (function_metadata.parameters[argument_index].type == TransformFunctionParameterType::LengthNone
                    && argument_tokens.next_token().is_ident("none"sv)) {

                    argument_tokens.discard_a_token(); // none
                    values.append(CSSKeywordValue::create(Keyword::None));
                    break;
                }
                return nullptr;
            }
            case TransformFunctionParameterType::LengthPercentage: {
                if (auto length_percentage_value = parse_length_percentage_value(argument_tokens)) {
                    values.append(length_percentage_value.release_nonnull());
                    break;
                }
                return nullptr;
            }
            case TransformFunctionParameterType::Number: {
                if (auto number_value = parse_number_value(argument_tokens)) {
                    values.append(number_value.release_nonnull());
                    break;
                }
                return nullptr;
            }
            case TransformFunctionParameterType::NumberPercentage: {
                if (auto number_percentage_value = parse_number_percentage_value(argument_tokens)) {
                    values.append(number_percentage_value.release_nonnull());
                    break;
                }
                return nullptr;
            }
            }

            argument_tokens.discard_whitespace();
            if (argument_tokens.has_next_token())
                return nullptr;
        }

        transformations.append(TransformationStyleValue::create(PropertyID::Transform, function, move(values)));
    }
    transaction.commit();
    return StyleValueList::create(move(transformations), StyleValueList::Separator::Space);
}

// https://www.w3.org/TR/css-transforms-1/#propdef-transform-origin
RefPtr<CSSStyleValue const> Parser::parse_transform_origin_value(TokenStream<ComponentValue>& tokens)
{
    enum class Axis {
        None,
        X,
        Y,
    };

    struct AxisOffset {
        Axis axis;
        NonnullRefPtr<CSSStyleValue const> offset;
    };

    auto to_axis_offset = [](RefPtr<CSSStyleValue const> value) -> Optional<AxisOffset> {
        if (!value)
            return OptionalNone {};
        if (value->is_percentage())
            return AxisOffset { Axis::None, value->as_percentage() };
        if (value->is_length())
            return AxisOffset { Axis::None, value->as_length() };
        if (value->is_keyword()) {
            switch (value->to_keyword()) {
            case Keyword::Top:
                return AxisOffset { Axis::Y, value.release_nonnull() };
            case Keyword::Left:
                return AxisOffset { Axis::X, value.release_nonnull() };
            case Keyword::Center:
                return AxisOffset { Axis::None, value.release_nonnull() };
            case Keyword::Bottom:
                return AxisOffset { Axis::Y, value.release_nonnull() };
            case Keyword::Right:
                return AxisOffset { Axis::X, value.release_nonnull() };
            default:
                return OptionalNone {};
            }
        }
        if (value->is_calculated()) {
            return AxisOffset { Axis::None, value->as_calculated() };
        }
        return OptionalNone {};
    };

    auto transaction = tokens.begin_transaction();

    auto make_list = [&transaction](NonnullRefPtr<CSSStyleValue const> const& x_value, NonnullRefPtr<CSSStyleValue const> const& y_value, NonnullRefPtr<CSSStyleValue const> const& z_value) -> NonnullRefPtr<StyleValueList> {
        transaction.commit();
        return StyleValueList::create(StyleValueVector { x_value, y_value, z_value }, StyleValueList::Separator::Space);
    };

    static CSSStyleValue const& zero_value = LengthStyleValue::create(Length::make_px(0));

    if (tokens.remaining_token_count() == 1) {
        auto single_value = to_axis_offset(parse_css_value_for_property(PropertyID::TransformOrigin, tokens));
        if (!single_value.has_value())
            return nullptr;
        // If only one value is specified, the second value is assumed to be center.
        // FIXME: If one or two values are specified, the third value is assumed to be 0px.
        switch (single_value->axis) {
        case Axis::None:
        case Axis::X:
            return make_list(single_value->offset, CSSKeywordValue::create(Keyword::Center), zero_value);
        case Axis::Y:
            return make_list(CSSKeywordValue::create(Keyword::Center), single_value->offset, zero_value);
        }
        VERIFY_NOT_REACHED();
    }

    if (tokens.remaining_token_count() > 3)
        return nullptr;

    auto first_value = to_axis_offset(parse_css_value_for_property(PropertyID::TransformOrigin, tokens));
    auto second_value = to_axis_offset(parse_css_value_for_property(PropertyID::TransformOrigin, tokens));
    auto third_value = parse_length_value(tokens);

    if (!first_value.has_value() || !second_value.has_value())
        return nullptr;

    if ((first_value->offset->is_length() || first_value->offset->is_percentage()) && second_value->axis == Axis::X)
        return nullptr;
    if ((second_value->offset->is_length() || second_value->offset->is_percentage()) && first_value->axis == Axis::Y)
        return nullptr;

    if (!third_value)
        third_value = zero_value;

    RefPtr<CSSStyleValue const> x_value;
    RefPtr<CSSStyleValue const> y_value;

    if (first_value->axis == Axis::X) {
        x_value = first_value->offset;
    } else if (first_value->axis == Axis::Y) {
        y_value = first_value->offset;
    }

    if (second_value->axis == Axis::X) {
        if (x_value)
            return nullptr;
        x_value = second_value->offset;
        // Put the other in Y since its axis can't have been X
        y_value = first_value->offset;
    } else if (second_value->axis == Axis::Y) {
        if (y_value)
            return nullptr;
        y_value = second_value->offset;
        // Put the other in X since its axis can't have been Y
        x_value = first_value->offset;
    } else {
        if (x_value) {
            VERIFY(!y_value);
            y_value = second_value->offset;
        } else {
            VERIFY(!x_value);
            x_value = second_value->offset;
        }
    }
    // If two or more values are defined and either no value is a keyword, or the only used keyword is center,
    // then the first value represents the horizontal position (or offset) and the second represents the vertical position (or offset).
    // A third value always represents the Z position (or offset) and must be of type <length>.
    if (first_value->axis == Axis::None && second_value->axis == Axis::None) {
        x_value = first_value->offset;
        y_value = second_value->offset;
    }

    return make_list(x_value.release_nonnull(), y_value.release_nonnull(), third_value.release_nonnull());
}

RefPtr<CSSStyleValue const> Parser::parse_transition_value(TokenStream<ComponentValue>& tokens)
{
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    Vector<TransitionStyleValue::Transition> transitions;
    auto transaction = tokens.begin_transaction();

    while (tokens.has_next_token()) {
        TransitionStyleValue::Transition transition;
        auto time_value_count = 0;
        bool transition_behavior_found = false;
        while (tokens.has_next_token() && !tokens.next_token().is(Token::Type::Comma)) {
            if (auto maybe_time = parse_time(tokens); maybe_time.has_value()) {
                auto time = maybe_time.release_value();
                switch (time_value_count) {
                case 0:
                    if (!time.is_calculated() && !property_accepts_time(PropertyID::TransitionDuration, time.value()))
                        return nullptr;
                    transition.duration = move(time);
                    break;
                case 1:
                    if (!time.is_calculated() && !property_accepts_time(PropertyID::TransitionDelay, time.value()))
                        return nullptr;
                    transition.delay = move(time);
                    break;
                default:
                    dbgln_if(CSS_PARSER_DEBUG, "Transition property has more than two time values");
                    return {};
                }
                time_value_count++;
                continue;
            }

            if (auto easing = parse_easing_value(tokens)) {
                if (transition.easing) {
                    dbgln_if(CSS_PARSER_DEBUG, "Transition property has multiple easing values");
                    return {};
                }

                transition.easing = easing->as_easing();
                continue;
            }

            if (!transition_behavior_found && (tokens.peek_token().is_ident("normal"sv) || tokens.peek_token().is_ident("allow-discrete"sv))) {
                transition_behavior_found = true;
                auto ident = tokens.consume_a_token().token().ident();
                if (ident == "allow-discrete"sv)
                    transition.transition_behavior = TransitionBehavior::AllowDiscrete;
                continue;
            }

            if (auto token = tokens.peek_token(); token.is_ident("all"sv)) {
                auto transition_keyword = parse_keyword_value(tokens);
                VERIFY(transition_keyword->to_keyword() == Keyword::All);
                if (transition.property_name) {
                    dbgln_if(CSS_PARSER_DEBUG, "Transition property has multiple property identifiers");
                    return {};
                }
                transition.property_name = transition_keyword.release_nonnull();
                continue;
            }

            if (auto transition_property = parse_custom_ident_value(tokens, { { "all"sv, "none"sv } })) {
                if (transition.property_name) {
                    dbgln_if(CSS_PARSER_DEBUG, "Transition property has multiple property identifiers");
                    return {};
                }

                auto custom_ident = transition_property->custom_ident();
                if (auto property = property_id_from_string(custom_ident); property.has_value()) {
                    transition.property_name = CustomIdentStyleValue::create(custom_ident);
                    continue;
                }
            }

            dbgln_if(CSS_PARSER_DEBUG, "Transition property has unexpected token \"{}\"", tokens.next_token().to_string());
            return {};
        }

        if (!transition.property_name)
            transition.property_name = CSSKeywordValue::create(Keyword::All);

        if (!transition.easing)
            transition.easing = EasingStyleValue::create(EasingStyleValue::CubicBezier::ease());

        transitions.append(move(transition));

        if (!tokens.next_token().is(Token::Type::Comma))
            break;

        tokens.discard_a_token();
    }

    transaction.commit();
    return TransitionStyleValue::create(move(transitions));
}

RefPtr<CSSStyleValue const> Parser::parse_list_of_time_values(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    auto time_values = parse_a_comma_separated_list_of_component_values(tokens);
    StyleValueVector time_value_list;
    for (auto const& value : time_values) {
        TokenStream time_value_tokens { value };
        auto time_style_value = parse_time_value(time_value_tokens);
        if (!time_style_value)
            return nullptr;
        if (time_value_tokens.has_next_token())
            return nullptr;
        if (!time_style_value->is_calculated() && !property_accepts_time(property_id, time_style_value->as_time().time()))
            return nullptr;
        time_value_list.append(*time_style_value);
    }

    transaction.commit();
    return StyleValueList::create(move(time_value_list), StyleValueList::Separator::Comma);
}

RefPtr<CSSStyleValue const> Parser::parse_transition_property_value(TokenStream<ComponentValue>& tokens)
{
    // https://drafts.csswg.org/css-transitions/#transition-property-property
    // none | <single-transition-property>#

    // none
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    // <single-transition-property>#
    // <single-transition-property> = all | <custom-ident>
    auto transaction = tokens.begin_transaction();
    auto transition_property_values = parse_a_comma_separated_list_of_component_values(tokens);

    StyleValueVector transition_properties;
    for (auto const& value : transition_property_values) {
        TokenStream transition_property_tokens { value };
        if (auto all_keyword_value = parse_all_as_single_keyword_value(transition_property_tokens, Keyword::All)) {
            transition_properties.append(*all_keyword_value);
        } else {
            auto custom_ident = parse_custom_ident_value(transition_property_tokens, { { "all"sv, "none"sv } });
            if (!custom_ident || transition_property_tokens.has_next_token())
                return nullptr;

            transition_properties.append(custom_ident.release_nonnull());
        }
    }
    transaction.commit();
    return StyleValueList::create(move(transition_properties), StyleValueList::Separator::Comma);
}

RefPtr<CSSStyleValue const> Parser::parse_translate_value(TokenStream<ComponentValue>& tokens)
{
    if (tokens.remaining_token_count() == 1) {
        // "none"
        if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
            return none;
    }

    auto transaction = tokens.begin_transaction();

    auto maybe_x = parse_length_percentage_value(tokens);
    if (!maybe_x)
        return nullptr;

    if (!tokens.has_next_token()) {
        transaction.commit();
        return TransformationStyleValue::create(PropertyID::Translate, TransformFunction::Translate, { maybe_x.release_nonnull(), LengthStyleValue::create(Length::make_px(0)) });
    }

    auto maybe_y = parse_length_percentage_value(tokens);
    if (!maybe_y)
        return nullptr;

    if (!tokens.has_next_token()) {
        transaction.commit();
        return TransformationStyleValue::create(PropertyID::Translate, TransformFunction::Translate, { maybe_x.release_nonnull(), maybe_y.release_nonnull() });
    }

    auto maybe_z = parse_length_value(tokens);
    if (!maybe_z)
        return nullptr;

    transaction.commit();
    return TransformationStyleValue::create(PropertyID::Translate, TransformFunction::Translate3d, { maybe_x.release_nonnull(), maybe_y.release_nonnull(), maybe_z.release_nonnull() });
}

RefPtr<CSSStyleValue const> Parser::parse_scale_value(TokenStream<ComponentValue>& tokens)
{
    if (tokens.remaining_token_count() == 1) {
        // "none"
        if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
            return none;
    }

    auto transaction = tokens.begin_transaction();

    auto maybe_x = parse_number_percentage_value(tokens);
    if (!maybe_x)
        return nullptr;

    if (!tokens.has_next_token()) {
        transaction.commit();
        return TransformationStyleValue::create(PropertyID::Scale, TransformFunction::Scale, { *maybe_x, *maybe_x });
    }

    auto maybe_y = parse_number_percentage_value(tokens);
    if (!maybe_y)
        return nullptr;

    if (!tokens.has_next_token()) {
        transaction.commit();
        return TransformationStyleValue::create(PropertyID::Scale, TransformFunction::Scale, { maybe_x.release_nonnull(), maybe_y.release_nonnull() });
    }

    auto maybe_z = parse_number_percentage_value(tokens);
    if (!maybe_z)
        return nullptr;

    transaction.commit();
    return TransformationStyleValue::create(PropertyID::Scale, TransformFunction::Scale3d, { maybe_x.release_nonnull(), maybe_y.release_nonnull(), maybe_z.release_nonnull() });
}

// https://drafts.csswg.org/css-scrollbars/#propdef-scrollbar-color
RefPtr<CSSStyleValue const> Parser::parse_scrollbar_color_value(TokenStream<ComponentValue>& tokens)
{
    // auto | <color>{2}
    if (!tokens.has_next_token())
        return nullptr;
    if (auto auto_keyword = parse_all_as_single_keyword_value(tokens, Keyword::Auto))
        return auto_keyword;

    auto transaction = tokens.begin_transaction();

    auto thumb_color = parse_color_value(tokens);
    if (!thumb_color)
        return nullptr;

    tokens.discard_whitespace();

    auto track_color = parse_color_value(tokens);
    if (!track_color)
        return nullptr;
    tokens.discard_whitespace();
    transaction.commit();

    return ScrollbarColorStyleValue::create(thumb_color.release_nonnull(), track_color.release_nonnull());
}

// https://drafts.csswg.org/css-overflow/#propdef-scrollbar-gutter
RefPtr<CSSStyleValue const> Parser::parse_scrollbar_gutter_value(TokenStream<ComponentValue>& tokens)
{
    // auto | stable && both-edges?
    if (!tokens.has_next_token())
        return nullptr;

    auto transaction = tokens.begin_transaction();

    auto parse_stable = [&]() -> Optional<bool> {
        auto transaction = tokens.begin_transaction();
        auto const& token = tokens.consume_a_token();
        if (!token.is(Token::Type::Ident))
            return {};
        auto const& ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("auto"sv)) {
            transaction.commit();
            return false;
        } else if (ident.equals_ignoring_ascii_case("stable"sv)) {
            transaction.commit();
            return true;
        }
        return {};
    };

    auto parse_both_edges = [&]() -> Optional<bool> {
        auto transaction = tokens.begin_transaction();
        auto const& token = tokens.consume_a_token();
        if (!token.is(Token::Type::Ident))
            return {};
        auto const& ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("both-edges"sv)) {
            transaction.commit();
            return true;
        }
        return {};
    };

    Optional<bool> stable;
    Optional<bool> both_edges;
    if (stable = parse_stable(); stable.has_value()) {
        if (stable.value())
            both_edges = parse_both_edges();
    } else if (both_edges = parse_both_edges(); both_edges.has_value()) {
        stable = parse_stable();
        if (!stable.has_value() || !stable.value())
            return nullptr;
    }

    if (tokens.has_next_token())
        return nullptr;

    transaction.commit();

    ScrollbarGutter gutter_value;
    if (both_edges.has_value())
        gutter_value = ScrollbarGutter::BothEdges;
    else if (stable.has_value() && stable.value())
        gutter_value = ScrollbarGutter::Stable;
    else
        gutter_value = ScrollbarGutter::Auto;
    return ScrollbarGutterStyleValue::create(gutter_value);
}

RefPtr<CSSStyleValue const> Parser::parse_grid_track_placement_shorthand_value(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    auto start_property = (property_id == PropertyID::GridColumn) ? PropertyID::GridColumnStart : PropertyID::GridRowStart;
    auto end_property = (property_id == PropertyID::GridColumn) ? PropertyID::GridColumnEnd : PropertyID::GridRowEnd;

    auto transaction = tokens.begin_transaction();
    NonnullRawPtr<ComponentValue const> current_token = tokens.consume_a_token();

    Vector<ComponentValue> track_start_placement_tokens;
    while (true) {
        if (current_token->is_delim('/')) {
            if (!tokens.has_next_token())
                return nullptr;
            break;
        }
        track_start_placement_tokens.append(current_token);
        if (!tokens.has_next_token())
            break;
        current_token = tokens.consume_a_token();
    }

    Vector<ComponentValue> track_end_placement_tokens;
    if (tokens.has_next_token()) {
        current_token = tokens.consume_a_token();
        while (true) {
            track_end_placement_tokens.append(current_token);
            if (!tokens.has_next_token())
                break;
            current_token = tokens.consume_a_token();
        }
    }

    TokenStream track_start_placement_token_stream { track_start_placement_tokens };
    auto parsed_start_value = parse_grid_track_placement(track_start_placement_token_stream);
    if (parsed_start_value && track_end_placement_tokens.is_empty()) {
        transaction.commit();
        if (parsed_start_value->grid_track_placement().is_custom_ident()) {
            auto custom_ident = parsed_start_value.release_nonnull();
            return ShorthandStyleValue::create(property_id, { start_property, end_property }, { custom_ident, custom_ident });
        }
        return ShorthandStyleValue::create(property_id,
            { start_property, end_property },
            { parsed_start_value.release_nonnull(), GridTrackPlacementStyleValue::create(GridTrackPlacement::make_auto()) });
    }

    TokenStream track_end_placement_token_stream { track_end_placement_tokens };
    auto parsed_end_value = parse_grid_track_placement(track_end_placement_token_stream);
    if (parsed_start_value && parsed_end_value) {
        transaction.commit();
        return ShorthandStyleValue::create(property_id,
            { start_property, end_property },
            { parsed_start_value.release_nonnull(), parsed_end_value.release_nonnull() });
    }

    return nullptr;
}

// https://www.w3.org/TR/css-grid-2/#explicit-grid-shorthand
// 7.4. Explicit Grid Shorthand: the grid-template property
RefPtr<CSSStyleValue const> Parser::parse_grid_track_size_list_shorthand_value(PropertyID property_id, TokenStream<ComponentValue>& tokens, bool include_grid_auto_properties)
{
    Vector<PropertyID> sub_properties;
    Vector<ValueComparingNonnullRefPtr<CSSStyleValue const>> values;
    if (include_grid_auto_properties) {
        sub_properties.append(PropertyID::GridAutoFlow);
        sub_properties.append(PropertyID::GridAutoRows);
        sub_properties.append(PropertyID::GridAutoColumns);
        values.append(property_initial_value(PropertyID::GridAutoFlow));
        values.append(property_initial_value(PropertyID::GridAutoRows));
        values.append(property_initial_value(PropertyID::GridAutoColumns));
    }
    sub_properties.append(PropertyID::GridTemplateAreas);
    sub_properties.append(PropertyID::GridTemplateRows);
    sub_properties.append(PropertyID::GridTemplateColumns);
    values.ensure_capacity(sub_properties.size());

    // none
    {
        if (parse_all_as_single_keyword_value(tokens, Keyword::None)) {
            values.unchecked_append(property_initial_value(PropertyID::GridTemplateAreas));
            values.unchecked_append(property_initial_value(PropertyID::GridTemplateRows));
            values.unchecked_append(property_initial_value(PropertyID::GridTemplateColumns));
            return ShorthandStyleValue::create(property_id, move(sub_properties), move(values));
        }
    }

    // [ <'grid-template-rows'> / <'grid-template-columns'> ]
    {
        auto transaction = tokens.begin_transaction();
        if (auto parsed_template_rows_values = parse_grid_track_size_list(tokens)) {
            tokens.discard_whitespace();
            if (tokens.has_next_token() && tokens.next_token().is_delim('/')) {
                tokens.discard_a_token();
                tokens.discard_whitespace();
                if (auto parsed_template_columns_values = parse_grid_track_size_list(tokens)) {
                    transaction.commit();
                    values.unchecked_append(property_initial_value(PropertyID::GridTemplateAreas));
                    values.unchecked_append(parsed_template_rows_values.release_nonnull());
                    values.unchecked_append(parsed_template_columns_values.release_nonnull());
                    return ShorthandStyleValue::create(property_id, move(sub_properties), move(values));
                }
            }
        }
    }

    // [ <line-names>? <string> <track-size>? <line-names>? ]+ [ / <explicit-track-list> ]?
    {
        auto transaction = tokens.begin_transaction();

        GridTrackSizeList track_list;
        Vector<ComponentValue> area_tokens;

        GridTrackParser parse_grid_track = [&](TokenStream<ComponentValue>& tokens) -> Optional<ExplicitGridTrack> {
            if (!tokens.has_next_token())
                return {};
            tokens.discard_whitespace();
            auto const& token = tokens.consume_a_token();
            if (!token.is(Token::Type::String))
                return {};
            area_tokens.append(token);
            tokens.discard_whitespace();
            if (auto track_size = parse_grid_track_size(tokens); track_size.has_value())
                return track_size.release_value();
            tokens.discard_whitespace();
            return ExplicitGridTrack(GridSize::make_auto());
        };

        auto parsed_track_count = parse_track_list_impl(tokens, track_list, parse_grid_track, AllowTrailingLineNamesForEachTrack::Yes);
        if (parsed_track_count > 0) {
            TokenStream area_tokens_stream { area_tokens };
            auto grid_areas = parse_grid_template_areas_value(area_tokens_stream);
            if (!grid_areas)
                return nullptr;

            auto rows_track_list = GridTrackSizeListStyleValue::create(move(track_list));

            tokens.discard_whitespace();

            RefPtr columns_track_list = property_initial_value(PropertyID::GridTemplateColumns);
            if (tokens.has_next_token() && tokens.next_token().is_delim('/')) {
                tokens.discard_a_token();
                tokens.discard_whitespace();
                if (auto parsed_columns = parse_explicit_track_list(tokens); !parsed_columns.is_empty()) {
                    transaction.commit();
                    columns_track_list = GridTrackSizeListStyleValue::create(move(parsed_columns));
                } else {
                    return nullptr;
                }
            } else if (tokens.has_next_token()) {
                return nullptr;
            }

            transaction.commit();
            values.unchecked_append(grid_areas.release_nonnull());
            values.unchecked_append(rows_track_list);
            values.unchecked_append(columns_track_list.release_nonnull());
            return ShorthandStyleValue::create(property_id, move(sub_properties), move(values));
        }
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_grid_area_shorthand_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto parse_placement_tokens = [&](Vector<ComponentValue>& placement_tokens, bool check_for_delimiter = true) -> void {
        while (tokens.has_next_token()) {
            auto& current_token = tokens.consume_a_token();
            if (check_for_delimiter && current_token.is_delim('/'))
                break;
            placement_tokens.append(current_token);
        }
    };

    Vector<ComponentValue> row_start_placement_tokens;
    parse_placement_tokens(row_start_placement_tokens);

    Vector<ComponentValue> column_start_placement_tokens;
    if (tokens.has_next_token())
        parse_placement_tokens(column_start_placement_tokens);

    Vector<ComponentValue> row_end_placement_tokens;
    if (tokens.has_next_token())
        parse_placement_tokens(row_end_placement_tokens);

    Vector<ComponentValue> column_end_placement_tokens;
    if (tokens.has_next_token())
        parse_placement_tokens(column_end_placement_tokens, false);

    // https://www.w3.org/TR/css-grid-2/#placement-shorthands
    // The grid-area property is a shorthand for grid-row-start, grid-column-start, grid-row-end and
    // grid-column-end.
    TokenStream row_start_placement_token_stream { row_start_placement_tokens };
    auto row_start_style_value = parse_grid_track_placement(row_start_placement_token_stream);
    if (row_start_placement_token_stream.has_next_token())
        return nullptr;

    TokenStream column_start_placement_token_stream { column_start_placement_tokens };
    auto column_start_style_value = parse_grid_track_placement(column_start_placement_token_stream);
    if (column_start_placement_token_stream.has_next_token())
        return nullptr;

    TokenStream row_end_placement_token_stream { row_end_placement_tokens };
    auto row_end_style_value = parse_grid_track_placement(row_end_placement_token_stream);
    if (row_end_placement_token_stream.has_next_token())
        return nullptr;

    TokenStream column_end_placement_token_stream { column_end_placement_tokens };
    auto column_end_style_value = parse_grid_track_placement(column_end_placement_token_stream);
    if (column_end_placement_token_stream.has_next_token())
        return nullptr;

    // If four <grid-line> values are specified, grid-row-start is set to the first value, grid-column-start
    // is set to the second value, grid-row-end is set to the third value, and grid-column-end is set to the
    // fourth value.
    auto row_start = GridTrackPlacement::make_auto();
    auto column_start = GridTrackPlacement::make_auto();
    auto row_end = GridTrackPlacement::make_auto();
    auto column_end = GridTrackPlacement::make_auto();

    if (row_start_style_value)
        row_start = row_start_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();

    // When grid-column-start is omitted, if grid-row-start is a <custom-ident>, all four longhands are set to
    // that value. Otherwise, it is set to auto.
    if (column_start_style_value)
        column_start = column_start_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();
    else if (row_start.is_custom_ident())
        column_start = row_start;

    // When grid-row-end is omitted, if grid-row-start is a <custom-ident>, grid-row-end is set to that
    // <custom-ident>; otherwise, it is set to auto.
    if (row_end_style_value)
        row_end = row_end_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();
    else if (row_start.is_custom_ident())
        row_end = row_start;

    // When grid-column-end is omitted, if grid-column-start is a <custom-ident>, grid-column-end is set to
    // that <custom-ident>; otherwise, it is set to auto.
    if (column_end_style_value)
        column_end = column_end_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();
    else if (column_start.is_custom_ident())
        column_end = column_start;

    transaction.commit();
    return ShorthandStyleValue::create(PropertyID::GridArea,
        { PropertyID::GridRowStart, PropertyID::GridColumnStart, PropertyID::GridRowEnd, PropertyID::GridColumnEnd },
        { GridTrackPlacementStyleValue::create(row_start), GridTrackPlacementStyleValue::create(column_start), GridTrackPlacementStyleValue::create(row_end), GridTrackPlacementStyleValue::create(column_end) });
}

RefPtr<CSSStyleValue const> Parser::parse_grid_shorthand_value(TokenStream<ComponentValue>& tokens)
{
    // <'grid-template'>
    if (auto grid_template = parse_grid_track_size_list_shorthand_value(PropertyID::Grid, tokens, true)) {
        return grid_template;
    }

    auto parse_auto_flow_and_dense = [&](GridAutoFlowStyleValue::Axis axis) -> RefPtr<GridAutoFlowStyleValue const> {
        bool found_auto_flow = false;
        auto dense = GridAutoFlowStyleValue::Dense::No;
        for (int i = 0; i < 2 && tokens.has_next_token(); ++i) {
            auto const& token = tokens.next_token();
            if (token.is_ident("auto-flow"sv) && !found_auto_flow) {
                tokens.discard_a_token();
                tokens.discard_whitespace();
                found_auto_flow = true;
            } else if (token.is_ident("dense"sv) && dense == GridAutoFlowStyleValue::Dense::No) {
                tokens.discard_a_token();
                tokens.discard_whitespace();
                dense = GridAutoFlowStyleValue::Dense::Yes;
            } else {
                break;
            }
        }

        if (found_auto_flow)
            return GridAutoFlowStyleValue::create(axis, dense);
        return {};
    };

    // [ auto-flow && dense? ] <'grid-auto-rows'>? / <'grid-template-columns'>
    auto parse_shorthand_branch_1 = [&] -> RefPtr<CSSStyleValue const> {
        auto transaction = tokens.begin_transaction();
        tokens.discard_whitespace();

        auto grid_auto_flow = parse_auto_flow_and_dense(GridAutoFlowStyleValue::Axis::Row);
        if (!grid_auto_flow)
            return nullptr;

        auto grid_auto_rows = parse_grid_auto_track_sizes(tokens);
        if (!grid_auto_rows) {
            grid_auto_rows = GridTrackSizeListStyleValue::create({});
        }

        tokens.discard_whitespace();
        if (!tokens.has_next_token() || !tokens.next_token().is_delim('/'))
            return nullptr;
        tokens.discard_a_token();
        tokens.discard_whitespace();

        auto grid_template_columns = parse_grid_track_size_list(tokens);
        if (!grid_template_columns)
            return nullptr;

        transaction.commit();
        return ShorthandStyleValue::create(PropertyID::Grid,
            { PropertyID::GridAutoFlow, PropertyID::GridAutoRows, PropertyID::GridTemplateColumns },
            { grid_auto_flow.release_nonnull(), grid_auto_rows.release_nonnull(), grid_template_columns.release_nonnull() });
    };

    // <'grid-template-rows'> / [ auto-flow && dense? ] <'grid-auto-columns'>?
    auto parse_shorthand_branch_2 = [&] -> RefPtr<CSSStyleValue const> {
        auto transaction = tokens.begin_transaction();
        tokens.discard_whitespace();

        auto grid_template_rows = parse_grid_track_size_list(tokens);
        if (!grid_template_rows)
            return nullptr;

        tokens.discard_whitespace();
        if (!tokens.has_next_token() || !tokens.next_token().is_delim('/'))
            return nullptr;
        tokens.discard_a_token();
        tokens.discard_whitespace();

        auto grid_auto_flow = parse_auto_flow_and_dense(GridAutoFlowStyleValue::Axis::Column);
        if (!grid_auto_flow)
            return nullptr;

        auto grid_auto_columns = parse_grid_auto_track_sizes(tokens);
        if (!grid_auto_columns) {
            grid_auto_columns = GridTrackSizeListStyleValue::create({});
        }

        transaction.commit();
        return ShorthandStyleValue::create(PropertyID::Grid,
            { PropertyID::GridTemplateRows, PropertyID::GridAutoFlow, PropertyID::GridAutoColumns },
            { grid_template_rows.release_nonnull(), grid_auto_flow.release_nonnull(), grid_auto_columns.release_nonnull() });
    };

    if (auto grid = parse_shorthand_branch_1()) {
        return grid;
    }

    if (auto grid = parse_shorthand_branch_2()) {
        return grid;
    }

    return nullptr;
}

// https://www.w3.org/TR/css-grid-1/#grid-template-areas-property
RefPtr<CSSStyleValue const> Parser::parse_grid_template_areas_value(TokenStream<ComponentValue>& tokens)
{
    // none | <string>+
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return GridTemplateAreaStyleValue::create({});

    auto is_full_stop = [](u32 code_point) {
        return code_point == '.';
    };

    auto consume_while = [](Utf8CodePointIterator& code_points, AK::Function<bool(u32)> predicate) {
        StringBuilder builder;
        while (!code_points.done() && predicate(*code_points)) {
            builder.append_code_point(*code_points);
            ++code_points;
        }
        return MUST(builder.to_string());
    };

    Vector<Vector<String>> grid_area_rows;
    Optional<size_t> column_count;

    auto transaction = tokens.begin_transaction();
    while (tokens.has_next_token() && tokens.next_token().is(Token::Type::String)) {
        Vector<String> grid_area_columns;
        auto string = tokens.consume_a_token().token().string().code_points();
        auto code_points = string.begin();

        while (!code_points.done()) {
            if (is_whitespace(*code_points)) {
                consume_while(code_points, is_whitespace);
            } else if (is_full_stop(*code_points)) {
                consume_while(code_points, *is_full_stop);
                grid_area_columns.append("."_string);
            } else if (is_ident_code_point(*code_points)) {
                auto token = consume_while(code_points, is_ident_code_point);
                grid_area_columns.append(move(token));
            } else {
                return nullptr;
            }
        }

        if (grid_area_columns.is_empty())
            return nullptr;

        if (column_count.has_value()) {
            if (grid_area_columns.size() != column_count)
                return nullptr;
        } else {
            column_count = grid_area_columns.size();
        }

        grid_area_rows.append(move(grid_area_columns));
    }

    // FIXME: If a named grid area spans multiple grid cells, but those cells do not form a single filled-in rectangle, the declaration is invalid.

    transaction.commit();
    return GridTemplateAreaStyleValue::create(grid_area_rows);
}

RefPtr<CSSStyleValue const> Parser::parse_grid_auto_track_sizes(TokenStream<ComponentValue>& tokens)
{
    // https://www.w3.org/TR/css-grid-2/#auto-tracks
    // <track-size>+
    auto transaction = tokens.begin_transaction();
    GridTrackSizeList track_list;
    while (tokens.has_next_token()) {
        tokens.discard_whitespace();
        auto track_size = parse_grid_track_size(tokens);
        if (!track_size.has_value())
            break;
        track_list.append(track_size.release_value());
    }
    if (!track_list.is_empty())
        transaction.commit();
    return GridTrackSizeListStyleValue::create(move(track_list));
}

// https://www.w3.org/TR/css-grid-1/#grid-auto-flow-property
RefPtr<GridAutoFlowStyleValue const> Parser::parse_grid_auto_flow_value(TokenStream<ComponentValue>& tokens)
{
    // [ row | column ] || dense
    if (!tokens.has_next_token())
        return nullptr;

    auto transaction = tokens.begin_transaction();

    auto parse_axis = [&]() -> Optional<GridAutoFlowStyleValue::Axis> {
        auto transaction = tokens.begin_transaction();
        auto const& token = tokens.consume_a_token();
        if (!token.is(Token::Type::Ident))
            return {};
        auto const& ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("row"sv)) {
            transaction.commit();
            return GridAutoFlowStyleValue::Axis::Row;
        } else if (ident.equals_ignoring_ascii_case("column"sv)) {
            transaction.commit();
            return GridAutoFlowStyleValue::Axis::Column;
        }
        return {};
    };

    auto parse_dense = [&]() -> Optional<GridAutoFlowStyleValue::Dense> {
        auto transaction = tokens.begin_transaction();
        auto const& token = tokens.consume_a_token();
        if (!token.is(Token::Type::Ident))
            return {};
        auto const& ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("dense"sv)) {
            transaction.commit();
            return GridAutoFlowStyleValue::Dense::Yes;
        }
        return {};
    };

    Optional<GridAutoFlowStyleValue::Axis> axis;
    Optional<GridAutoFlowStyleValue::Dense> dense;
    if (axis = parse_axis(); axis.has_value()) {
        dense = parse_dense();
    } else if (dense = parse_dense(); dense.has_value()) {
        axis = parse_axis();
    }

    if (tokens.has_next_token())
        return nullptr;

    transaction.commit();
    return GridAutoFlowStyleValue::create(axis.value_or(GridAutoFlowStyleValue::Axis::Row), dense.value_or(GridAutoFlowStyleValue::Dense::No));
}

// https://www.w3.org/TR/css-grid-2/#track-sizing
RefPtr<CSSStyleValue const> Parser::parse_grid_track_size_list(TokenStream<ComponentValue>& tokens)
{
    // none | <track-list> | <auto-track-list> | FIXME subgrid <line-name-list>?

    // none
    {
        auto transaction = tokens.begin_transaction();
        if (tokens.has_next_token() && tokens.next_token().is_ident("none"sv)) {
            tokens.discard_a_token();
            transaction.commit();
            return GridTrackSizeListStyleValue::make_none();
        }
    }

    // <auto-track-list>
    auto auto_track_list = parse_grid_auto_track_list(tokens);
    if (!auto_track_list.is_empty()) {
        return GridTrackSizeListStyleValue::create(GridTrackSizeList(move(auto_track_list)));
    }

    // <track-list>
    auto track_list = parse_grid_track_list(tokens);
    if (!track_list.is_empty()) {
        return GridTrackSizeListStyleValue::create(GridTrackSizeList(move(track_list)));
    }

    return nullptr;
}

RefPtr<CSSStyleValue const> Parser::parse_filter_value_list_value(TokenStream<ComponentValue>& tokens)
{
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    auto transaction = tokens.begin_transaction();

    // FIXME: <url>s are ignored for now
    // <filter-value-list> = [ <filter-function> | <url> ]+

    enum class FilterToken {
        // Color filters:
        Brightness,
        Contrast,
        Grayscale,
        Invert,
        Opacity,
        Saturate,
        Sepia,
        // Special filters:
        Blur,
        DropShadow,
        HueRotate
    };

    auto filter_token_to_operation = [&](auto filter) {
        VERIFY(to_underlying(filter) < to_underlying(FilterToken::Blur));
        return static_cast<Gfx::ColorFilterType>(filter);
    };

    auto parse_filter_function_name = [&](auto name) -> Optional<FilterToken> {
        if (name.equals_ignoring_ascii_case("blur"sv))
            return FilterToken::Blur;
        if (name.equals_ignoring_ascii_case("brightness"sv))
            return FilterToken::Brightness;
        if (name.equals_ignoring_ascii_case("contrast"sv))
            return FilterToken::Contrast;
        if (name.equals_ignoring_ascii_case("drop-shadow"sv))
            return FilterToken::DropShadow;
        if (name.equals_ignoring_ascii_case("grayscale"sv))
            return FilterToken::Grayscale;
        if (name.equals_ignoring_ascii_case("hue-rotate"sv))
            return FilterToken::HueRotate;
        if (name.equals_ignoring_ascii_case("invert"sv))
            return FilterToken::Invert;
        if (name.equals_ignoring_ascii_case("opacity"sv))
            return FilterToken::Opacity;
        if (name.equals_ignoring_ascii_case("saturate"sv))
            return FilterToken::Saturate;
        if (name.equals_ignoring_ascii_case("sepia"sv))
            return FilterToken::Sepia;
        return {};
    };

    auto parse_filter_function = [&](auto filter_token, auto const& function_values) -> Optional<FilterValue> {
        TokenStream tokens { function_values };
        tokens.discard_whitespace();

        auto if_no_more_tokens_return = [&](auto filter) -> Optional<FilterValue> {
            tokens.discard_whitespace();
            if (tokens.has_next_token())
                return {};
            return filter;
        };

        if (filter_token == FilterToken::Blur) {
            // blur( <length>? )
            if (!tokens.has_next_token())
                return FilterOperation::Blur {};
            auto blur_radius = parse_length(tokens);
            tokens.discard_whitespace();
            if (!blur_radius.has_value() || (!blur_radius->is_calculated() && blur_radius->value().raw_value() < 0))
                return {};
            return if_no_more_tokens_return(FilterOperation::Blur { blur_radius.value() });
        } else if (filter_token == FilterToken::DropShadow) {
            if (!tokens.has_next_token())
                return {};
            // drop-shadow( [ <color>? && <length>{2,3} ] )
            // Note: The following code is a little awkward to allow the color to be before or after the lengths.
            Optional<LengthOrCalculated> maybe_radius = {};
            auto maybe_color = parse_color_value(tokens);
            tokens.discard_whitespace();
            auto x_offset = parse_length(tokens);
            tokens.discard_whitespace();
            if (!x_offset.has_value() || !tokens.has_next_token())
                return {};

            auto y_offset = parse_length(tokens);
            tokens.discard_whitespace();
            if (!y_offset.has_value())
                return {};

            if (tokens.has_next_token()) {
                maybe_radius = parse_length(tokens);
                tokens.discard_whitespace();
                if (!maybe_color && (!maybe_radius.has_value() || tokens.has_next_token())) {
                    maybe_color = parse_color_value(tokens);
                    if (!maybe_color)
                        return {};
                } else if (!maybe_radius.has_value()) {
                    return {};
                }
            }
            Optional<Color> color = {};
            if (maybe_color)
                // FIXME: We should support colors which require compute-time information (i.e. `em` and `vw` to `px` ratios).
                color = maybe_color->to_color({}, {});

            return if_no_more_tokens_return(FilterOperation::DropShadow { x_offset.value(), y_offset.value(), maybe_radius, color });
        } else if (filter_token == FilterToken::HueRotate) {
            // hue-rotate( [ <angle> | <zero> ]? )
            if (!tokens.has_next_token())
                return FilterOperation::HueRotate {};

            if (tokens.next_token().is(Token::Type::Number)) {
                // hue-rotate(0)
                auto number = tokens.consume_a_token().token().number();
                if (number.is_integer() && number.integer_value() == 0)
                    return if_no_more_tokens_return(FilterOperation::HueRotate { FilterOperation::HueRotate::Zero {} });
                return {};
            }

            if (auto angle = parse_angle(tokens); angle.has_value())
                return if_no_more_tokens_return(FilterOperation::HueRotate { angle.value() });

            return {};
        } else {
            // Simple filters:
            // brightness( <number-percentage>? )
            // contrast( <number-percentage>? )
            // grayscale( <number-percentage>? )
            // invert( <number-percentage>? )
            // opacity( <number-percentage>? )
            // sepia( <number-percentage>? )
            // saturate( <number-percentage>? )
            if (!tokens.has_next_token())
                return FilterOperation::Color { filter_token_to_operation(filter_token) };
            auto amount = parse_number_percentage(tokens);
            if (amount.has_value()) {
                if (amount->is_percentage() && amount->percentage().value() < 0)
                    return {};
                if (amount->is_number() && amount->number().value() < 0)
                    return {};
                if (first_is_one_of(filter_token, FilterToken::Grayscale, FilterToken::Invert, FilterToken::Opacity, FilterToken::Sepia)) {
                    if (amount->is_percentage() && amount->percentage().value() > 100)
                        amount = Percentage { 100 };
                    if (amount->is_number() && amount->number().value() > 1)
                        amount = Number { Number::Type::Integer, 1.0 };
                }
            }
            return if_no_more_tokens_return(FilterOperation::Color { filter_token_to_operation(filter_token), amount.value_or(Number { Number::Type::Integer, 1 }) });
        }
    };

    Vector<FilterValue> filter_value_list {};

    while (tokens.has_next_token()) {
        tokens.discard_whitespace();
        if (!tokens.has_next_token())
            break;

        auto url_function = parse_url_function(tokens);
        if (url_function.has_value()) {
            filter_value_list.append(*url_function);
            continue;
        }

        auto& token = tokens.consume_a_token();
        if (!token.is_function())
            return nullptr;
        auto filter_token = parse_filter_function_name(token.function().name);
        if (!filter_token.has_value())
            return nullptr;

        auto context_guard = push_temporary_value_parsing_context(FunctionContext { token.function().name });
        auto filter_function = parse_filter_function(*filter_token, token.function().value);
        if (!filter_function.has_value())
            return nullptr;
        filter_value_list.append(*filter_function);
    }

    if (filter_value_list.is_empty())
        return nullptr;

    transaction.commit();
    return FilterValueListStyleValue::create(move(filter_value_list));
}

RefPtr<CSSStyleValue const> Parser::parse_contain_value(TokenStream<ComponentValue>& tokens)
{
    // none | strict | content | [ [size | inline-size] || layout || style || paint ]
    auto transaction = tokens.begin_transaction();
    tokens.discard_whitespace();

    // none
    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None)) {
        transaction.commit();
        return none;
    }

    // strict
    if (auto strict = parse_all_as_single_keyword_value(tokens, Keyword::Strict)) {
        transaction.commit();
        return strict;
    }

    // content
    if (auto content = parse_all_as_single_keyword_value(tokens, Keyword::Content)) {
        transaction.commit();
        return content;
    }

    // [ [size | inline-size] || layout || style || paint ]
    if (!tokens.has_next_token())
        return {};

    RefPtr<CSSStyleValue const> size_value;
    RefPtr<CSSStyleValue const> layout_value;
    RefPtr<CSSStyleValue const> style_value;
    RefPtr<CSSStyleValue const> paint_value;

    while (tokens.has_next_token()) {
        tokens.discard_whitespace();
        auto keyword_value = parse_keyword_value(tokens);
        if (!keyword_value)
            return {};
        switch (keyword_value->to_keyword()) {
        case Keyword::Size:
        case Keyword::InlineSize:
            if (size_value)
                return {};
            size_value = move(keyword_value);
            break;
        case Keyword::Layout:
            if (layout_value)
                return {};
            layout_value = move(keyword_value);
            break;
        case Keyword::Style:
            if (style_value)
                return {};
            style_value = move(keyword_value);
            break;
        case Keyword::Paint:
            if (paint_value)
                return {};
            paint_value = move(keyword_value);
            break;
        default:
            return {};
        }
    }

    StyleValueVector containment_values;
    if (size_value)
        containment_values.append(size_value.release_nonnull());
    if (layout_value)
        containment_values.append(layout_value.release_nonnull());
    if (style_value)
        containment_values.append(style_value.release_nonnull());
    if (paint_value)
        containment_values.append(paint_value.release_nonnull());

    transaction.commit();

    return StyleValueList::create(move(containment_values), StyleValueList::Separator::Space);
}

// https://www.w3.org/TR/css-text-4/#white-space-trim
RefPtr<CSSStyleValue const> Parser::parse_white_space_trim_value(TokenStream<ComponentValue>& tokens)
{
    // none | discard-before || discard-after || discard-inner

    if (auto none = parse_all_as_single_keyword_value(tokens, Keyword::None))
        return none;

    auto transaction = tokens.begin_transaction();

    RefPtr<CSSStyleValue const> discard_before;
    RefPtr<CSSStyleValue const> discard_after;
    RefPtr<CSSStyleValue const> discard_inner;

    while (auto parsed_value = parse_css_value_for_property(PropertyID::WhiteSpaceTrim, tokens)) {
        switch (parsed_value->as_keyword().keyword()) {
        case Keyword::DiscardBefore:
            if (discard_before)
                return {};
            discard_before = parsed_value;
            break;
        case Keyword::DiscardAfter:
            if (discard_after)
                return {};
            discard_after = parsed_value;
            break;
        case Keyword::DiscardInner:
            if (discard_inner)
                return {};
            discard_inner = parsed_value;
            break;
        default:
            return {};
        }

        if (!tokens.has_next_token())
            break;
    }

    StyleValueVector parsed_values;

    // NOTE: The values are appended here rather than in the loop above to canonicalize their order.
    if (discard_before)
        parsed_values.append(discard_before.release_nonnull());
    if (discard_after)
        parsed_values.append(discard_after.release_nonnull());
    if (discard_inner)
        parsed_values.append(discard_inner.release_nonnull());

    transaction.commit();

    return StyleValueList::create(move(parsed_values), StyleValueList::Separator::Space);
}

// https://www.w3.org/TR/css-text-4/#white-space-property
RefPtr<CSSStyleValue const> Parser::parse_white_space_shorthand(TokenStream<ComponentValue>& tokens)
{
    // normal | pre | pre-wrap | pre-line | <'white-space-collapse'> || <'text-wrap-mode'> || <'white-space-trim'>

    auto transaction = tokens.begin_transaction();

    auto make_whitespace_shorthand = [&](RefPtr<CSSStyleValue const> white_space_collapse, RefPtr<CSSStyleValue const> text_wrap_mode, RefPtr<CSSStyleValue const> white_space_trim) {
        transaction.commit();

        if (!white_space_collapse)
            white_space_collapse = property_initial_value(PropertyID::WhiteSpaceCollapse);

        if (!text_wrap_mode)
            text_wrap_mode = property_initial_value(PropertyID::TextWrapMode);

        if (!white_space_trim)
            white_space_trim = property_initial_value(PropertyID::WhiteSpaceTrim);

        return ShorthandStyleValue::create(
            PropertyID::WhiteSpace,
            { PropertyID::WhiteSpaceCollapse, PropertyID::TextWrapMode, PropertyID::WhiteSpaceTrim },
            { white_space_collapse.release_nonnull(), text_wrap_mode.release_nonnull(), white_space_trim.release_nonnull() });
    };

    // normal | pre | pre-wrap | pre-line
    if (parse_all_as_single_keyword_value(tokens, Keyword::Normal))
        return make_whitespace_shorthand(CSSKeywordValue::create(Keyword::Collapse), CSSKeywordValue::create(Keyword::Wrap), CSSKeywordValue::create(Keyword::None));

    if (parse_all_as_single_keyword_value(tokens, Keyword::Pre))
        return make_whitespace_shorthand(CSSKeywordValue::create(Keyword::Preserve), CSSKeywordValue::create(Keyword::Nowrap), CSSKeywordValue::create(Keyword::None));

    if (parse_all_as_single_keyword_value(tokens, Keyword::PreWrap))
        return make_whitespace_shorthand(CSSKeywordValue::create(Keyword::Preserve), CSSKeywordValue::create(Keyword::Wrap), CSSKeywordValue::create(Keyword::None));

    if (parse_all_as_single_keyword_value(tokens, Keyword::PreLine))
        return make_whitespace_shorthand(CSSKeywordValue::create(Keyword::PreserveBreaks), CSSKeywordValue::create(Keyword::Wrap), CSSKeywordValue::create(Keyword::None));

    // <'white-space-collapse'> || <'text-wrap-mode'> || <'white-space-trim'>
    RefPtr<CSSStyleValue const> white_space_collapse;
    RefPtr<CSSStyleValue const> text_wrap_mode;
    RefPtr<CSSStyleValue const> white_space_trim;

    while (tokens.has_next_token()) {
        if (auto value = parse_css_value_for_property(PropertyID::WhiteSpaceCollapse, tokens)) {
            if (white_space_collapse)
                return {};
            white_space_collapse = value;
            continue;
        }

        if (auto value = parse_css_value_for_property(PropertyID::TextWrapMode, tokens)) {
            if (text_wrap_mode)
                return {};
            text_wrap_mode = value;
            continue;
        }

        Vector<ComponentValue> white_space_trim_component_values;

        while (true) {
            auto peek_token = tokens.next_token();

            if (!peek_token.is(Token::Type::Ident)) {
                break;
            }

            auto keyword = keyword_from_string(peek_token.token().ident());

            if (!keyword.has_value() || !property_accepts_keyword(PropertyID::WhiteSpaceTrim, keyword.value())) {
                break;
            }

            white_space_trim_component_values.append(tokens.consume_a_token());
        }

        if (!white_space_trim_component_values.is_empty()) {
            auto white_space_trim_token_stream = TokenStream { white_space_trim_component_values };

            if (auto value = parse_white_space_trim_value(white_space_trim_token_stream)) {
                if (white_space_trim)
                    return {};
                white_space_trim = value;
                continue;
            }
        }

        return {};
    }

    return make_whitespace_shorthand(white_space_collapse, text_wrap_mode, white_space_trim);
}

}
