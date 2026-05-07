// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <array>

namespace Markoff::Detail {

struct EmojiEntry { const char *shortcode; const char *glyph; };

constexpr std::array<EmojiEntry, 50> kEmojis = {{
    { "smile",        "\xF0\x9F\x98\x83" },  // U+1F603
    { "smiley",       "\xF0\x9F\x98\x80" },
    { "grinning",     "\xF0\x9F\x98\x80" },
    { "joy",          "\xF0\x9F\x98\x82" },
    { "heart_eyes",   "\xF0\x9F\x98\x8D" },
    { "wink",         "\xF0\x9F\x98\x89" },
    { "thinking",     "\xF0\x9F\xA4\x94" },
    { "thumbsup",     "\xF0\x9F\x91\x8D" },
    { "thumbsdown",   "\xF0\x9F\x91\x8E" },
    { "ok_hand",      "\xF0\x9F\x91\x8C" },
    { "clap",         "\xF0\x9F\x91\x8F" },
    { "wave",         "\xF0\x9F\x91\x8B" },
    { "pray",         "\xF0\x9F\x99\x8F" },
    { "muscle",       "\xF0\x9F\x92\xAA" },
    { "fire",         "\xF0\x9F\x94\xA5" },
    { "sparkles",     "\xE2\x9C\xA8" },
    { "star",         "\xE2\xAD\x90" },
    { "heart",        "\xE2\x9D\xA4" },
    { "broken_heart", "\xF0\x9F\x92\x94" },
    { "tada",         "\xF0\x9F\x8E\x89" },
    { "rocket",       "\xF0\x9F\x9A\x80" },
    { "warning",      "\xE2\x9A\xA0" },
    { "x",            "\xE2\x9D\x8C" },
    { "white_check_mark", "\xE2\x9C\x85" },
    { "check",        "\xE2\x9C\x94" },
    { "eyes",         "\xF0\x9F\x91\x80" },
    { "see_no_evil",  "\xF0\x9F\x99\x88" },
    { "robot",        "\xF0\x9F\xA4\x96" },
    { "ghost",        "\xF0\x9F\x91\xBB" },
    { "skull",        "\xF0\x9F\x92\x80" },
    { "poop",         "\xF0\x9F\x92\xA9" },
    { "cake",         "\xF0\x9F\x8E\x82" },
    { "pizza",        "\xF0\x9F\x8D\x95" },
    { "coffee",       "\xE2\x98\x95" },
    { "tea",          "\xF0\x9F\xA7\x8B" },
    { "beer",         "\xF0\x9F\x8D\xBA" },
    { "computer",     "\xF0\x9F\x92\xBB" },
    { "phone",        "\xF0\x9F\x93\xB1" },
    { "book",         "\xF0\x9F\x93\x96" },
    { "pencil",       "\xE2\x9C\x8F" },
    { "memo",         "\xF0\x9F\x93\x9D" },
    { "bug",          "\xF0\x9F\x90\x9B" },
    { "lock",         "\xF0\x9F\x94\x92" },
    { "key",          "\xF0\x9F\x94\x91" },
    { "bulb",         "\xF0\x9F\x92\xA1" },
    { "calendar",     "\xF0\x9F\x93\x85" },
    { "moon",         "\xF0\x9F\x8C\x99" },
    { "sun_with_face","\xF0\x9F\x8C\x9E" },
    { "rainbow",      "\xF0\x9F\x8C\x88" },
    { "snowflake",    "\xE2\x9D\x84" },
}};

}  // namespace Markoff::Detail
